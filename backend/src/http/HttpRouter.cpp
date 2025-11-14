#include "http/HttpRouter.hpp"

#include <cctype>
#include <optional>
#include <string>
#include <vector>

#include "auth/AuthManager.hpp"
#include "httplib.h"
#include "network/NetworkConfigService.hpp"
#include "trdp/ConfigService.hpp"

namespace trdp::http {

namespace {
std::string escapeJson(const std::string &value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '"':
                escaped += "\\\"";
                break;
            case '\\':
                escaped += "\\\\";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped += ch;
                break;
        }
    }
    return escaped;
}

std::optional<std::string> extractJsonStringField(const std::string &body, const std::string &field_name) {
    const std::string needle = "\"" + field_name + "\"";
    auto key_pos = body.find(needle);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }

    auto colon_pos = body.find(':', key_pos + needle.size());
    if (colon_pos == std::string::npos) {
        return std::nullopt;
    }

    auto start_quote = body.find('"', colon_pos);
    if (start_quote == std::string::npos) {
        return std::nullopt;
    }

    auto end_quote = body.find('"', start_quote + 1);
    if (end_quote == std::string::npos) {
        return std::nullopt;
    }

    return body.substr(start_quote + 1, end_quote - start_quote - 1);
}

std::optional<int> extractJsonIntField(const std::string &body, const std::string &field_name) {
    const std::string needle = "\"" + field_name + "\"";
    auto key_pos = body.find(needle);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }

    auto colon_pos = body.find(':', key_pos + needle.size());
    if (colon_pos == std::string::npos) {
        return std::nullopt;
    }

    auto value_start = body.find_first_of("-0123456789", colon_pos + 1);
    if (value_start == std::string::npos) {
        return std::nullopt;
    }

    auto value_end = value_start;
    while (value_end < body.size() && std::isdigit(static_cast<unsigned char>(body[value_end]))) {
        ++value_end;
    }

    try {
        return std::stoi(body.substr(value_start, value_end - value_start));
    } catch (const std::exception &) {
        return std::nullopt;
    }
}

std::optional<std::vector<std::string>> extractJsonStringArrayField(const std::string &body,
                                                                    const std::string &field_name) {
    const std::string needle = "\"" + field_name + "\"";
    auto key_pos = body.find(needle);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }

    auto open_bracket = body.find('[', key_pos + needle.size());
    if (open_bracket == std::string::npos) {
        return std::nullopt;
    }

    auto close_bracket = body.find(']', open_bracket + 1);
    if (close_bracket == std::string::npos) {
        return std::nullopt;
    }

    std::vector<std::string> values;
    size_t cursor = open_bracket + 1;
    while (cursor < close_bracket) {
        auto quote_start = body.find('"', cursor);
        if (quote_start == std::string::npos || quote_start >= close_bracket) {
            break;
        }
        auto quote_end = body.find('"', quote_start + 1);
        if (quote_end == std::string::npos || quote_end > close_bracket) {
            return std::nullopt;
        }
        values.push_back(body.substr(quote_start + 1, quote_end - quote_start - 1));
        cursor = quote_end + 1;
    }

    return values;
}

std::string serializeStringArray(const std::vector<std::string> &values) {
    std::string payload = "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            payload += ",";
        }
        payload += "\"" + escapeJson(values[i]) + "\"";
    }
    payload += "]";
    return payload;
}

std::string serializeNetworkConfig(const network::NetworkConfig &config) {
    return std::string{"{"} +
           "\"interface_name\":\"" + escapeJson(config.interface_name) + "\"," +
           "\"local_ip\":\"" + escapeJson(config.local_ip) + "\"," +
           "\"multicast_groups\":" + serializeStringArray(config.multicast_groups) + "," +
           "\"pd_port\":" + std::to_string(config.pd_port) + "," +
           "\"md_port\":" + std::to_string(config.md_port) + "}";
}

std::string jsonError(const std::string &message) {
    return std::string{"{\"error\":\""} + escapeJson(message) + "\"}";
}
}  // namespace

HttpRouter::HttpRouter(auth::AuthManager &auth_manager, config::ConfigService &config_service,
                       network::NetworkConfigService &network_config_service)
    : auth_manager_(auth_manager), config_service_(config_service), network_config_service_(network_config_service) {}

void HttpRouter::registerRoutes(httplib::Server &server) {
    registerHealthEndpoint(server);
    auth_manager_.registerRoutes(server);
    config_service_.registerRoutes(server);
    registerNetworkConfigEndpoints(server);
}

void HttpRouter::registerHealthEndpoint(httplib::Server &server) {
    server.Get("/health", [](const httplib::Request &, httplib::Response &res) {
        res.set_content("{\"status\":\"OK\"}", "application/json");
    });
}

void HttpRouter::registerNetworkConfigEndpoints(httplib::Server &server) {
    server.Get("/api/network/config", [this](const httplib::Request &req, httplib::Response &res) {
        auto user = auth_manager_.userFromRequest(req);
        if (!user) {
            res.status = 401;
            res.set_content(jsonError("authentication required"), "application/json");
            return;
        }

        try {
            auto config = network_config_service_.loadConfig();
            if (!config) {
                res.status = 200;
                res.set_content("{\"config\":null}", "application/json");
                return;
            }

            std::string payload = "{\"config\":" + serializeNetworkConfig(*config) + "}";
            res.status = 200;
            res.set_content(payload, "application/json");
        } catch (const std::exception &ex) {
            res.status = 500;
            res.set_content(jsonError(ex.what()), "application/json");
        }
    });

    server.Post("/api/network/config", [this](const httplib::Request &req, httplib::Response &res) {
        auto user = auth_manager_.userFromRequest(req);
        if (!user) {
            res.status = 401;
            res.set_content(jsonError("authentication required"), "application/json");
            return;
        }

        auto interface_name = extractJsonStringField(req.body, "interface_name");
        auto local_ip = extractJsonStringField(req.body, "local_ip");
        auto multicast_groups = extractJsonStringArrayField(req.body, "multicast_groups");
        auto pd_port = extractJsonIntField(req.body, "pd_port");
        auto md_port = extractJsonIntField(req.body, "md_port");

        if (!interface_name || !local_ip || !multicast_groups || !pd_port || !md_port) {
            res.status = 400;
            res.set_content(jsonError("interface_name, local_ip, multicast_groups, pd_port, and md_port are required"),
                            "application/json");
            return;
        }

        network::NetworkConfig config;
        config.interface_name = *interface_name;
        config.local_ip = *local_ip;
        config.multicast_groups = *multicast_groups;
        config.pd_port = *pd_port;
        config.md_port = *md_port;

        try {
            auto stored = network_config_service_.saveConfig(config);
            std::string payload = "{\"config\":" + serializeNetworkConfig(stored) + "}";
            res.status = 200;
            res.set_content(payload, "application/json");
        } catch (const std::exception &ex) {
            res.status = 500;
            res.set_content(jsonError(ex.what()), "application/json");
        }
    });
}

}  // namespace trdp::http

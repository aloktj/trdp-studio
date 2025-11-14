#include "http/HttpRouter.hpp"

#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "auth/AuthManager.hpp"
#include "httplib.h"
#include "network/NetworkConfigService.hpp"
#include "trdp/ConfigService.hpp"
#include "trdp/TrdpEngine.hpp"

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

int hexValue(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    }
    if (ch >= 'A' && ch <= 'F') {
        return 10 + (ch - 'A');
    }
    return -1;
}

std::optional<std::vector<uint8_t>> parseHexString(const std::string &hex) {
    if (hex.size() % 2 != 0) {
        return std::nullopt;
    }

    std::vector<uint8_t> bytes;
    bytes.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        int hi = hexValue(hex[i]);
        int lo = hexValue(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            return std::nullopt;
        }
        bytes.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return bytes;
}

std::string bytesToHex(const std::vector<uint8_t> &data) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(data.size() * 2);
    for (uint8_t byte : data) {
        hex.push_back(kHex[(byte >> 4) & 0x0F]);
        hex.push_back(kHex[byte & 0x0F]);
    }
    return hex;
}

std::string serializePdMessage(const stack::PdMessage &message) {
    return std::string{"{"} + "\"id\":" + std::to_string(message.id) + "," +
           "\"name\":\"" + escapeJson(message.name) + "\"," +
           "\"payload_hex\":\"" + bytesToHex(message.payload) + "\"," +
           "\"timestamp\":\"" + escapeJson(message.timestamp) + "\"}";
}

std::string serializeMdMessage(const stack::MdMessage &message) {
    return std::string{"{"} + "\"id\":" + std::to_string(message.id) + "," +
           "\"source\":\"" + escapeJson(message.source) + "\"," +
           "\"destination\":\"" + escapeJson(message.destination) + "\"," +
           "\"payload_hex\":\"" + bytesToHex(message.payload) + "\"," +
           "\"timestamp\":\"" + escapeJson(message.timestamp) + "\"}";
}

std::optional<int> extractPathId(const httplib::Request &req) {
    if (req.matches.size() < 2) {
        return std::nullopt;
    }

    try {
        return std::stoi(req.matches[1]);
    } catch (const std::exception &) {
        return std::nullopt;
    }
}

std::string jsonError(const std::string &message) {
    return std::string{"{\"error\":\""} + escapeJson(message) + "\"}";
}
}  // namespace

HttpRouter::HttpRouter(auth::AuthManager &auth_manager, config::ConfigService &config_service,
                       network::NetworkConfigService &network_config_service, stack::TrdpEngine &trdp_engine)
    : auth_manager_(auth_manager),
      config_service_(config_service),
      network_config_service_(network_config_service),
      trdp_engine_(trdp_engine) {}

void HttpRouter::registerRoutes(httplib::Server &server) {
    registerHealthEndpoint(server);
    auth_manager_.registerRoutes(server);
    config_service_.registerRoutes(server);
    registerNetworkConfigEndpoints(server);
    registerTrdpEngineEndpoints(server);
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

void HttpRouter::registerTrdpEngineEndpoints(httplib::Server &server) {
    server.Get("/api/pd/outgoing", [this](const httplib::Request &req, httplib::Response &res) {
        auto user = auth_manager_.userFromRequest(req);
        if (!user) {
            res.status = 401;
            res.set_content(jsonError("authentication required"), "application/json");
            return;
        }

        auto messages = trdp_engine_.listOutgoingPd();
        std::string payload = "{\"messages\":[";
        for (size_t i = 0; i < messages.size(); ++i) {
            if (i != 0) {
                payload += ",";
            }
            payload += serializePdMessage(messages[i]);
        }
        payload += "]}";
        res.status = 200;
        res.set_content(payload, "application/json");
    });

    server.Get("/api/pd/incoming", [this](const httplib::Request &req, httplib::Response &res) {
        auto user = auth_manager_.userFromRequest(req);
        if (!user) {
            res.status = 401;
            res.set_content(jsonError("authentication required"), "application/json");
            return;
        }

        auto messages = trdp_engine_.listIncomingPd();
        std::string payload = "{\"messages\":[";
        for (size_t i = 0; i < messages.size(); ++i) {
            if (i != 0) {
                payload += ",";
            }
            payload += serializePdMessage(messages[i]);
        }
        payload += "]}";
        res.status = 200;
        res.set_content(payload, "application/json");
    });

    server.Post(R"(/api/pd/outgoing/(\d+)/payload)", [this](const httplib::Request &req, httplib::Response &res) {
        auto user = auth_manager_.userFromRequest(req);
        if (!user) {
            res.status = 401;
            res.set_content(jsonError("authentication required"), "application/json");
            return;
        }

        auto msg_id = extractPathId(req);
        if (!msg_id) {
            res.status = 400;
            res.set_content(jsonError("invalid PD message id"), "application/json");
            return;
        }

        auto payload_hex = extractJsonStringField(req.body, "payload_hex");
        if (!payload_hex) {
            res.status = 400;
            res.set_content(jsonError("payload_hex is required"), "application/json");
            return;
        }

        auto payload_bytes = parseHexString(*payload_hex);
        if (!payload_bytes) {
            res.status = 400;
            res.set_content(jsonError("payload_hex must be an even-length hex string"), "application/json");
            return;
        }

        try {
            trdp_engine_.updateOutgoingPdPayload(*msg_id, *payload_bytes);
            res.status = 200;
            res.set_content("{\"status\":\"updated\"}", "application/json");
        } catch (const std::exception &ex) {
            std::string message = ex.what();
            if (message.find("not found") != std::string::npos) {
                res.status = 404;
            } else {
                res.status = 500;
            }
            res.set_content(jsonError(message), "application/json");
        }
    });

    server.Post("/api/md/send", [this](const httplib::Request &req, httplib::Response &res) {
        auto user = auth_manager_.userFromRequest(req);
        if (!user) {
            res.status = 401;
            res.set_content(jsonError("authentication required"), "application/json");
            return;
        }

        auto destination = extractJsonStringField(req.body, "destination");
        auto payload_hex = extractJsonStringField(req.body, "payload_hex");

        if (!destination || !payload_hex) {
            res.status = 400;
            res.set_content(jsonError("destination and payload_hex are required"), "application/json");
            return;
        }

        auto payload_bytes = parseHexString(*payload_hex);
        if (!payload_bytes) {
            res.status = 400;
            res.set_content(jsonError("payload_hex must be an even-length hex string"), "application/json");
            return;
        }

        try {
            auto message = trdp_engine_.sendMdMessage(*destination, *payload_bytes);
            std::string payload = "{\"message\":" + serializeMdMessage(message) + "}";
            res.status = 200;
            res.set_content(payload, "application/json");
        } catch (const std::exception &ex) {
            res.status = 500;
            res.set_content(jsonError(ex.what()), "application/json");
        }
    });

    server.Get("/api/md/incoming", [this](const httplib::Request &req, httplib::Response &res) {
        auto user = auth_manager_.userFromRequest(req);
        if (!user) {
            res.status = 401;
            res.set_content(jsonError("authentication required"), "application/json");
            return;
        }

        auto messages = trdp_engine_.listIncomingMd();
        std::string payload = "{\"messages\":[";
        for (size_t i = 0; i < messages.size(); ++i) {
            if (i != 0) {
                payload += ",";
            }
            payload += serializeMdMessage(messages[i]);
        }
        payload += "]}";
        res.status = 200;
        res.set_content(payload, "application/json");
    });
}

}  // namespace trdp::http

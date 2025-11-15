#include "trdp/ConfigService.hpp"

#include <cctype>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "auth/AuthManager.hpp"
#include "httplib.h"
#include "network/NetworkConfigService.hpp"
#include "trdp/PlanBuilder.hpp"
#include "trdp/TrdpConfigService.hpp"
#include "trdp/TrdpXmlParser.hpp"
#include "trdp/TrdpEngine.hpp"

namespace trdp::config {
namespace {
int hexDigit(char ch) {
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

void appendUtf8(char32_t codepoint, std::string &out) {
    if (codepoint <= 0x7F) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

bool appendUnicodeEscape(const std::string &body, size_t &cursor, std::string &out) {
    auto decodeCodeUnit = [&](size_t start, char32_t &unit) {
        if (start + 4 > body.size()) {
            return false;
        }
        unit = 0;
        for (size_t i = start; i < start + 4; ++i) {
            int digit = hexDigit(body[i]);
            if (digit < 0) {
                return false;
            }
            unit = static_cast<char32_t>((unit << 4) | digit);
        }
        return true;
    };

    size_t first_unit_pos = cursor + 1;
    char32_t code_unit = 0;
    if (!decodeCodeUnit(first_unit_pos, code_unit)) {
        return false;
    }
    cursor = first_unit_pos + 3;

    if (code_unit >= 0xD800 && code_unit <= 0xDBFF) {
        size_t next = cursor + 1;
        if (next + 1 >= body.size() || body[next] != '\\' || body[next + 1] != 'u') {
            return false;
        }
        next += 2;
        char32_t low_unit = 0;
        if (!decodeCodeUnit(next, low_unit)) {
            return false;
        }
        if (low_unit < 0xDC00 || low_unit > 0xDFFF) {
            return false;
        }
        cursor = next + 3;
        char32_t codepoint = 0x10000 + (((code_unit - 0xD800) << 10) | (low_unit - 0xDC00));
        appendUtf8(codepoint, out);
        return true;
    }

    if (code_unit >= 0xDC00 && code_unit <= 0xDFFF) {
        return false;
    }

    appendUtf8(code_unit, out);
    return true;
}

std::optional<std::string> parseJsonStringToken(const std::string &body, size_t start_quote) {
    if (start_quote >= body.size() || body[start_quote] != '"') {
        return std::nullopt;
    }
    std::string value;
    value.reserve(body.size() - start_quote);
    bool escaping = false;
    size_t cursor = start_quote + 1;
    while (cursor < body.size()) {
        char ch = body[cursor];
        if (escaping) {
            escaping = false;
            switch (ch) {
                case '"':
                    value.push_back('"');
                    break;
                case '\\':
                    value.push_back('\\');
                    break;
                case '/':
                    value.push_back('/');
                    break;
                case 'b':
                    value.push_back('\b');
                    break;
                case 'f':
                    value.push_back('\f');
                    break;
                case 'n':
                    value.push_back('\n');
                    break;
                case 'r':
                    value.push_back('\r');
                    break;
                case 't':
                    value.push_back('\t');
                    break;
                case 'u':
                    if (!appendUnicodeEscape(body, cursor, value)) {
                        return std::nullopt;
                    }
                    ++cursor;
                    continue;
                default:
                    return std::nullopt;
            }
            ++cursor;
            continue;
        }
        if (ch == '\\') {
            escaping = true;
            ++cursor;
            continue;
        }
        if (ch == '"') {
            return value;
        }
        value.push_back(ch);
        ++cursor;
    }
    return std::nullopt;
}

std::optional<long long> parseId(const httplib::Request &req) {
    if (req.matches.size() < 2) {
        return std::nullopt;
    }
    try {
        return std::stoll(req.matches[1]);
    } catch (const std::exception &) {
        return std::nullopt;
    }
}
}  // namespace

ConfigService::ConfigService(auth::AuthManager &auth_manager, TrdpConfigService &config_service,
                             network::NetworkConfigService &network_config_service,
                             stack::TrdpEngine &trdp_engine)
    : auth_manager_(auth_manager),
      config_service_(config_service),
      network_config_service_(network_config_service),
      trdp_engine_(trdp_engine) {}

void ConfigService::registerRoutes(httplib::Server &server) {
    server.Get("/api/trdp/configs", [this](const httplib::Request &req, httplib::Response &res) {
        handleListConfigs(req, res);
    });

    server.Post("/api/trdp/configs", [this](const httplib::Request &req, httplib::Response &res) {
        handleCreateConfig(req, res);
    });

    server.Get(R"(/api/trdp/configs/(\d+))", [this](const httplib::Request &req, httplib::Response &res) {
        handleGetConfig(req, res);
    });

    server.Post(R"(/api/trdp/configs/(\d+)/activate)", [this](const httplib::Request &req, httplib::Response &res) {
        handleActivateConfig(req, res);
    });

    server.Get(R"(/api/trdp/configs/(\d+)/plan)", [this](const httplib::Request &req, httplib::Response &res) {
        handlePlanForConfig(req, res);
    });
}

void ConfigService::handleListConfigs(const httplib::Request &req, httplib::Response &res) {
    auto user_id = requireUserId(req, res);
    if (!user_id) {
        return;
    }

    try {
        auto configs = config_service_.listConfigsForUser(*user_id);
        std::string payload = "{\"configs\":[";
        for (size_t i = 0; i < configs.size(); ++i) {
            if (i != 0) {
                payload += ",";
            }
            payload += serializeConfigMetadata(configs[i]);
        }
        payload += "]}";
        res.status = 200;
        res.set_content(payload, "application/json");
    } catch (const std::exception &ex) {
        res.status = 500;
        res.set_content(jsonError(ex.what()), "application/json");
    }
}

void ConfigService::handleCreateConfig(const httplib::Request &req, httplib::Response &res) {
    auto user_id = requireUserId(req, res);
    if (!user_id) {
        return;
    }

    auto name = extractJsonField(req.body, "name");
    auto xml = extractJsonField(req.body, "xml");

    if (!name || !xml) {
        res.status = 400;
        res.set_content(jsonError("name and xml are required"), "application/json");
        return;
    }

    try {
        auto config = config_service_.createConfig(*user_id, *name, *xml);
        std::string payload = "{\"config\":" + serializeConfigWithXml(config) + "}";
        res.status = 201;
        res.set_content(payload, "application/json");
    } catch (const std::exception &ex) {
        res.status = 500;
        res.set_content(jsonError(ex.what()), "application/json");
    }
}

void ConfigService::handleGetConfig(const httplib::Request &req, httplib::Response &res) {
    auto user_id = requireUserId(req, res);
    if (!user_id) {
        return;
    }

    auto config_id = parseId(req);
    if (!config_id) {
        res.status = 400;
        res.set_content(jsonError("invalid config id"), "application/json");
        return;
    }

    try {
        auto config = config_service_.getConfigById(*config_id);
        if (!config || config->user_id != *user_id) {
            res.status = 404;
            res.set_content(jsonError("config not found"), "application/json");
            return;
        }

        std::string payload = "{\"config\":" + serializeConfigWithXml(*config) + "}";
        res.status = 200;
        res.set_content(payload, "application/json");
    } catch (const std::exception &ex) {
        res.status = 500;
        res.set_content(jsonError(ex.what()), "application/json");
    }
}

void ConfigService::handleActivateConfig(const httplib::Request &req, httplib::Response &res) {
    auto user_id = requireUserId(req, res);
    if (!user_id) {
        return;
    }

    auto config_id = parseId(req);
    if (!config_id) {
        res.status = 400;
        res.set_content(jsonError("invalid config id"), "application/json");
        return;
    }

    try {
        auto config = config_service_.getConfigById(*config_id);
        if (!config || config->user_id != *user_id) {
            res.status = 404;
            res.set_content(jsonError("config not found"), "application/json");
            return;
        }

        config_service_.setActiveConfig(*config_id);
        loadConfigIntoEngine(*config);
        res.status = 200;
        res.set_content("{\"status\":\"activated\",\"config_id\":" + std::to_string(*config_id) + "}",
                        "application/json");
    } catch (const std::exception &ex) {
        res.status = 500;
        res.set_content(jsonError(ex.what()), "application/json");
    }
}

bool ConfigService::ensureTrdpEngineLoaded() {
    try {
        auto active = config_service_.getActiveConfig();
        if (!active) {
            return false;
        }
        return loadConfigIntoEngine(*active);
    } catch (const std::exception &ex) {
        std::cerr << "Failed to load active TRDP configuration: " << ex.what() << std::endl;
        return false;
    }
}

bool ConfigService::loadConfigIntoEngine(const TrdpConfig &config) {
    auto net_cfg = network_config_service_.loadConfig();
    if (!net_cfg) {
        return false;
    }
    try {
        return trdp_engine_.loadConfiguration(config, *net_cfg);
    } catch (const std::exception &ex) {
        std::cerr << "Failed to load TRDP configuration into engine: " << ex.what() << std::endl;
        return false;
    }
}

void ConfigService::handlePlanForConfig(const httplib::Request &req, httplib::Response &res) {
    auto user_id = requireUserId(req, res);
    if (!user_id) {
        return;
    }

    auto config_id = parseId(req);
    if (!config_id) {
        res.status = 400;
        res.set_content(jsonError("invalid config id"), "application/json");
        return;
    }

    try {
        auto config = config_service_.getConfigById(*config_id);
        if (!config || config->user_id != *user_id) {
            res.status = 404;
            res.set_content(jsonError("config not found"), "application/json");
            return;
        }

        std::string error;
        auto parsed = parseTrdpXmlConfig(config->xml_content, &error);
        if (!parsed) {
            res.status = 422;
            res.set_content(jsonError(error.empty() ? "Failed to parse TRDP XML" : error), "application/json");
            return;
        }

        TrdpPlanBuilder builder;
        auto plan = builder.buildPlan(*parsed);
        std::string payload = serializePlanSections(plan);
        res.status = 200;
        res.set_content(payload, "application/json");
    } catch (const std::exception &ex) {
        res.status = 500;
        res.set_content(jsonError(ex.what()), "application/json");
    }
}

std::optional<long long> ConfigService::requireUserId(const httplib::Request &req, httplib::Response &res) {
    auto user = auth_manager_.userFromRequest(req);
    if (!user) {
        res.status = 401;
        res.set_content(jsonError("authentication required"), "application/json");
        return std::nullopt;
    }
    return user->id;
}

std::optional<std::string> ConfigService::extractJsonField(const std::string &body, const std::string &field_name) {
    const std::string needle = "\"" + field_name + "\"";
    auto key_pos = body.find(needle);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }

    auto colon_pos = body.find(':', key_pos + needle.size());
    if (colon_pos == std::string::npos) {
        return std::nullopt;
    }

    size_t cursor = colon_pos + 1;
    while (cursor < body.size() && std::isspace(static_cast<unsigned char>(body[cursor]))) {
        ++cursor;
    }
    if (cursor >= body.size() || body[cursor] != '"') {
        return std::nullopt;
    }

    return parseJsonStringToken(body, cursor);
}

std::string ConfigService::escapeJson(const std::string &value) {
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

std::string ConfigService::jsonError(const std::string &message) {
    return std::string{"{\"error\":\""} + escapeJson(message) + "\"}";
}

std::string ConfigService::serializeConfigMetadata(const TrdpConfig &config) {
    return "{\"id\":" + std::to_string(config.id) + ",\"name\":\"" + escapeJson(config.name) +
           "\",\"validation_status\":\"" + escapeJson(config.validation_status) +
           "\",\"created_at\":\"" + escapeJson(config.created_at) + "\"}";
}

std::string ConfigService::serializeConfigWithXml(const TrdpConfig &config) {
    return "{\"id\":" + std::to_string(config.id) + ",\"user_id\":" + std::to_string(config.user_id) +
           ",\"name\":\"" + escapeJson(config.name) +
           "\",\"xml\":\"" + escapeJson(config.xml_content) +
           "\",\"validation_status\":\"" + escapeJson(config.validation_status) +
           "\",\"created_at\":\"" + escapeJson(config.created_at) + "\"}";
}

std::string ConfigService::serializePlanSections(const std::vector<TrdpPlanSection> &sections) {
    std::string json = "{\"plan\":[";
    for (std::size_t i = 0; i < sections.size(); ++i) {
        const auto &section = sections[i];
        if (i != 0) {
            json += ",";
        }
        json += "{\"name\":\"" + escapeJson(section.name) + "\",\"steps\":[";
        for (std::size_t j = 0; j < section.steps.size(); ++j) {
            const auto &step = section.steps[j];
            if (j != 0) {
                json += ",";
            }
            json += "{\"title\":\"" + escapeJson(step.title) + "\",\"description\":\"" +
                    escapeJson(step.description) + "\",\"api_calls\":[";
            for (std::size_t k = 0; k < step.api_calls.size(); ++k) {
                if (k != 0) {
                    json += ",";
                }
                json += "\"" + escapeJson(step.api_calls[k]) + "\"";
            }
            json += "]}";
        }
        json += "]}";
    }
    json += "],\"sections\":" + std::to_string(sections.size()) + "}";
    return json;
}

}  // namespace trdp::config

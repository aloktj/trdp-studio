#include "trdp/ConfigService.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "auth/AuthManager.hpp"
#include "httplib.h"
#include "trdp/PlanBuilder.hpp"
#include "trdp/TrdpConfigService.hpp"
#include "trdp/TrdpXmlParser.hpp"

namespace trdp::config {
namespace {
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

ConfigService::ConfigService(auth::AuthManager &auth_manager, TrdpConfigService &config_service)
    : auth_manager_(auth_manager), config_service_(config_service) {}

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
        res.status = 200;
        res.set_content("{\"status\":\"activated\",\"config_id\":" + std::to_string(*config_id) + "}",
                        "application/json");
    } catch (const std::exception &ex) {
        res.status = 500;
        res.set_content(jsonError(ex.what()), "application/json");
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

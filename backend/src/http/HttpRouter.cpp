#include "http/HttpRouter.hpp"

#include <optional>
#include <string>
#include <vector>

#include "auth/AuthManager.hpp"
#include "auth/AuthService.hpp"
#include "http/JsonUtils.hpp"
#include "httplib.h"
#include "network/NetworkConfigService.hpp"
#include "trdp/ConfigService.hpp"
#include "trdp/TrdpEngine.hpp"
#include "util/LogService.hpp"

namespace trdp::http {

namespace {

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

std::string serializeStringArray(const std::vector<std::string> &values) {
    std::string payload = "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            payload += ",";
        }
        payload += "\"" + json::escape(values[i]) + "\"";
    }
    payload += "]";
    return payload;
}

std::string serializeNetworkConfig(const network::NetworkConfig &config) {
    return std::string{"{"} +
           "\"interface_name\":\"" + json::escape(config.interface_name) + "\"," +
           "\"local_ip\":\"" + json::escape(config.local_ip) + "\"," +
           "\"multicast_groups\":" + serializeStringArray(config.multicast_groups) + "," +
           "\"pd_port\":" + std::to_string(config.pd_port) + "," +
           "\"md_port\":" + std::to_string(config.md_port) + "}";
}

std::optional<stack::PdMessage> findPdById(const std::vector<stack::PdMessage> &messages, int id) {
    for (const auto &msg : messages) {
        if (msg.id == id) {
            return msg;
        }
    }
    return std::nullopt;
}

int queryInt(const httplib::Request &req, const std::string &name, int default_value) {
    if (!req.has_param(name)) {
        return default_value;
    }
    try {
        return std::stoi(req.get_param_value(name));
    } catch (...) {
        return default_value;
    }
}

std::optional<std::string> queryString(const httplib::Request &req, const std::string &name) {
    if (!req.has_param(name)) {
        return std::nullopt;
    }
    auto value = req.get_param_value(name);
    if (value.empty()) {
        return std::nullopt;
    }
    return value;
}

}  // namespace

HttpRouter::HttpRouter(auth::AuthManager &auth_manager, auth::AuthService &auth_service,
                       config::ConfigService &config_service, network::NetworkConfigService &network_config_service,
                       stack::TrdpEngine &trdp_engine, util::LogService &log_service)
    : auth_manager_(auth_manager),
      auth_service_(auth_service),
      config_service_(config_service),
      network_config_service_(network_config_service),
      trdp_engine_(trdp_engine),
      log_service_(log_service) {}

void HttpRouter::registerRoutes(httplib::Server &server) {
    registerHealthEndpoint(server);
    auth_manager_.registerRoutes(server);
    config_service_.registerRoutes(server);
    registerNetworkConfigEndpoints(server);
    registerTrdpEngineEndpoints(server);
    registerAccountEndpoints(server);
    registerLogEndpoints(server);
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
            res.set_content(json::error("authentication required"), "application/json");
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
            res.set_content(json::error(ex.what()), "application/json");
        }
    });

    server.Post("/api/network/config", [this](const httplib::Request &req, httplib::Response &res) {
        auto user = auth_manager_.userFromRequest(req);
        if (!user) {
            res.status = 401;
            res.set_content(json::error("authentication required"), "application/json");
            return;
        }

        auto interface_name = json::stringField(req.body, "interface_name");
        auto local_ip = json::stringField(req.body, "local_ip");
        auto multicast_groups = json::stringArrayField(req.body, "multicast_groups");
        auto pd_port = json::intField(req.body, "pd_port");
        auto md_port = json::intField(req.body, "md_port");

        if (!interface_name || !local_ip || !multicast_groups || !pd_port || !md_port) {
            res.status = 400;
            res.set_content(json::error(
                                "interface_name, local_ip, multicast_groups, pd_port, and md_port are required"),
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
            res.set_content(json::error(ex.what()), "application/json");
        }
    });
}

void HttpRouter::registerTrdpEngineEndpoints(httplib::Server &server) {
    server.Get("/api/pd/outgoing", [this](const httplib::Request &req, httplib::Response &res) {
        auto user = auth_manager_.userFromRequest(req);
        if (!user) {
            res.status = 401;
            res.set_content(json::error("authentication required"), "application/json");
            return;
        }

        auto messages = trdp_engine_.listOutgoingPd();
        res.status = 200;
        res.set_content(json::pdListJson(messages, true), "application/json");
    });

    server.Get("/api/pd/incoming", [this](const httplib::Request &req, httplib::Response &res) {
        auto user = auth_manager_.userFromRequest(req);
        if (!user) {
            res.status = 401;
            res.set_content(json::error("authentication required"), "application/json");
            return;
        }

        auto messages = trdp_engine_.listIncomingPd();
        res.status = 200;
        res.set_content(json::pdListJson(messages, false), "application/json");
    });

    server.Get(R"(/api/pd/outgoing/(\d+))", [this](const httplib::Request &req, httplib::Response &res) {
        auto user = auth_manager_.userFromRequest(req);
        if (!user) {
            res.status = 401;
            res.set_content(json::error("authentication required"), "application/json");
            return;
        }

        auto msg_id = extractPathId(req);
        if (!msg_id) {
            res.status = 400;
            res.set_content(json::error("invalid PD message id"), "application/json");
            return;
        }
        auto messages = trdp_engine_.listOutgoingPd();
        auto message = findPdById(messages, *msg_id);
        if (!message) {
            res.status = 404;
            res.set_content(json::error("PD message not found"), "application/json");
            return;
        }
        res.status = 200;
        res.set_content(json::pdDetailJson(*message), "application/json");
    });

    server.Post(R"(/api/pd/outgoing/(\d+)/payload)", [this](const httplib::Request &req, httplib::Response &res) {
        auto user = auth_manager_.userFromRequest(req);
        if (!user) {
            res.status = 401;
            res.set_content(json::error("authentication required"), "application/json");
            return;
        }

        auto msg_id = extractPathId(req);
        if (!msg_id) {
            res.status = 400;
            res.set_content(json::error("invalid PD message id"), "application/json");
            return;
        }

        auto payload_hex = json::stringField(req.body, "payload_hex");
        if (!payload_hex) {
            res.status = 400;
            res.set_content(json::error("payload_hex is required"), "application/json");
            return;
        }
        auto payload_bytes = json::hexToBlob(*payload_hex);
        if (!payload_bytes) {
            res.status = 400;
            res.set_content(json::error("payload_hex must be an even-length hex string"), "application/json");
            return;
        }

        try {
            trdp_engine_.updateOutgoingPdPayload(*msg_id, *payload_bytes);
            auto messages = trdp_engine_.listOutgoingPd();
            auto message = findPdById(messages, *msg_id);
            if (!message) {
                res.status = 404;
                res.set_content(json::error("PD message not found"), "application/json");
                return;
            }
            res.status = 200;
            res.set_content(json::pdDetailJson(*message), "application/json");
        } catch (const std::exception &ex) {
            std::string message = ex.what();
            if (message.find("not found") != std::string::npos) {
                res.status = 404;
            } else {
                res.status = 500;
            }
            res.set_content(json::error(message), "application/json");
        }
    });

    server.Post("/api/md/send", [this](const httplib::Request &req, httplib::Response &res) {
        auto user = auth_manager_.userFromRequest(req);
        if (!user) {
            res.status = 401;
            res.set_content(json::error("authentication required"), "application/json");
            return;
        }

        auto destination = json::stringField(req.body, "destination_ip");
        auto payload_hex = json::stringField(req.body, "payload_hex");
        auto msg_id = json::intField(req.body, "msg_id");

        if (!destination || !payload_hex || !msg_id) {
            res.status = 400;
            res.set_content(json::error("destination_ip, msg_id, and payload_hex are required"),
                            "application/json");
            return;
        }
        if (*msg_id <= 0) {
            res.status = 400;
            res.set_content(json::error("msg_id must be positive"), "application/json");
            return;
        }

        auto payload_bytes = json::hexToBlob(*payload_hex);
        if (!payload_bytes) {
            res.status = 400;
            res.set_content(json::error("payload_hex must be an even-length hex string"), "application/json");
            return;
        }

        try {
            auto message = trdp_engine_.sendMdMessage(*destination, *msg_id, *payload_bytes);
            res.status = 200;
            res.set_content(json::mdSendResponseJson(message), "application/json");
        } catch (const std::exception &ex) {
            res.status = 500;
            res.set_content(json::error(ex.what()), "application/json");
        }
    });

    server.Get("/api/md/incoming", [this](const httplib::Request &req, httplib::Response &res) {
        auto user = auth_manager_.userFromRequest(req);
        if (!user) {
            res.status = 401;
            res.set_content(json::error("authentication required"), "application/json");
            return;
        }

        auto messages = trdp_engine_.listIncomingMd();
        res.status = 200;
        res.set_content(json::mdIncomingListJson(messages), "application/json");
    });
}

void HttpRouter::registerAccountEndpoints(httplib::Server &server) {
    server.Get("/api/account/me", [this](const httplib::Request &req, httplib::Response &res) {
        auto user = requireUser(req, res);
        if (!user) {
            return;
        }

        try {
            auto fresh_user = auth_service_.getUserById(static_cast<int>(user->id));
            res.status = 200;
            res.set_content("{\"user\":" + json::userJson(fresh_user) + "}", "application/json");
        } catch (const std::exception &ex) {
            res.status = 404;
            res.set_content(json::error(ex.what()), "application/json");
        }
    });

    server.Post("/api/account/me/password", [this](const httplib::Request &req, httplib::Response &res) {
        auto user = requireUser(req, res);
        if (!user) {
            return;
        }

        auto current_password = json::stringField(req.body, "current_password");
        auto new_password = json::stringField(req.body, "new_password");
        if (!current_password || !new_password) {
            res.status = 400;
            res.set_content(json::error("current_password and new_password are required"), "application/json");
            return;
        }
        if (new_password->size() < 8) {
            res.status = 422;
            res.set_content(json::error("new password must be at least 8 characters"), "application/json");
            return;
        }

        auto verified = auth_service_.authenticate(user->username, *current_password);
        if (!verified || verified->id != user->id) {
            res.status = 403;
            res.set_content(json::error("current password is incorrect"), "application/json");
            return;
        }

        if (!auth_service_.changePassword(static_cast<int>(user->id), *new_password)) {
            res.status = 500;
            res.set_content(json::error("failed to update password"), "application/json");
            return;
        }

        log_service_.appendAppLog("INFO", "User " + user->username + " changed their password");
        res.status = 200;
        res.set_content("{\"status\":\"password_updated\"}", "application/json");
    });

    server.Get("/api/account/users", [this](const httplib::Request &req, httplib::Response &res) {
        auto user = requireUser(req, res);
        if (!user) {
            return;
        }
        if (!ensureAdmin(*user, res)) {
            return;
        }

        try {
            auto users = auth_service_.listAllUsers();
            res.status = 200;
            res.set_content("{\"users\":" + json::userListJson(users) + "}", "application/json");
        } catch (const std::exception &ex) {
            res.status = 500;
            res.set_content(json::error(ex.what()), "application/json");
        }
    });

    server.Post(R"(/api/account/users/(\d+)/reset_password)", [this](const httplib::Request &req, httplib::Response &res) {
        auto user = requireUser(req, res);
        if (!user) {
            return;
        }
        if (!ensureAdmin(*user, res)) {
            return;
        }

        auto target_id = extractPathId(req);
        if (!target_id) {
            res.status = 400;
            res.set_content(json::error("invalid user id"), "application/json");
            return;
        }
        if (*target_id <= 0) {
            res.status = 400;
            res.set_content(json::error("user id must be positive"), "application/json");
            return;
        }

        auto new_password = json::stringField(req.body, "new_password");
        if (!new_password) {
            res.status = 400;
            res.set_content(json::error("new_password is required"), "application/json");
            return;
        }
        if (new_password->size() < 8) {
            res.status = 422;
            res.set_content(json::error("new password must be at least 8 characters"), "application/json");
            return;
        }

        try {
            auto target_user = auth_service_.getUserById(*target_id);
            if (!auth_service_.resetPasswordForUser(*target_id, *new_password)) {
                res.status = 500;
                res.set_content(json::error("failed to reset password"), "application/json");
                return;
            }
            log_service_.appendAppLog("WARN", "Admin " + user->username + " reset password for user " + target_user.username);
            res.status = 200;
            res.set_content("{\"status\":\"password_reset\"}", "application/json");
        } catch (const std::exception &ex) {
            std::string message = ex.what();
            if (message.find("not found") != std::string::npos) {
                res.status = 404;
            } else {
                res.status = 500;
            }
            res.set_content(json::error(message), "application/json");
        }
    });
}

void HttpRouter::registerLogEndpoints(httplib::Server &server) {
    server.Get("/api/logs/trdp", [this](const httplib::Request &req, httplib::Response &res) {
        auto user = auth_manager_.userFromRequest(req);
        if (!user) {
            res.status = 401;
            res.set_content(json::error("authentication required"), "application/json");
            return;
        }

        int limit = queryInt(req, "limit", 100);
        int offset = queryInt(req, "offset", 0);
        if (limit <= 0) {
            limit = 1;
        }
        if (limit > 500) {
            limit = 500;
        }
        if (offset < 0) {
            offset = 0;
        }

        try {
            auto logs = log_service_.getTrdpLogs(limit, offset, queryString(req, "type"),
                                                 queryString(req, "direction"));
            res.status = 200;
            res.set_content(json::trdpLogListJson(logs), "application/json");
        } catch (const std::exception &ex) {
            res.status = 500;
            res.set_content(json::error(ex.what()), "application/json");
        }
    });

    server.Get("/api/logs/app", [this](const httplib::Request &req, httplib::Response &res) {
        auto user = auth_manager_.userFromRequest(req);
        if (!user) {
            res.status = 401;
            res.set_content(json::error("authentication required"), "application/json");
            return;
        }

        int limit = queryInt(req, "limit", 100);
        int offset = queryInt(req, "offset", 0);
        if (limit <= 0) {
            limit = 1;
        }
        if (limit > 500) {
            limit = 500;
        }
        if (offset < 0) {
            offset = 0;
        }

        try {
            auto logs = log_service_.getAppLogs(limit, offset, queryString(req, "level"));
            res.status = 200;
            res.set_content(json::appLogListJson(logs), "application/json");
        } catch (const std::exception &ex) {
            res.status = 500;
            res.set_content(json::error(ex.what()), "application/json");
        }
    });
}

std::optional<auth::User> HttpRouter::requireUser(const httplib::Request &req, httplib::Response &res) {
    auto user = auth_manager_.userFromRequest(req);
    if (!user) {
        res.status = 401;
        res.set_content(json::error("authentication required"), "application/json");
    }
    return user;
}

bool HttpRouter::ensureAdmin(const auth::User &user, httplib::Response &res) {
    if (user.role != "admin") {
        res.status = 403;
        res.set_content(json::error("admin privileges required"), "application/json");
        return false;
    }
    return true;
}

}  // namespace trdp::http

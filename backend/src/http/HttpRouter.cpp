#include "http/HttpRouter.hpp"

#include <optional>
#include <string>
#include <vector>

#include "auth/AuthManager.hpp"
#include "http/JsonUtils.hpp"
#include "httplib.h"
#include "network/NetworkConfigService.hpp"
#include "trdp/ConfigService.hpp"
#include "trdp/TrdpEngine.hpp"

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
        auto payload_bytes = json::parseHex(*payload_hex);
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

        auto payload_bytes = json::parseHex(*payload_hex);
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

}  // namespace trdp::http

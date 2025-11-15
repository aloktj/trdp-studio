#pragma once

#include <optional>
#include <string>
#include <vector>

namespace httplib {
class Server;
class Request;
class Response;
}

namespace trdp::auth {
class AuthManager;
}

namespace trdp::config {
class TrdpConfigService;
struct TrdpConfig;
struct TrdpPlanSection;
}

namespace trdp::network {
class NetworkConfigService;
}

namespace trdp::stack {
class TrdpEngine;
}

namespace trdp::config {

// ConfigService exposes REST APIs for TRDP XML configuration management.
class ConfigService {
public:
    ConfigService(auth::AuthManager &auth_manager, TrdpConfigService &config_service,
                  network::NetworkConfigService &network_config_service, stack::TrdpEngine &trdp_engine);

    void registerRoutes(httplib::Server &server);

    // Attempts to load the active TRDP configuration into the engine if all
    // prerequisites are satisfied.
    bool ensureTrdpEngineLoaded();

private:
    void handleListConfigs(const httplib::Request &req, httplib::Response &res);
    void handleCreateConfig(const httplib::Request &req, httplib::Response &res);
    void handleGetConfig(const httplib::Request &req, httplib::Response &res);
    void handleActivateConfig(const httplib::Request &req, httplib::Response &res);
    void handlePlanForConfig(const httplib::Request &req, httplib::Response &res);

    std::optional<long long> requireUserId(const httplib::Request &req, httplib::Response &res);
    static std::optional<std::string> extractJsonField(const std::string &body, const std::string &field_name);
    static std::string escapeJson(const std::string &value);
    static std::string jsonError(const std::string &message);
    static std::string serializeConfigMetadata(const TrdpConfig &config);
    static std::string serializeConfigWithXml(const TrdpConfig &config);
    static std::string serializePlanSections(const std::vector<TrdpPlanSection> &sections);
    bool loadConfigIntoEngine(const TrdpConfig &config);

    auth::AuthManager &auth_manager_;
    TrdpConfigService &config_service_;
    network::NetworkConfigService &network_config_service_;
    stack::TrdpEngine &trdp_engine_;
};

}  // namespace trdp::config

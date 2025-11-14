#pragma once

#include <optional>
#include <string>

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

// ConfigService exposes REST APIs for TRDP XML configuration management.
class ConfigService {
public:
    ConfigService(auth::AuthManager &auth_manager, TrdpConfigService &config_service);

    void registerRoutes(httplib::Server &server);

private:
    void handleListConfigs(const httplib::Request &req, httplib::Response &res);
    void handleCreateConfig(const httplib::Request &req, httplib::Response &res);
    void handleGetConfig(const httplib::Request &req, httplib::Response &res);
    void handleActivateConfig(const httplib::Request &req, httplib::Response &res);

    std::optional<long long> requireUserId(const httplib::Request &req, httplib::Response &res);
    static std::optional<std::string> extractJsonField(const std::string &body, const std::string &field_name);
    static std::string escapeJson(const std::string &value);
    static std::string jsonError(const std::string &message);
    static std::string serializeConfigMetadata(const TrdpConfig &config);
    static std::string serializeConfigWithXml(const TrdpConfig &config);

    auth::AuthManager &auth_manager_;
    TrdpConfigService &config_service_;
};

}  // namespace trdp::config

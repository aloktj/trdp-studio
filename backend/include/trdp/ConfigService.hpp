#pragma once

#include <optional>
#include <string>

namespace httplib {
class Server;
class Request;
class Response;
}

namespace trdp::db {
class Database;
}

namespace trdp::auth {
class AuthManager;
}

namespace trdp::config {

// ConfigService provides REST APIs for managing TRDP XML configurations.
class ConfigService {
public:
    ConfigService(db::Database &database, auth::AuthManager &auth_manager);

    void registerRoutes(httplib::Server &server);

private:
    void handleListConfigs(const httplib::Request &req, httplib::Response &res);
    void handleCreateConfig(const httplib::Request &req, httplib::Response &res);
    void handleGetConfig(const httplib::Request &req, httplib::Response &res);
    void handleActivateConfig(const httplib::Request &req, httplib::Response &res);

    std::optional<long long> requireUserId(const httplib::Request &req, httplib::Response &res);
    std::optional<long long> userIdForUsername(const std::string &username);
    bool configBelongsToUser(long long config_id, long long user_id);
    static std::optional<std::string> extractJsonField(const std::string &body, const std::string &field_name);
    static std::string escapeJson(const std::string &value);
    static std::string jsonError(const std::string &message);
    static std::string runValidationStub(const std::string &xml_content);

    db::Database &database_;
    auth::AuthManager &auth_manager_;
};

}  // namespace trdp::config

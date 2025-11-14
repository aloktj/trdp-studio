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
class AuthService;
struct User;
}

namespace trdp::config {
class ConfigService;
}

namespace trdp::network {
class NetworkConfigService;
}

namespace trdp::stack {
class TrdpEngine;
}

namespace trdp::util {
class LogService;
}

namespace trdp::http {

// HttpRouter wires up all REST endpoints in a single place to keep the
// main entry point minimal.
class HttpRouter {
public:
    HttpRouter(auth::AuthManager &auth_manager, auth::AuthService &auth_service,
               config::ConfigService &config_service, network::NetworkConfigService &network_config_service,
               stack::TrdpEngine &trdp_engine,
               util::LogService &log_service);

    void registerRoutes(httplib::Server &server);

private:
    void registerHealthEndpoint(httplib::Server &server);
    void registerNetworkConfigEndpoints(httplib::Server &server);
    void registerTrdpEngineEndpoints(httplib::Server &server);
    void registerLogEndpoints(httplib::Server &server);
    void registerAccountEndpoints(httplib::Server &server);
    void registerFrontendEndpoints(httplib::Server &server);

    bool serveFrontendAsset(const httplib::Request &req, httplib::Response &res) const;
    void respondFrontendMissing(httplib::Response &res) const;
    std::string locateFrontendRoot() const;
    static std::string detectMimeType(const std::string &extension);
    static bool isApiRequest(const std::string &path);

    std::optional<auth::User> requireUser(const httplib::Request &req, httplib::Response &res);
    bool ensureAdmin(const auth::User &user, httplib::Response &res);

    auth::AuthManager &auth_manager_;
    auth::AuthService &auth_service_;
    config::ConfigService &config_service_;
    network::NetworkConfigService &network_config_service_;
    stack::TrdpEngine &trdp_engine_;
    util::LogService &log_service_;

    bool frontend_available_{false};
    std::string frontend_root_;
};

}  // namespace trdp::http

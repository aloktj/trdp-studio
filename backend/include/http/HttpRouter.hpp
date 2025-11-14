#pragma once

namespace httplib {
class Server;
}

namespace trdp::auth {
class AuthManager;
}

namespace trdp::config {
class ConfigService;
}

namespace trdp::http {

// HttpRouter wires up all REST endpoints in a single place to keep the
// main entry point minimal.
class HttpRouter {
public:
    HttpRouter(auth::AuthManager &auth_manager, config::ConfigService &config_service);

    void registerRoutes(httplib::Server &server);

private:
    void registerHealthEndpoint(httplib::Server &server);

    auth::AuthManager &auth_manager_;
    config::ConfigService &config_service_;
};

}  // namespace trdp::http

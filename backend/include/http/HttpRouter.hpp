#pragma once

namespace httplib {
class Server;
}

namespace trdp::auth {
class AuthManager;
}

namespace trdp::http {

// HttpRouter wires up all REST endpoints in a single place to keep the
// main entry point minimal.
class HttpRouter {
public:
    explicit HttpRouter(auth::AuthManager &auth_manager);

    void registerRoutes(httplib::Server &server);

private:
    void registerHealthEndpoint(httplib::Server &server);

    auth::AuthManager &auth_manager_;
};

}  // namespace trdp::http

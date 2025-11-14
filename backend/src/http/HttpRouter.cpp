#include "http/HttpRouter.hpp"

#include "auth/AuthManager.hpp"
#include "httplib.h"

namespace trdp::http {

HttpRouter::HttpRouter(auth::AuthManager &auth_manager) : auth_manager_(auth_manager) {}

void HttpRouter::registerRoutes(httplib::Server &server) {
    registerHealthEndpoint(server);
    auth_manager_.registerRoutes(server);
}

void HttpRouter::registerHealthEndpoint(httplib::Server &server) {
    server.Get("/health", [](const httplib::Request &, httplib::Response &res) {
        res.set_content("{\"status\":\"OK\"}", "application/json");
    });
}

}  // namespace trdp::http

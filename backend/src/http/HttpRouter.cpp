#include "http/HttpRouter.hpp"

#include "auth/AuthManager.hpp"
#include "httplib.h"
#include "trdp/ConfigService.hpp"

namespace trdp::http {

HttpRouter::HttpRouter(auth::AuthManager &auth_manager, config::ConfigService &config_service)
    : auth_manager_(auth_manager), config_service_(config_service) {}

void HttpRouter::registerRoutes(httplib::Server &server) {
    registerHealthEndpoint(server);
    auth_manager_.registerRoutes(server);
    config_service_.registerRoutes(server);
}

void HttpRouter::registerHealthEndpoint(httplib::Server &server) {
    server.Get("/health", [](const httplib::Request &, httplib::Response &res) {
        res.set_content("{\"status\":\"OK\"}", "application/json");
    });
}

}  // namespace trdp::http

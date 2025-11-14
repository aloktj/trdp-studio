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
    HttpRouter(auth::AuthManager &auth_manager, config::ConfigService &config_service,
               network::NetworkConfigService &network_config_service, stack::TrdpEngine &trdp_engine,
               util::LogService &log_service);

    void registerRoutes(httplib::Server &server);

private:
    void registerHealthEndpoint(httplib::Server &server);
    void registerNetworkConfigEndpoints(httplib::Server &server);
    void registerTrdpEngineEndpoints(httplib::Server &server);
    void registerLogEndpoints(httplib::Server &server);

    auth::AuthManager &auth_manager_;
    config::ConfigService &config_service_;
    network::NetworkConfigService &network_config_service_;
    stack::TrdpEngine &trdp_engine_;
    util::LogService &log_service_;
};

}  // namespace trdp::http

#include <iostream>
#include <string>

#include "auth/AuthManager.hpp"
#include "auth/AuthService.hpp"
#include "db/Database.hpp"
#include "http/HttpRouter.hpp"
#include "httplib.h"
#include "trdp/ConfigService.hpp"

int main() {
    try {
        trdp::db::Database database{"trdp_studio.db"};
        trdp::auth::AuthService auth_service{database};
        auth_service.ensureDefaultUsers();
        trdp::auth::AuthManager auth_manager{auth_service};
        trdp::config::ConfigService config_service{database, auth_manager};
        trdp::http::HttpRouter router{auth_manager, config_service};

        httplib::Server server;
        router.registerRoutes(server);

        std::cout << "TRDP backend listening on http://0.0.0.0:8080" << std::endl;

        if (!server.listen("0.0.0.0", 8080)) {
            std::cerr << "Failed to start HTTP server." << std::endl;
            return 1;
        }
    } catch (const std::exception &ex) {
        std::cerr << "Failed to initialize backend: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}

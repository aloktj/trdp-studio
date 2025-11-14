#include <iostream>
#include <string>

#include "db/Database.hpp"
#include "httplib.h"

int main() {
    try {
        trdp::db::Database database{"trdp_studio.db"};
        (void)database; // suppress unused warning until the database is used elsewhere
    } catch (const std::exception &ex) {
        std::cerr << "Failed to initialize database: " << ex.what() << std::endl;
        return 1;
    }

    httplib::Server server;

    server.Get("/health", [](const httplib::Request &, httplib::Response &res) {
        res.set_content("{\"status\":\"OK\"}", "application/json");
    });

    std::cout << "TRDP backend listening on http://0.0.0.0:8080" << std::endl;

    if (!server.listen("0.0.0.0", 8080)) {
        std::cerr << "Failed to start HTTP server." << std::endl;
        return 1;
    }

    return 0;
}

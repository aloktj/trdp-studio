#include "db/Database.hpp"

#include <stdexcept>
#include <vector>

namespace trdp::db {

Database::Database(const std::string &db_path) : db_path_(db_path) {
    if (sqlite3_open(db_path_.c_str(), &db_) != SQLITE_OK) {
        throw std::runtime_error{"Unable to open SQLite database at " + db_path_};
    }

    initializeSchema();
}

Database::~Database() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void Database::initializeSchema() {
    const std::vector<const char *> statements = {
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "username TEXT UNIQUE NOT NULL,"
        "password_hash TEXT NOT NULL,"
        "role TEXT NOT NULL,"
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP);",
        "CREATE TABLE IF NOT EXISTS xml_configs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "user_id INTEGER NOT NULL,"
        "name TEXT NOT NULL,"
        "xml_content TEXT NOT NULL,"
        "validation_status TEXT,"
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "FOREIGN KEY(user_id) REFERENCES users(id));",
        "CREATE TABLE IF NOT EXISTS active_config ("
        "id INTEGER PRIMARY KEY CHECK(id = 1),"
        "xml_config_id INTEGER,"
        "FOREIGN KEY(xml_config_id) REFERENCES xml_configs(id));",
        "CREATE TABLE IF NOT EXISTS network_config ("
        "id INTEGER PRIMARY KEY CHECK(id = 1),"
        "interface_name TEXT,"
        "local_ip TEXT,"
        "multicast_groups TEXT,"
        "pd_port INTEGER,"
        "md_port INTEGER,"
        "updated_at DATETIME DEFAULT CURRENT_TIMESTAMP);",
        "CREATE TABLE IF NOT EXISTS trdp_logs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "direction TEXT NOT NULL,"
        "type TEXT NOT NULL,"
        "msg_id INTEGER,"
        "src_ip TEXT,"
        "dst_ip TEXT,"
        "payload BLOB,"
        "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);",
        "CREATE TABLE IF NOT EXISTS app_logs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "level TEXT NOT NULL,"
        "message TEXT NOT NULL,"
        "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);"
    };

    char *err_msg = nullptr;
    for (const auto *statement : statements) {
        if (sqlite3_exec(db_, statement, nullptr, nullptr, &err_msg) != SQLITE_OK) {
            std::string error = err_msg ? err_msg : "Unknown error";
            sqlite3_free(err_msg);
            throw std::runtime_error{"Failed to initialize database schema: " + error};
        }
    }
}

}  // namespace trdp::db

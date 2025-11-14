#pragma once

#include <sqlite3.h>

#include <string>

namespace trdp::db {

class Database {
public:
    explicit Database(const std::string &db_path);
    ~Database();

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    sqlite3 *handle() const noexcept { return db_; }

private:
    void initializeSchema();

    std::string db_path_;
    sqlite3 *db_ {nullptr};
};

}  // namespace trdp::db

#include "auth/AuthService.hpp"

#include <sqlite3.h>

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "auth/PasswordHasher.hpp"
#include "db/Database.hpp"

namespace trdp::auth {
namespace {
struct UserRow {
    User user;
    std::string password_hash;
};

std::optional<UserRow> fetchUserRow(sqlite3 *db, const std::string &username) {
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "SELECT id, username, password_hash, role, created_at FROM users WHERE username = ? LIMIT 1;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return std::nullopt;
    }

    UserRow row;
    row.user.id = sqlite3_column_int64(stmt, 0);
    const unsigned char *username_text = sqlite3_column_text(stmt, 1);
    const unsigned char *hash_text = sqlite3_column_text(stmt, 2);
    const unsigned char *role_text = sqlite3_column_text(stmt, 3);
    const unsigned char *created_text = sqlite3_column_text(stmt, 4);

    if (username_text != nullptr) {
        row.user.username = reinterpret_cast<const char *>(username_text);
    }
    if (role_text != nullptr) {
        row.user.role = reinterpret_cast<const char *>(role_text);
    }
    if (created_text != nullptr) {
        row.user.created_at = reinterpret_cast<const char *>(created_text);
    }
    if (hash_text != nullptr) {
        row.password_hash = reinterpret_cast<const char *>(hash_text);
    }

    sqlite3_finalize(stmt);
    return row;
}

std::optional<UserRow> fetchUserRowById(sqlite3 *db, long long user_id) {
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "SELECT id, username, password_hash, role, created_at FROM users WHERE id = ? LIMIT 1;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_int64(stmt, 1, user_id);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return std::nullopt;
    }

    UserRow row;
    row.user.id = sqlite3_column_int64(stmt, 0);
    const unsigned char *username_text = sqlite3_column_text(stmt, 1);
    const unsigned char *hash_text = sqlite3_column_text(stmt, 2);
    const unsigned char *role_text = sqlite3_column_text(stmt, 3);
    const unsigned char *created_text = sqlite3_column_text(stmt, 4);

    if (username_text != nullptr) {
        row.user.username = reinterpret_cast<const char *>(username_text);
    }
    if (role_text != nullptr) {
        row.user.role = reinterpret_cast<const char *>(role_text);
    }
    if (created_text != nullptr) {
        row.user.created_at = reinterpret_cast<const char *>(created_text);
    }
    if (hash_text != nullptr) {
        row.password_hash = reinterpret_cast<const char *>(hash_text);
    }

    sqlite3_finalize(stmt);
    return row;
}

bool insertUser(sqlite3 *db, const std::string &username, const std::string &password_hash, const std::string &role) {
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "INSERT INTO users (username, password_hash, role) VALUES (?, ?, ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, password_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, role.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool lookupUserExists(sqlite3 *db, const std::string &username) {
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "SELECT 1 FROM users WHERE username = ? LIMIT 1;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW;
}

bool updatePasswordHash(sqlite3 *db, long long user_id, const std::string &password_hash) {
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "UPDATE users SET password_hash = ? WHERE id = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, password_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, user_id);

    int rc = sqlite3_step(stmt);
    bool updated = rc == SQLITE_DONE && sqlite3_changes(db) > 0;
    sqlite3_finalize(stmt);
    return updated;
}

std::vector<User> fetchAllUsers(sqlite3 *db) {
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "SELECT id, username, role, created_at FROM users ORDER BY id ASC;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error{"Failed to load users"};
    }

    std::vector<User> users;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        User user;
        user.id = sqlite3_column_int64(stmt, 0);
        const unsigned char *username_text = sqlite3_column_text(stmt, 1);
        const unsigned char *role_text = sqlite3_column_text(stmt, 2);
        const unsigned char *created_text = sqlite3_column_text(stmt, 3);
        if (username_text != nullptr) {
            user.username = reinterpret_cast<const char *>(username_text);
        }
        if (role_text != nullptr) {
            user.role = reinterpret_cast<const char *>(role_text);
        }
        if (created_text != nullptr) {
            user.created_at = reinterpret_cast<const char *>(created_text);
        }
        users.push_back(std::move(user));
    }

    sqlite3_finalize(stmt);
    return users;
}
}  // namespace

AuthService::AuthService(db::Database &database) : database_(database) {}

bool AuthService::registerUser(const std::string &username, const std::string &password, const std::string &role) {
    const std::string password_hash = PasswordHasher::hash(password);
    return insertUser(database_.handle(), username, password_hash, role);
}

std::optional<User> AuthService::authenticate(const std::string &username, const std::string &password) {
    auto row = fetchUserRow(database_.handle(), username);
    if (!row) {
        return std::nullopt;
    }

    if (!PasswordHasher::verify(password, row->password_hash)) {
        return std::nullopt;
    }

    return row->user;
}

void AuthService::ensureDefaultUsers() {
    const struct {
        const char *username;
        const char *password;
        const char *role;
    } defaults[] = {
        {"admin", "admin", "admin"},
        {"dev", "dev", "dev"},
    };

    for (const auto &entry : defaults) {
        if (!userExists(entry.username)) {
            registerUser(entry.username, entry.password, entry.role);
        }
    }
}

bool AuthService::userExists(const std::string &username) const {
    return lookupUserExists(database_.handle(), username);
}

User AuthService::getUserById(int id) {
    auto row = fetchUserRowById(database_.handle(), id);
    if (!row) {
        throw std::runtime_error{"user not found"};
    }
    return row->user;
}

std::optional<User> AuthService::getUserByUsername(const std::string &username) {
    auto row = fetchUserRow(database_.handle(), username);
    if (!row) {
        return std::nullopt;
    }
    return row->user;
}

bool AuthService::changePassword(int user_id, const std::string &new_password) {
    const std::string password_hash = PasswordHasher::hash(new_password);
    return updatePasswordHash(database_.handle(), user_id, password_hash);
}

std::vector<User> AuthService::listAllUsers() {
    return fetchAllUsers(database_.handle());
}

bool AuthService::resetPasswordForUser(int target_user_id, const std::string &new_password) {
    return changePassword(target_user_id, new_password);
}

}  // namespace trdp::auth

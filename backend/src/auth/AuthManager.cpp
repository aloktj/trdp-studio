#include "auth/AuthManager.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <random>
#include <stdexcept>
#include <string>

#include "db/Database.hpp"
#include "httplib.h"

extern "C" {
#include <sqlite3.h>
}

#if __has_include(<bcrypt/BCrypt.hpp>)
#include <bcrypt/BCrypt.hpp>
#define TRDP_HAS_BCRYPT 1
#else
#define TRDP_HAS_BCRYPT 0
#endif

namespace {
std::string hashPassword(const std::string &password) {
#if TRDP_HAS_BCRYPT
    return BCrypt::generateHash(password);
#else
    std::hash<std::string> hasher;
    return std::to_string(hasher(password));
#endif
}

bool verifyPassword(const std::string &password, const std::string &hash) {
#if TRDP_HAS_BCRYPT
    return BCrypt::validatePassword(password, hash);
#else
    std::hash<std::string> hasher;
    return std::to_string(hasher(password)) == hash;
#endif
}

std::string jsonError(const std::string &message) {
    return std::string{"{\"error\":\""} + message + "\"}";
}

bool isValidUsername(const std::string &username) {
    if (username.size() < 3 || username.size() > 48) {
        return false;
    }
    return std::all_of(username.begin(), username.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '_' || ch == '-';
    });
}

bool isValidPassword(const std::string &password) {
    return password.size() >= 8;
}

}  // namespace

namespace trdp::auth {

namespace {
constexpr const char *kSessionCookieName = "trdp_session";
}

AuthManager::AuthManager(db::Database &database) : database_(database) {}

void AuthManager::registerRoutes(httplib::Server &server) {
    server.Post("/api/auth/register", [this](const httplib::Request &req, httplib::Response &res) {
        handleRegister(req, res);
    });

    server.Post("/api/auth/login", [this](const httplib::Request &req, httplib::Response &res) {
        handleLogin(req, res);
    });

    server.Post("/api/auth/logout", [this](const httplib::Request &req, httplib::Response &res) {
        handleLogout(req, res);
    });
}

std::optional<std::string> AuthManager::usernameFromRequest(const httplib::Request &req) {
    auto session_id = sessionIdFromRequest(req);
    if (!session_id) {
        return std::nullopt;
    }

    std::lock_guard<std::mutex> lock(session_mutex_);
    auto it = sessions_.find(*session_id);
    if (it == sessions_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void AuthManager::handleRegister(const httplib::Request &req, httplib::Response &res) {
    auto username = extractJsonField(req.body, "username");
    auto password = extractJsonField(req.body, "password");

    if (!username || !password) {
        res.status = 400;
        res.set_content(jsonError("username and password are required"), "application/json");
        return;
    }

    if (!isValidUsername(*username) || !isValidPassword(*password)) {
        res.status = 422;
        res.set_content(jsonError("invalid username or password"), "application/json");
        return;
    }

    if (usernameExists(*username)) {
        res.status = 409;
        res.set_content(jsonError("username already exists"), "application/json");
        return;
    }

    std::string password_hash = hashPassword(*password);

    if (!insertUser(*username, password_hash)) {
        res.status = 500;
        res.set_content(jsonError("failed to create user"), "application/json");
        return;
    }

    res.status = 201;
    res.set_content("{\"status\":\"registered\"}", "application/json");
}

void AuthManager::handleLogin(const httplib::Request &req, httplib::Response &res) {
    auto username = extractJsonField(req.body, "username");
    auto password = extractJsonField(req.body, "password");

    if (!username || !password) {
        res.status = 400;
        res.set_content(jsonError("username and password are required"), "application/json");
        return;
    }

    auto stored_hash = fetchPasswordHash(*username);
    if (!stored_hash) {
        res.status = 401;
        res.set_content(jsonError("invalid credentials"), "application/json");
        return;
    }

    if (!verifyPassword(*password, *stored_hash)) {
        res.status = 401;
        res.set_content(jsonError("invalid credentials"), "application/json");
        return;
    }

    auto session_id = generateSessionId();
    {
        std::lock_guard<std::mutex> lock(session_mutex_);
        sessions_[session_id] = *username;
    }

    attachSessionCookie(session_id, res);
    res.status = 200;
    res.set_content("{\"status\":\"logged_in\"}", "application/json");
}

void AuthManager::handleLogout(const httplib::Request &req, httplib::Response &res) {
    auto session_id = sessionIdFromRequest(req);
    if (!session_id) {
        clearSessionCookie(res);
        res.status = 200;
        res.set_content("{\"status\":\"logged_out\"}", "application/json");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(session_mutex_);
        sessions_.erase(*session_id);
    }

    clearSessionCookie(res);
    res.status = 200;
    res.set_content("{\"status\":\"logged_out\"}", "application/json");
}

bool AuthManager::usernameExists(const std::string &username) {
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "SELECT 1 FROM users WHERE username = ? LIMIT 1;";
    if (sqlite3_prepare_v2(database_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    bool exists = (rc == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return exists;
}

bool AuthManager::insertUser(const std::string &username, const std::string &password_hash, const std::string &role) {
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "INSERT INTO users (username, password_hash, role) VALUES (?, ?, ?);";
    if (sqlite3_prepare_v2(database_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, password_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, role.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::optional<std::string> AuthManager::fetchPasswordHash(const std::string &username) {
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "SELECT password_hash FROM users WHERE username = ? LIMIT 1;";
    if (sqlite3_prepare_v2(database_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return std::nullopt;
    }

    const unsigned char *text = sqlite3_column_text(stmt, 0);
    std::optional<std::string> hash;
    if (text != nullptr) {
        hash = reinterpret_cast<const char *>(text);
    }
    sqlite3_finalize(stmt);
    return hash;
}

std::optional<std::string> AuthManager::extractJsonField(const std::string &body, const std::string &field_name) {
    const std::string needle = "\"" + field_name + "\"";
    auto key_pos = body.find(needle);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }

    auto colon_pos = body.find(':', key_pos + needle.size());
    if (colon_pos == std::string::npos) {
        return std::nullopt;
    }

    auto start_quote = body.find('"', colon_pos);
    if (start_quote == std::string::npos) {
        return std::nullopt;
    }

    auto end_quote = body.find('"', start_quote + 1);
    if (end_quote == std::string::npos) {
        return std::nullopt;
    }

    return body.substr(start_quote + 1, end_quote - start_quote - 1);
}

std::string AuthManager::generateSessionId() {
    static constexpr char kHexDigits[] = "0123456789abcdef";
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<int> dist(0, 15);

    std::string token;
    token.reserve(64);
    for (size_t i = 0; i < 64; ++i) {
        token.push_back(kHexDigits[dist(gen)]);
    }
    return token;
}

void AuthManager::attachSessionCookie(const std::string &session_id, httplib::Response &res) {
    std::string cookie = std::string{kSessionCookieName} + "=" + session_id + "; Path=/; HttpOnly; SameSite=Lax";
    res.set_header("Set-Cookie", cookie.c_str());
}

void AuthManager::clearSessionCookie(httplib::Response &res) {
    std::string cookie = std::string{kSessionCookieName} + "=deleted; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT; HttpOnly; SameSite=Lax";
    res.set_header("Set-Cookie", cookie.c_str());
}

std::optional<std::string> AuthManager::sessionIdFromRequest(const httplib::Request &req) {
    auto cookie_header = req.get_header_value("Cookie");
    if (cookie_header.empty()) {
        return std::nullopt;
    }

    const std::string needle = std::string{kSessionCookieName} + "=";
    auto pos = cookie_header.find(needle);
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    pos += needle.size();
    auto end = cookie_header.find(';', pos);
    auto session_id = cookie_header.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    return session_id;
}

}  // namespace trdp::auth

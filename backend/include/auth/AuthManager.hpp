#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace httplib {
class Server;
class Request;
class Response;
}  // namespace httplib

namespace trdp::db {
class Database;
}

namespace trdp::auth {

// AuthManager owns the REST handlers for registration, login, and logout.
// It persists users in SQLite and keeps a lightweight in-memory session map
// for issuing HttpOnly cookies.
class AuthManager {
public:
    explicit AuthManager(db::Database &database);

    // Registers /api/auth/* endpoints on the provided HTTP server.
    void registerRoutes(httplib::Server &server);

    // Returns the username associated with the HttpOnly session cookie if
    // present and valid.
    std::optional<std::string> usernameFromRequest(const httplib::Request &req);

private:
    void handleRegister(const httplib::Request &req, httplib::Response &res);
    void handleLogin(const httplib::Request &req, httplib::Response &res);
    void handleLogout(const httplib::Request &req, httplib::Response &res);

    bool usernameExists(const std::string &username);
    bool insertUser(const std::string &username, const std::string &password_hash, const std::string &role = "dev");
    std::optional<std::string> fetchPasswordHash(const std::string &username);

    static std::optional<std::string> extractJsonField(const std::string &body, const std::string &field_name);
    static std::string generateSessionId();
    static void attachSessionCookie(const std::string &session_id, httplib::Response &res);
    static void clearSessionCookie(httplib::Response &res);

    std::optional<std::string> sessionIdFromRequest(const httplib::Request &req);

    db::Database &database_;
    std::mutex session_mutex_;
    std::unordered_map<std::string, std::string> sessions_;
};

}  // namespace trdp::auth

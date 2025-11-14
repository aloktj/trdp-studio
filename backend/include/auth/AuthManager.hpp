#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "auth/User.hpp"

namespace httplib {
class Server;
class Request;
class Response;
}  // namespace httplib

namespace trdp::auth {

class AuthService;

// AuthManager owns the REST handlers for registration, login, and logout.
// It persists users in SQLite through AuthService and keeps an in-memory
// session map for issuing HttpOnly cookies.
class AuthManager {
public:
    explicit AuthManager(AuthService &auth_service);

    // Registers /api/auth/* endpoints on the provided HTTP server.
    void registerRoutes(httplib::Server &server);

    // Returns the authenticated user associated with the HttpOnly session
    // cookie if present and valid.
    std::optional<User> userFromRequest(const httplib::Request &req);

private:
    void handleRegister(const httplib::Request &req, httplib::Response &res);
    void handleLogin(const httplib::Request &req, httplib::Response &res);
    void handleLogout(const httplib::Request &req, httplib::Response &res);

    static std::optional<std::string> extractJsonField(const std::string &body, const std::string &field_name);
    static std::string generateSessionId();
    static void attachSessionCookie(const std::string &session_id, httplib::Response &res);
    static void clearSessionCookie(httplib::Response &res);

    std::optional<std::string> sessionIdFromRequest(const httplib::Request &req);

    AuthService &auth_service_;
    std::mutex session_mutex_;
    std::unordered_map<std::string, User> sessions_;
};

}  // namespace trdp::auth

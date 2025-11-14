#pragma once

#include <optional>
#include <string>

#include "auth/User.hpp"

namespace trdp::db {
class Database;
}

namespace trdp::auth {

class AuthService {
public:
    explicit AuthService(db::Database &database);

    bool registerUser(const std::string &username, const std::string &password, const std::string &role = "dev");
    std::optional<User> authenticate(const std::string &username, const std::string &password);
    void ensureDefaultUsers();
    bool userExists(const std::string &username) const;

private:
    db::Database &database_;
};

}  // namespace trdp::auth

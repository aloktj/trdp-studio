#pragma once

#include <optional>
#include <string>
#include <vector>

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

    User getUserById(int id);
    std::optional<User> getUserByUsername(const std::string &username);
    bool changePassword(int user_id, const std::string &new_password);
    std::vector<User> listAllUsers();
    bool resetPasswordForUser(int target_user_id, const std::string &new_password);

private:
    db::Database &database_;
};

}  // namespace trdp::auth

#pragma once

#include <string>

namespace trdp::auth {

struct User {
    long long id {0};
    std::string username;
    std::string role;
    std::string created_at;
};

}  // namespace trdp::auth

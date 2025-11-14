#pragma once

#include <string>

namespace trdp::auth {

// TODO: Implement full authentication management (user sessions, password hashing, etc.).
class AuthManager {
public:
    AuthManager() = default;
    void initialize();
};

}  // namespace trdp::auth

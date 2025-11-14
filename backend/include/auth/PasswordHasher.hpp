#pragma once

#include <string>

namespace trdp::auth {

class PasswordHasher {
public:
    static std::string hash(const std::string &password);
    static bool verify(const std::string &password, const std::string &hash);
};

}  // namespace trdp::auth

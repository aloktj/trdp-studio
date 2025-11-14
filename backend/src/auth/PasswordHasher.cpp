#include "auth/PasswordHasher.hpp"

#include <functional>

#if __has_include(<bcrypt/BCrypt.hpp>)
#include <bcrypt/BCrypt.hpp>
#define TRDP_HAS_BCRYPT 1
#else
#define TRDP_HAS_BCRYPT 0
#endif

namespace trdp::auth {

std::string PasswordHasher::hash(const std::string &password) {
#if TRDP_HAS_BCRYPT
    return BCrypt::generateHash(password);
#else
    std::hash<std::string> hasher;
    return std::to_string(hasher(password));
#endif
}

bool PasswordHasher::verify(const std::string &password, const std::string &hash) {
#if TRDP_HAS_BCRYPT
    return BCrypt::validatePassword(password, hash);
#else
    std::hash<std::string> hasher;
    return std::to_string(hasher(password)) == hash;
#endif
}

}  // namespace trdp::auth

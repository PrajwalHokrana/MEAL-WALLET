#ifndef MEALWALLET_CRYPTO_HPP
#define MEALWALLET_CRYPTO_HPP

#include <string>

namespace mealwallet {

/**
 * Classroom-grade cryptographic utility:
 * Implements FIPS 180-4 SHA-256 from scratch in standard C++17 (no OpenSSL dependency)
 * so the project builds cleanly anywhere.
 * Passwords are stored as SHA-256(salt + ":" + secret + ":" + password) with a unique
 * per-user 16-byte hex salt.
 */
class Crypto {
public:
    static std::string sha256Hex(const std::string& input);
    static std::string generateSalt(size_t hexLen = 16);
    static std::string generateToken(size_t hexLen = 40);
    static std::string generateTempPassword();
    static std::string hashPassword(const std::string& plainPassword, const std::string& salt);
    static bool verifyPassword(const std::string& plainPassword,
                               const std::string& salt,
                               const std::string& expectedHash);
};

} // namespace mealwallet

#endif // MEALWALLET_CRYPTO_HPP

#ifndef MEALWALLET_CRYPTO_HPP
#define MEALWALLET_CRYPTO_HPP

#include <string>

namespace mealwallet {

class Crypto {
public:
    static std::string generateToken(size_t hexLen = 32);
    static std::string generateTempPassword();
};

} // namespace mealwallet

#endif // MEALWALLET_CRYPTO_HPP

#ifndef MEALWALLET_MONEY_HPP
#define MEALWALLET_MONEY_HPP

#include <cstdint>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>

namespace mealwallet {


class Money {
private:
    int64_t paise_;

public:
    constexpr Money() noexcept : paise_(0) {}
    constexpr explicit Money(int64_t paise) noexcept : paise_(paise) {}

    static constexpr Money fromPaise(int64_t p) noexcept { return Money(p); }
    static constexpr Money fromRupees(int64_t r) noexcept { return Money(r * 100); }

    constexpr int64_t paise() const noexcept { return paise_; }

    // Format as "Rs. 45.00" or "₹45.00"
    std::string toRupeeString(bool withSymbol = true) const {
        std::ostringstream oss;
        int64_t absP = (paise_ < 0) ? -paise_ : paise_;
        if (paise_ < 0) oss << "-";
        if (withSymbol) oss << "Rs. ";
        oss << (absP / 100) << "." << std::setw(2) << std::setfill('0') << (absP % 100);
        return oss.str();
    }

    // Arithmetic operator overloads
    constexpr Money operator+(const Money& rhs) const noexcept {
        return Money(paise_ + rhs.paise_);
    }

    constexpr Money operator-(const Money& rhs) const noexcept {
        return Money(paise_ - rhs.paise_);
    }

    constexpr Money operator-() const noexcept {
        return Money(-paise_);
    }

    Money& operator+=(const Money& rhs) noexcept {
        paise_ += rhs.paise_;
        return *this;
    }

    Money& operator-=(const Money& rhs) noexcept {
        paise_ -= rhs.paise_;
        return *this;
    }

    // Comparison operator overloads
    constexpr bool operator<(const Money& rhs) const noexcept { return paise_ < rhs.paise_; }
    constexpr bool operator>(const Money& rhs) const noexcept { return paise_ > rhs.paise_; }
    constexpr bool operator<=(const Money& rhs) const noexcept { return paise_ <= rhs.paise_; }
    constexpr bool operator>=(const Money& rhs) const noexcept { return paise_ >= rhs.paise_; }
    constexpr bool operator==(const Money& rhs) const noexcept { return paise_ == rhs.paise_; }
    constexpr bool operator!=(const Money& rhs) const noexcept { return paise_ != rhs.paise_; }

    
    friend std::ostream& operator<<(std::ostream& os, const Money& m) {
        os << m.toRupeeString(true);
        return os;
    }
};

} // namespace mealwallet

#endif // MEALWALLET_MONEY_HPP

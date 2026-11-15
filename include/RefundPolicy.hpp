#ifndef MEALWALLET_REFUND_POLICY_HPP
#define MEALWALLET_REFUND_POLICY_HPP

#include "Money.hpp"
#include <memory>
#include <string>

namespace mealwallet {


class RefundPolicy {
public:
    virtual ~RefundPolicy() = default;

    // Pure virtual functions making RefundPolicy an abstract class
    virtual Money compute(Money baseMealPrice) const = 0;
    virtual std::string name() const = 0;
    virtual int percentage() const = 0;
    virtual std::string description() const = 0;
};


class FullRefundPolicy : public RefundPolicy {
public:
    Money compute(Money baseMealPrice) const override {
        return baseMealPrice;
    }
    std::string name() const override { return "FULL"; }
    int percentage() const override { return 100; }
    std::string description() const override {
        return "Full Refund Policy (100% of meal price credited on approved skip)";
    }
};


class PercentRefundPolicy : public RefundPolicy {
private:
    int percent_;

public:
    explicit PercentRefundPolicy(int percent = 80)
        : percent_(percent < 0 ? 0 : (percent > 100 ? 100 : percent)) {}

    Money compute(Money baseMealPrice) const override {
        int64_t creditedPaise = (baseMealPrice.paise() * static_cast<int64_t>(percent_)) / 100LL;
        return Money::fromPaise(creditedPaise);
    }
    std::string name() const override { return "PERCENT"; }
    int percentage() const override { return percent_; }
    std::string description() const override {
        return "Percent Refund Policy (" + std::to_string(percent_) +
               "% of meal price credited on approved skip; " +
               std::to_string(100 - percent_) + "% retained for fixed mess overhead)";
    }
};

} // namespace mealwallet

#endif // MEALWALLET_REFUND_POLICY_HPP

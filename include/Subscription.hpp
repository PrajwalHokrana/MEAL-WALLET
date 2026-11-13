#ifndef MEALWALLET_SUBSCRIPTION_HPP
#define MEALWALLET_SUBSCRIPTION_HPP

#include "DateTime.hpp"
#include "Enums.hpp"
#include "Money.hpp"
#include <string>

namespace mealwallet {

/**
 * [OOP Concept: Composition]
 * A Student HAS-A Subscription representing their active mess cycle, plan ID,
 * validity window, month-end settlement preference (default CARRY_FORWARD),
 * and any queued plan change for the next cycle (since plan changes apply from
 * the next cycle only).
 */
class Subscription {
private:
    std::string id_;
    std::string studentId_;
    std::string planId_;
    PlanVariant planVariant_;
    Date startDate_;
    Date endDate_;
    SettlementChoice settlementChoice_; // Default CARRY_FORWARD if student makes no choice
    std::string nextCyclePlanId_;       // Enforces: "Plan changes apply from the next cycle only"
    Money carryForwardCreditApplied_;
    bool active_;

public:
    Subscription()
        : id_(""),
          studentId_(""),
          planId_("PLAN-VEG"),
          planVariant_(PlanVariant::VEG),
          startDate_(Date(2026, 11, 1)),
          endDate_(Date(2026, 11, 30)),
          settlementChoice_(SettlementChoice::CARRY_FORWARD),
          nextCyclePlanId_(""),
          carryForwardCreditApplied_(Money::fromPaise(0)),
          active_(true) {}

    Subscription(std::string id,
                 std::string studentId,
                 std::string planId,
                 PlanVariant variant,
                 Date startDate,
                 Date endDate,
                 SettlementChoice choice = SettlementChoice::CARRY_FORWARD,
                 std::string nextCyclePlanId = "",
                 Money carryForwardApplied = Money::fromPaise(0),
                 bool active = true)
        : id_(std::move(id)),
          studentId_(std::move(studentId)),
          planId_(std::move(planId)),
          planVariant_(variant),
          startDate_(startDate),
          endDate_(endDate),
          settlementChoice_(choice),
          nextCyclePlanId_(std::move(nextCyclePlanId)),
          carryForwardCreditApplied_(carryForwardApplied),
          active_(active) {}

    const std::string& id() const noexcept { return id_; }
    const std::string& studentId() const noexcept { return studentId_; }
    const std::string& planId() const noexcept { return planId_; }
    PlanVariant planVariant() const noexcept { return planVariant_; }
    const Date& startDate() const noexcept { return startDate_; }
    const Date& endDate() const noexcept { return endDate_; }
    SettlementChoice settlementChoice() const noexcept { return settlementChoice_; }
    const std::string& nextCyclePlanId() const noexcept { return nextCyclePlanId_; }
    Money carryForwardCreditApplied() const noexcept { return carryForwardCreditApplied_; }
    bool active() const noexcept { return active_; }

    void setSettlementChoice(SettlementChoice c) noexcept { settlementChoice_ = c; }

    // Business Rule: Plan changes apply from the next cycle only
    void scheduleNextCyclePlan(const std::string& newPlanId) {
        nextCyclePlanId_ = newPlanId;
    }

    void rollToNextCycle(const Date& newStart, const Date& newEnd,
                         PlanVariant nextVariant, Money carryDiscount) {
        if (!nextCyclePlanId_.empty()) {
            planId_ = nextCyclePlanId_;
            planVariant_ = nextVariant;
            nextCyclePlanId_.clear();
        }
        startDate_ = newStart;
        endDate_ = newEnd;
        carryForwardCreditApplied_ = carryDiscount;
        settlementChoice_ = SettlementChoice::CARRY_FORWARD; // Reset default for new month
        active_ = true;
    }

    bool coversDate(const Date& d) const noexcept {
        return active_ && d >= startDate_ && d <= endDate_;
    }
};

} // namespace mealwallet

#endif // MEALWALLET_SUBSCRIPTION_HPP

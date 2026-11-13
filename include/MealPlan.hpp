#ifndef MEALWALLET_MEAL_PLAN_HPP
#define MEALWALLET_MEAL_PLAN_HPP

#include "Enums.hpp"
#include "Money.hpp"
#include <memory>
#include <string>

namespace mealwallet {

/**
 * [OOP Concept: Abstraction & Runtime Polymorphism]
 * Abstract MealPlan defines pure virtual mealPrice(MealType) and variant().
 * Concrete subclasses (VegPlan, NonVegPlan, PremiumPlan) provide fixed per-meal
 * prices in integer paise. Polymorphic calls through MealPlan* compute refunds
 * and meal-swap price differences.
 */
class MealPlan {
protected:
    std::string id_;
    std::string displayName_;
    Money breakfastPrice_;
    Money lunchPrice_;
    Money dinnerPrice_;

public:
    MealPlan(std::string id, std::string displayName,
             Money breakfast, Money lunch, Money dinner)
        : id_(std::move(id)),
          displayName_(std::move(displayName)),
          breakfastPrice_(breakfast),
          lunchPrice_(lunch),
          dinnerPrice_(dinner) {}

    virtual ~MealPlan() = default;

    const std::string& id() const noexcept { return id_; }
    const std::string& displayName() const noexcept { return displayName_; }

    // Pure virtual functions
    virtual PlanVariant variant() const = 0;
    virtual Money mealPrice(MealType meal) const = 0;
    virtual std::unique_ptr<MealPlan> clone() const = 0;

    Money dailyCost() const {
        return mealPrice(MealType::BREAKFAST) +
               mealPrice(MealType::LUNCH) +
               mealPrice(MealType::DINNER);
    }

    Money monthlyCost30Days() const {
        return Money::fromPaise(dailyCost().paise() * 30LL);
    }

    static std::unique_ptr<MealPlan> create(const std::string& id,
                                            PlanVariant variant,
                                            const std::string& name,
                                            Money b, Money l, Money d);
};

/**
 * [OOP Concept: Inheritance & Polymorphism]
 * Standard Vegetarian Mess Plan (e.g., Breakfast Rs.35, Lunch Rs.55, Dinner Rs.55)
 */
class VegPlan : public MealPlan {
public:
    VegPlan(std::string id = "PLAN-VEG",
            std::string name = "Standard Veg Plan",
            Money b = Money::fromRupees(35),
            Money l = Money::fromRupees(55),
            Money d = Money::fromRupees(55))
        : MealPlan(std::move(id), std::move(name), b, l, d) {}

    PlanVariant variant() const override { return PlanVariant::VEG; }

    Money mealPrice(MealType meal) const override {
        switch (meal) {
            case MealType::BREAKFAST: return breakfastPrice_;
            case MealType::LUNCH:     return lunchPrice_;
            case MealType::DINNER:    return dinnerPrice_;
        }
        return breakfastPrice_;
    }

    std::unique_ptr<MealPlan> clone() const override {
        return std::make_unique<VegPlan>(*this);
    }
};

/**
 * [OOP Concept: Inheritance & Polymorphism]
 * Non-Vegetarian Mess Plan (e.g., Breakfast Rs.45, Lunch Rs.75, Dinner Rs.75)
 */
class NonVegPlan : public MealPlan {
public:
    NonVegPlan(std::string id = "PLAN-NONVEG",
               std::string name = "Protein Non-Veg Plan",
               Money b = Money::fromRupees(45),
               Money l = Money::fromRupees(75),
               Money d = Money::fromRupees(75))
        : MealPlan(std::move(id), std::move(name), b, l, d) {}

    PlanVariant variant() const override { return PlanVariant::NON_VEG; }

    Money mealPrice(MealType meal) const override {
        switch (meal) {
            case MealType::BREAKFAST: return breakfastPrice_;
            case MealType::LUNCH:     return lunchPrice_;
            case MealType::DINNER:    return dinnerPrice_;
        }
        return breakfastPrice_;
    }

    std::unique_ptr<MealPlan> clone() const override {
        return std::make_unique<NonVegPlan>(*this);
    }
};

/**
 * [OOP Concept: Inheritance & Polymorphism]
 * Premium Special Mess Plan (e.g., Breakfast Rs.55, Lunch Rs.90, Dinner Rs.95)
 */
class PremiumPlan : public MealPlan {
public:
    PremiumPlan(std::string id = "PLAN-PREMIUM",
                std::string name = "Executive Premium Plan",
                Money b = Money::fromRupees(55),
                Money l = Money::fromRupees(90),
                Money d = Money::fromRupees(95))
        : MealPlan(std::move(id), std::move(name), b, l, d) {}

    PlanVariant variant() const override { return PlanVariant::PREMIUM; }

    Money mealPrice(MealType meal) const override {
        switch (meal) {
            case MealType::BREAKFAST: return breakfastPrice_;
            case MealType::LUNCH:     return lunchPrice_;
            case MealType::DINNER:    return dinnerPrice_;
        }
        return breakfastPrice_;
    }

    std::unique_ptr<MealPlan> clone() const override {
        return std::make_unique<PremiumPlan>(*this);
    }
};

} // namespace mealwallet

#endif // MEALWALLET_MEAL_PLAN_HPP

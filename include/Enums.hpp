#ifndef MEALWALLET_ENUMS_HPP
#define MEALWALLET_ENUMS_HPP

#include <string>
#include <stdexcept>

namespace mealwallet {

/**
 * [OOP Concept: Scoped Enumerations (enum class)]
 * Prevents implicit integer conversions and namespace pollution across
 * meal types, request states, transaction types, and user roles.
 */
enum class MealType {
    BREAKFAST = 0,
    LUNCH = 1,
    DINNER = 2
};

enum class RequestStatus {
    PENDING = 0,
    APPROVED = 1,
    REJECTED = 2,
    APPEALED = 3,
    CANCELLED = 4
};

enum class TxnType {
    CREDIT = 0,
    DEBIT = 1,
    PAYOUT = 2
};

enum class UserRole {
    STUDENT = 0,
    ADMIN = 1,
    MESS_STAFF = 2
};

enum class PlanVariant {
    VEG = 0,
    NON_VEG = 1,
    PREMIUM = 2
};

enum class SettlementChoice {
    CARRY_FORWARD = 0,
    REFUND = 1
};

// Helper conversions between enum class and string for text file serialization & UI
inline std::string toString(MealType m) {
    switch (m) {
        case MealType::BREAKFAST: return "BREAKFAST";
        case MealType::LUNCH:     return "LUNCH";
        case MealType::DINNER:    return "DINNER";
    }
    return "BREAKFAST";
}

inline MealType mealTypeFromString(const std::string& s) {
    if (s == "BREAKFAST" || s == "breakfast") return MealType::BREAKFAST;
    if (s == "LUNCH" || s == "lunch")         return MealType::LUNCH;
    if (s == "DINNER" || s == "dinner")       return MealType::DINNER;
    throw std::invalid_argument("Invalid MealType: " + s);
}

inline std::string toString(RequestStatus st) {
    switch (st) {
        case RequestStatus::PENDING:   return "PENDING";
        case RequestStatus::APPROVED:  return "APPROVED";
        case RequestStatus::REJECTED:  return "REJECTED";
        case RequestStatus::APPEALED:  return "APPEALED";
        case RequestStatus::CANCELLED: return "CANCELLED";
    }
    return "PENDING";
}

inline RequestStatus requestStatusFromString(const std::string& s) {
    if (s == "PENDING" || s == "pending")     return RequestStatus::PENDING;
    if (s == "APPROVED" || s == "approved")   return RequestStatus::APPROVED;
    if (s == "REJECTED" || s == "rejected")   return RequestStatus::REJECTED;
    if (s == "APPEALED" || s == "appealed")   return RequestStatus::APPEALED;
    if (s == "CANCELLED" || s == "cancelled") return RequestStatus::CANCELLED;
    throw std::invalid_argument("Invalid RequestStatus: " + s);
}

inline std::string toString(TxnType t) {
    switch (t) {
        case TxnType::CREDIT: return "CREDIT";
        case TxnType::DEBIT:  return "DEBIT";
        case TxnType::PAYOUT: return "PAYOUT";
    }
    return "CREDIT";
}

inline TxnType txnTypeFromString(const std::string& s) {
    if (s == "CREDIT" || s == "credit") return TxnType::CREDIT;
    if (s == "DEBIT"  || s == "debit")  return TxnType::DEBIT;
    if (s == "PAYOUT" || s == "payout") return TxnType::PAYOUT;
    throw std::invalid_argument("Invalid TxnType: " + s);
}

inline std::string toString(UserRole r) {
    switch (r) {
        case UserRole::STUDENT:    return "STUDENT";
        case UserRole::ADMIN:      return "ADMIN";
        case UserRole::MESS_STAFF: return "MESS_STAFF";
    }
    return "STUDENT";
}

inline UserRole userRoleFromString(const std::string& s) {
    if (s == "STUDENT" || s == "student")       return UserRole::STUDENT;
    if (s == "ADMIN" || s == "admin")           return UserRole::ADMIN;
    if (s == "MESS_STAFF" || s == "mess_staff") return UserRole::MESS_STAFF;
    throw std::invalid_argument("Invalid UserRole: " + s);
}

inline std::string toString(PlanVariant v) {
    switch (v) {
        case PlanVariant::VEG:     return "VEG";
        case PlanVariant::NON_VEG: return "NON_VEG";
        case PlanVariant::PREMIUM: return "PREMIUM";
    }
    return "VEG";
}

inline PlanVariant planVariantFromString(const std::string& s) {
    if (s == "VEG" || s == "veg")         return PlanVariant::VEG;
    if (s == "NON_VEG" || s == "non_veg" || s == "non-veg") return PlanVariant::NON_VEG;
    if (s == "PREMIUM" || s == "premium") return PlanVariant::PREMIUM;
    throw std::invalid_argument("Invalid PlanVariant: " + s);
}

inline std::string toString(SettlementChoice c) {
    return (c == SettlementChoice::REFUND) ? "REFUND" : "CARRY_FORWARD";
}

inline SettlementChoice settlementChoiceFromString(const std::string& s) {
    if (s == "REFUND" || s == "refund") return SettlementChoice::REFUND;
    return SettlementChoice::CARRY_FORWARD;
}

} // namespace mealwallet

#endif // MEALWALLET_ENUMS_HPP

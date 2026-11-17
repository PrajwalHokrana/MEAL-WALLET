#ifndef MEALWALLET_RECORDS_HPP
#define MEALWALLET_RECORDS_HPP

#include "DateTime.hpp"
#include "Enums.hpp"
#include "Money.hpp"
#include <string>
#include <vector>

namespace mealwallet {

struct AttendanceRecord {
    std::string id;
    std::string studentId;
    Date mealDate;
    MealType mealType;
    std::string markedBy;
    int64_t timestampUtc = 0;
};

struct ClosureRecord {
    std::string id;
    Date closureDate;
    MealType mealType;
    std::string reason;
    int studentsCredited = 0;
    Money totalCredited;
    int64_t createdAtUtc = 0;
};

struct SettlementRecord {
    std::string id;
    std::string studentId;
    std::string yearMonth;
    SettlementChoice choice = SettlementChoice::CARRY_FORWARD;
    Money settledAmount;
    std::string payoutId; // Non-empty if choice == REFUND
    int64_t settledAtUtc = 0;
    std::string nextPlanId; // Plan selected / rolled into for the next cycle
};

struct PayoutRecord {
    std::string id;
    std::string studentId;
    std::string settlementId;
    Money amount;
    bool paidOffline = false;
    int64_t createdAtUtc = 0;
    int64_t paidAtUtc = 0;
    std::string paidNote;
};

struct NotificationRecord {
    std::string id;
    std::string studentId;
    std::string title;
    std::string message;
    bool isRead = false;
    int64_t createdAtUtc = 0;
};

struct SessionRecord {
    std::string token;
    std::string userId;
    std::string username;
    UserRole role = UserRole::STUDENT;
    int64_t createdAtUtc = 0;
    int64_t expiresAtUtc = 0;
};

/**
 * Admin review assistant: computes a SUGGESTED verdict ("APPROVE" or "REJECT")
 * with human-readable reasons based on cutoff compliance and counter attendance log.
 */
struct SuggestedVerdict {
    std::string verdict; // "APPROVE" or "REJECT"
    bool seenAtCounter = false;
    bool beforeCutoff = true;
    std::vector<std::string> reasons;
};

struct MealForecastItem {
    Date date;
    MealType mealType;
    int totalEnrolled = 0;
    int approvedSkips = 0;
    bool messClosed = false;
    int expectedHeadcount = 0;
};

} // namespace mealwallet

#endif // MEALWALLET_RECORDS_HPP

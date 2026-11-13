#ifndef MEALWALLET_EXCEPTIONS_HPP
#define MEALWALLET_EXCEPTIONS_HPP

#include <stdexcept>
#include <string>

namespace mealwallet {

/**
 * [OOP Concept: Custom Exception Hierarchy & Polymorphic Catching]
 * Base class MealWalletException inherits from std::runtime_error so callers
 * can catch either specific business rule violations or the base class.
 * Every exception stores the violated rule name so the UI can explain
 * clearly which rule was broken.
 */
class MealWalletException : public std::runtime_error {
private:
    std::string ruleName_;

public:
    explicit MealWalletException(const std::string& message,
                                 const std::string& ruleName = "GENERAL_RULE")
        : std::runtime_error(message), ruleName_(ruleName) {}

    virtual ~MealWalletException() noexcept = default;

    const std::string& ruleName() const noexcept { return ruleName_; }
};

/**
 * Thrown when a student tries to skip or cancel a meal after its cutoff window.
 * Message includes both the cutoff time and the actual request timestamp.
 */
class CutoffPassedException : public MealWalletException {
public:
    CutoffPassedException(const std::string& mealLabel,
                          const std::string& cutoffIst,
                          const std::string& requestIst)
        : MealWalletException(
              "Cutoff for " + mealLabel + " was " + cutoffIst +
              "; the request came at " + requestIst + ".",
              "CUTOFF_WINDOW_RULE") {}
};

/**
 * Thrown when a duplicate skip request is submitted for the same (student, date, meal).
 */
class DuplicateRequestException : public MealWalletException {
public:
    DuplicateRequestException(const std::string& studentId,
                              const std::string& dateStr,
                              const std::string& mealStr)
        : MealWalletException(
              "Duplicate request: student " + studentId + " already has a skip request for " +
              mealStr + " on " + dateStr + " (Rule: max 1 request per student + date + meal).",
              "UNIQUE_SLOT_RULE") {}
};

/**
 * Thrown when a student exceeds the monthly configurable skip cap (default 10 meals).
 */
class SkipLimitExceededException : public MealWalletException {
public:
    SkipLimitExceededException(int currentCount, int maxCap, const std::string& yearMonth)
        : MealWalletException(
              "Monthly skip cap reached for " + yearMonth + ": already used " +
              std::to_string(currentCount) + " of " + std::to_string(maxCap) +
              " allowed meal skips this month.",
              "MONTHLY_SKIP_CAP_RULE") {}
};

/**
 * Thrown when a wallet debit (e.g., meal swap upgrade or zero-balance refund) exceeds balance.
 */
class InsufficientBalanceException : public MealWalletException {
public:
    InsufficientBalanceException(const std::string& details)
        : MealWalletException(details, "WALLET_BALANCE_RULE") {}
};

} // namespace mealwallet

#endif // MEALWALLET_EXCEPTIONS_HPP

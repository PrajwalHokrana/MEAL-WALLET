#ifndef MEALWALLET_SETTLEMENT_ENGINE_HPP
#define MEALWALLET_SETTLEMENT_ENGINE_HPP

#include "Records.hpp"
#include "User.hpp"
#include <string>

namespace mealwallet {


class SettlementEngine {
public:
    struct Result {
        SettlementRecord settlement;
        bool createdPayout = false;
        PayoutRecord payout;
        std::unique_ptr<Transaction> ledgerEntry;
    };

    static Result settleStudentMonthEnd(Student& student,
                                        const std::string& yearMonth,
                                        int64_t nowUtc);
};

} // namespace mealwallet

#endif // MEALWALLET_SETTLEMENT_ENGINE_HPP

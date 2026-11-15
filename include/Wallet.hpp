#ifndef MEALWALLET_WALLET_HPP
#define MEALWALLET_WALLET_HPP

#include "Exceptions.hpp"
#include "Money.hpp"
#include "RefundPolicy.hpp"
#include "Request.hpp"
#include "Transaction.hpp"
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

namespace mealwallet {

class SettlementEngine; // Forward declaration for friend class


class Wallet {
private:
    std::string studentId_;
    std::vector<std::unique_ptr<Transaction>> ledger_;

    
    friend class SettlementEngine;

    
    friend std::ostream& operator<<(std::ostream& os, const Wallet& w);

public:
    explicit Wallet(std::string studentId = "")
        : studentId_(std::move(studentId)) {}

    // Rule of Five: Destructor
    ~Wallet() = default;

    // Rule of Five: Deep-Copy Constructor (clones each polymorphic Transaction)
    Wallet(const Wallet& other) : studentId_(other.studentId_) {
        ledger_.reserve(other.ledger_.size());
        for (const auto& txnPtr : other.ledger_) {
            if (txnPtr) {
                ledger_.push_back(txnPtr->clone());
            }
        }
    }

    // Rule of Five: Deep-Copy Assignment Operator (copy-and-swap / self-check)
    Wallet& operator=(const Wallet& other) {
        if (this != &other) {
            studentId_ = other.studentId_;
            std::vector<std::unique_ptr<Transaction>> cloned;
            cloned.reserve(other.ledger_.size());
            for (const auto& txnPtr : other.ledger_) {
                if (txnPtr) {
                    cloned.push_back(txnPtr->clone());
                }
            }
            ledger_ = std::move(cloned);
        }
        return *this;
    }

    // Rule of Five: Move Constructor & Move Assignment
    Wallet(Wallet&& other) noexcept = default;
    Wallet& operator=(Wallet&& other) noexcept = default;

    const std::string& studentId() const noexcept { return studentId_; }
    void setStudentId(const std::string& id) { studentId_ = id; }

    /**
     * Derived balance using STL std::accumulate and Runtime Polymorphism (txn->signedAmount()).
     * Never stores or edits a mutable balance field.
     */
    Money balance() const noexcept {
        return std::accumulate(
            ledger_.begin(), ledger_.end(), Money::fromPaise(0),
            [](Money acc, const std::unique_ptr<Transaction>& txn) {
                return txn ? (acc + txn->signedAmount()) : acc;
            });
    }

    Money totalCredited() const noexcept {
        return std::accumulate(
            ledger_.begin(), ledger_.end(), Money::fromPaise(0),
            [](Money acc, const std::unique_ptr<Transaction>& txn) {
                if (txn && txn->type() == TxnType::CREDIT) {
                    return acc + txn->amount();
                }
                return acc;
            });
    }

    // --- Function Overloading: 3 overloads of Wallet::credit() ---

    // Overload 1: Minimal credit with Money only
    const Transaction& credit(Money amount, int64_t timestampUtc = 0);

    // Overload 2: Credit with Money + explicit reason and reference ID
    const Transaction& credit(Money amount,
                              const std::string& reason,
                              int64_t timestampUtc,
                              const std::string& refId = "");

    // Overload 3: Credit directly from an approved SingleSkipRequest + polymorphic RefundPolicy
    const Transaction& credit(const SingleSkipRequest& approvedReq,
                              Money baseMealPrice,
                              const RefundPolicy& policy,
                              int64_t timestampUtc);

    // Debit wallet (throws InsufficientBalanceException if amount > balance())
    const Transaction& debit(Money amount,
                             const std::string& reason,
                             int64_t timestampUtc,
                             const std::string& refId = "");

    // Replay an existing persisted transaction when loading the append-only ledger at startup
    void replayPersistedTransaction(std::unique_ptr<Transaction> txn) {
        if (txn) {
            ledger_.push_back(std::move(txn));
        }
    }

    const std::vector<std::unique_ptr<Transaction>>& transactions() const noexcept {
        return ledger_;
    }
};

} // namespace mealwallet

#endif // MEALWALLET_WALLET_HPP

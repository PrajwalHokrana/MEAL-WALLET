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

/**
 * [OOP Concept: Encapsulation, Rule of Five, Function Overloading & Friend]
 * 1. Encapsulation: ledger_ is strictly private and holds polymorphic
 *    std::unique_ptr<Transaction> objects. There is NO stored balance variable;
 *    balance() is derived on the fly from the append-only ledger.
 *    Only credit() and debit() can append to ledger_.
 * 2. Rule of Five: Because Wallet owns deep polymorphic pointers
 *    (std::vector<std::unique_ptr<Transaction>>), the compiler-generated copy
 *    constructor would fail. We implement a deep-cloning Copy Constructor,
 *    Copy Assignment Operator, Move Constructor, Move Assignment Operator,
 *    and Destructor.
 * 3. Function Overloading: Three overloaded Wallet::credit(...) methods:
 *    - credit(Money amount, int64_t tsUtc)
 *    - credit(Money amount, const std::string& reason, int64_t tsUtc, const std::string& refId)
 *    - credit(const SingleSkipRequest& req, Money baseMealPrice, const RefundPolicy& policy, int64_t tsUtc)
 * 4. Friend: SettlementEngine and operator<< are declared as friends with clear viva justifications.
 */
class Wallet {
private:
    std::string studentId_;
    std::vector<std::unique_ptr<Transaction>> ledger_;

    /**
     * [OOP Concept: Friend Class Justification]
     * SettlementEngine needs to inspect raw ledger history and record atomic
     * month-end settlement payout/carry-forward entries during batch settlement
     * without exposing mutable ledger internals to arbitrary callers.
     */
    friend class SettlementEngine;

    /**
     * [OOP Concept: Friend Operator<< Justification]
     * Prints a formatted mini-statement of the wallet's private ledger to an ostream.
     */
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

#ifndef MEALWALLET_TRANSACTION_HPP
#define MEALWALLET_TRANSACTION_HPP

#include "Enums.hpp"
#include "Money.hpp"
#include <memory>
#include <string>

namespace mealwallet {


class Transaction {
protected:
    std::string id_;
    std::string studentId_;
    Money amount_;
    std::string reason_;
    std::string refId_;
    int64_t timestampUtc_;

public:
    Transaction(std::string id,
                std::string studentId,
                Money amount,
                std::string reason,
                std::string refId,
                int64_t timestampUtc)
        : id_(std::move(id)),
          studentId_(std::move(studentId)),
          amount_(amount),
          reason_(std::move(reason)),
          refId_(std::move(refId)),
          timestampUtc_(timestampUtc) {}

    virtual ~Transaction() = default;

    const std::string& id() const noexcept { return id_; }
    const std::string& studentId() const noexcept { return studentId_; }
    Money amount() const noexcept { return amount_; }
    const std::string& reason() const noexcept { return reason_; }
    const std::string& refId() const noexcept { return refId_; }
    int64_t timestampUtc() const noexcept { return timestampUtc_; }

    // Pure virtual functions for runtime polymorphism
    virtual TxnType type() const = 0;
    virtual Money signedAmount() const = 0;
    virtual std::unique_ptr<Transaction> clone() const = 0;

    static std::unique_ptr<Transaction> create(const std::string& id,
                                               const std::string& studentId,
                                               TxnType type,
                                               Money amount,
                                               const std::string& reason,
                                               const std::string& refId,
                                               int64_t timestampUtc);
};


class CreditTxn : public Transaction {
public:
    using Transaction::Transaction;

    TxnType type() const override { return TxnType::CREDIT; }
    Money signedAmount() const override { return amount_; }
    std::unique_ptr<Transaction> clone() const override {
        return std::make_unique<CreditTxn>(*this);
    }
};


class DebitTxn : public Transaction {
public:
    using Transaction::Transaction;

    TxnType type() const override { return TxnType::DEBIT; }
    Money signedAmount() const override { return -amount_; }
    std::unique_ptr<Transaction> clone() const override {
        return std::make_unique<DebitTxn>(*this);
    }
};


class PayoutTxn : public Transaction {
public:
    using Transaction::Transaction;

    TxnType type() const override { return TxnType::PAYOUT; }
    Money signedAmount() const override { return -amount_; }
    std::unique_ptr<Transaction> clone() const override {
        return std::make_unique<PayoutTxn>(*this);
    }
};

} // namespace mealwallet

#endif // MEALWALLET_TRANSACTION_HPP

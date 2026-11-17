#include "Compat.hpp"
#include "Config.hpp"
#include "Crypto.hpp"
#include "DateTime.hpp"
#include "MealPlan.hpp"
#include "SettlementEngine.hpp"
#include "Transaction.hpp"
#include "Wallet.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <random>
#include <sstream>

namespace mealwallet {

// ============================================================================
// Static Member Definitions for IdGenerator & Config
// [OOP Concept: Static Data Members]
// ============================================================================
int IdGenerator::nextUserSeq_    = 0;
int IdGenerator::nextSubSeq_     = 0;
int IdGenerator::nextReqSeq_     = 0;
int IdGenerator::nextTxnSeq_     = 0;
int IdGenerator::nextSettleSeq_  = 0;
int IdGenerator::nextPayoutSeq_  = 0;
int IdGenerator::nextClosureSeq_ = 0;
int IdGenerator::nextAttSeq_     = 0;
int IdGenerator::nextNotifSeq_   = 0;

int IdGenerator::extractSeq(const std::string& id) {
    auto pos = id.find('-');
    if (pos == std::string::npos || pos + 1 >= id.size()) return 0;
    try {
        return std::stoi(id.substr(pos + 1));
    } catch (...) {
        return 0;
    }
}

void IdGenerator::syncCounter(const std::string& prefix, int observedSeq) {
    if (prefix == "USR") nextUserSeq_    = std::max(nextUserSeq_, observedSeq);
    if (prefix == "SUB") nextSubSeq_     = std::max(nextSubSeq_, observedSeq);
    if (prefix == "REQ") nextReqSeq_     = std::max(nextReqSeq_, observedSeq);
    if (prefix == "TXN") nextTxnSeq_     = std::max(nextTxnSeq_, observedSeq);
    if (prefix == "STL") nextSettleSeq_  = std::max(nextSettleSeq_, observedSeq);
    if (prefix == "PAY") nextPayoutSeq_  = std::max(nextPayoutSeq_, observedSeq);
    if (prefix == "CLS") nextClosureSeq_ = std::max(nextClosureSeq_, observedSeq);
    if (prefix == "ATT") nextAttSeq_     = std::max(nextAttSeq_, observedSeq);
    if (prefix == "NTF") nextNotifSeq_   = std::max(nextNotifSeq_, observedSeq);
}

void IdGenerator::resetAll() {
    nextUserSeq_ = 0;
    nextSubSeq_ = 0;
    nextReqSeq_ = 0;
    nextTxnSeq_ = 0;
    nextSettleSeq_ = 0;
    nextPayoutSeq_ = 0;
    nextClosureSeq_ = 0;
    nextAttSeq_ = 0;
    nextNotifSeq_ = 0;
}

int Config::cutoffHoursBeforeMeal_     = 2;
int Config::monthlySkipCap_            = 10;
int Config::refundPercent_             = 80;
std::string Config::refundPolicyType_  = "PERCENT"; // "PERCENT" (80%) or "FULL" (100%)
std::string Config::repositoryMode_    = "TEXT";    // "TEXT", "BINARY", or "MEMORY"
std::string Config::dataDir_           = "./data";
int Config::serverPort_                = 8080;
std::string Config::adminSeedPassword_ = "admin123";
std::string Config::sessionSecret_     = "mealwallet-classroom-secret-2026";
bool Config::demoModeEnabled_          = false;
int64_t Config::demoClockEpochUtc_     = 0;

void Config::loadFromEnv() {
    if (const char* p = std::getenv("PORT")) {
        int val = std::atoi(p);
        if (val > 0 && val < 65536) serverPort_ = val;
    }
    if (const char* d = std::getenv("DATA_DIR")) {
        if (std::strlen(d) > 0) dataDir_ = d;
    }
    if (const char* a = std::getenv("ADMIN_SEED_PASSWORD")) {
        if (std::strlen(a) > 0) adminSeedPassword_ = a;
    }
    if (const char* s = std::getenv("SESSION_SECRET")) {
        if (std::strlen(s) > 0) sessionSecret_ = s;
    }
    if (const char* r = std::getenv("REPO_MODE")) {
        if (std::strlen(r) > 0) repositoryMode_ = r;
    }
}

// ============================================================================
// Session Token & Temporary Password Generators (High-Entropy, Thread-Safe)
// ============================================================================
std::string Crypto::generateToken(size_t hexLen) {
    static std::atomic<uint64_t> s_seq{1000};

    auto nowNs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()
        ).count());
    uint64_t seq = s_seq.fetch_add(1);

#ifdef _WIN32
    uint64_t pid = static_cast<uint64_t>(GetCurrentProcessId());
    uint64_t tid = static_cast<uint64_t>(GetCurrentThreadId());
#else
    uint64_t pid = static_cast<uint64_t>(getpid());
    uint64_t tid = static_cast<uint64_t>(pthread_self());
#endif

    // SplitMix64 mixing rounds combining nanosecond clock, atomic counter, PID, TID
    uint64_t z1 = nowNs + seq + (pid << 32) + tid + 0x9e3779b97f4a7c15ULL;
    z1 = (z1 ^ (z1 >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z1 = (z1 ^ (z1 >> 27)) * 0x94d049bb133111ebULL;
    uint64_t r1 = z1 ^ (z1 >> 31);

    uint64_t z2 = (nowNs ^ (seq << 24)) + (tid << 16) + 0x517cc1b727220a95ULL;
    z2 = (z2 ^ (z2 >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z2 = (z2 ^ (z2 >> 27)) * 0x94d049bb133111ebULL;
    uint64_t r2 = z2 ^ (z2 >> 31);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(16) << r1
        << std::setw(16) << r2
        << std::setw(8)  << static_cast<uint32_t>(seq);
    std::string s = oss.str();
    if (s.size() < hexLen) {
        s.append(hexLen - s.size(), 'a');
    }
    return s.substr(0, hexLen);
}

std::string Crypto::generateTempPassword() {
    static const char* kChars = "abcdefghjkmnpqrstuvwxyz23456789";
    static std::atomic<uint64_t> s_passSeq{1};
    auto nowNs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()
        ).count());
    uint64_t z = nowNs + s_passSeq.fetch_add(1) + 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    std::string code = "pass";
    for (int i = 0; i < 4; ++i) {
        code.push_back(kChars[(z >> (i * 5)) % 31]);
    }
    return code;
}

// ============================================================================
// MealPlan Factory & Transaction Factory
// ============================================================================
std::unique_ptr<MealPlan> MealPlan::create(const std::string& id,
                                           PlanVariant variant,
                                           const std::string& name,
                                           Money b, Money l, Money d) {
    switch (variant) {
        case PlanVariant::VEG:
            return std::make_unique<VegPlan>(id, name, b, l, d);
        case PlanVariant::NON_VEG:
            return std::make_unique<NonVegPlan>(id, name, b, l, d);
        case PlanVariant::PREMIUM:
            return std::make_unique<PremiumPlan>(id, name, b, l, d);
    }
    return std::make_unique<VegPlan>(id, name, b, l, d);
}

std::unique_ptr<Transaction> Transaction::create(const std::string& id,
                                                 const std::string& studentId,
                                                 TxnType type,
                                                 Money amount,
                                                 const std::string& reason,
                                                 const std::string& refId,
                                                 int64_t timestampUtc) {
    switch (type) {
        case TxnType::CREDIT:
            return std::make_unique<CreditTxn>(id, studentId, amount, reason, refId, timestampUtc);
        case TxnType::DEBIT:
            return std::make_unique<DebitTxn>(id, studentId, amount, reason, refId, timestampUtc);
        case TxnType::PAYOUT:
            return std::make_unique<PayoutTxn>(id, studentId, amount, reason, refId, timestampUtc);
    }
    return std::make_unique<CreditTxn>(id, studentId, amount, reason, refId, timestampUtc);
}

// ============================================================================
// Wallet Implementation (Function Overloading, Encapsulation, Friend <<)
// ============================================================================
const Transaction& Wallet::credit(Money amount, int64_t timestampUtc) {
    return credit(amount, "Manual wallet credit", timestampUtc, "MANUAL");
}

const Transaction& Wallet::credit(Money amount,
                                  const std::string& reason,
                                  int64_t timestampUtc,
                                  const std::string& refId) {
    if (amount <= Money::fromPaise(0)) {
        throw MealWalletException("Credit amount must be strictly positive.", "POSITIVE_AMOUNT_RULE");
    }
    std::string txnId = IdGenerator::nextTxnId();
    auto txn = std::make_unique<CreditTxn>(txnId, studentId_, amount, reason, refId, timestampUtc);
    const Transaction& ref = *txn;
    ledger_.push_back(std::move(txn));
    return ref;
}

const Transaction& Wallet::credit(const SingleSkipRequest& approvedReq,
                                  Money baseMealPrice,
                                  const RefundPolicy& policy,
                                  int64_t timestampUtc) {
    // Runtime polymorphism: policy.compute(baseMealPrice) calls either
    // FullRefundPolicy::compute or PercentRefundPolicy::compute
    Money refundAmt = policy.compute(baseMealPrice);
    std::string desc = "Approved skip refund: " + toString(approvedReq.mealType()) +
                       " on " + approvedReq.mealDate().toString() + " (" +
                       std::to_string(policy.percentage()) + "% of " +
                       baseMealPrice.toRupeeString() + ")";
    return credit(refundAmt, desc, timestampUtc, approvedReq.id());
}

const Transaction& Wallet::debit(Money amount,
                                 const std::string& reason,
                                 int64_t timestampUtc,
                                 const std::string& refId) {
    if (amount <= Money::fromPaise(0)) {
        throw MealWalletException("Debit amount must be strictly positive.", "POSITIVE_AMOUNT_RULE");
    }
    Money curBal = balance();
    if (curBal < amount) {
        throw InsufficientBalanceException(
            "Insufficient wallet balance: required " + amount.toRupeeString() +
            ", available " + curBal.toRupeeString() + ".");
    }
    std::string txnId = IdGenerator::nextTxnId();
    auto txn = std::make_unique<DebitTxn>(txnId, studentId_, amount, reason, refId, timestampUtc);
    const Transaction& ref = *txn;
    ledger_.push_back(std::move(txn));
    return ref;
}

std::ostream& operator<<(std::ostream& os, const Wallet& w) {
    os << "=== Wallet Statement [Student: " << w.studentId_
       << " | Derived Balance: " << w.balance() << "] ===\n";
    for (const auto& txn : w.ledger_) {
        if (!txn) continue;
        os << "  [" << txn->id() << "] " << toString(txn->type())
           << " | Signed: " << txn->signedAmount()
           << " | Reason: " << txn->reason()
           << " | Ref: " << txn->refId() << "\n";
    }
    return os;
}

// ============================================================================
// SettlementEngine Implementation (Friend of Wallet)
// ============================================================================
SettlementEngine::Result SettlementEngine::settleStudentMonthEnd(Student& student,
                                                                 const std::string& yearMonth,
                                                                 int64_t nowUtc) {
    Wallet& wallet = student.wallet();
    Money currentBal = wallet.balance();
    SettlementChoice choice = student.subscription().settlementChoice();

    if (currentBal <= Money::fromPaise(0)) {
        if (choice == SettlementChoice::REFUND) {
            throw InsufficientBalanceException(
                "Cannot process month-end REFUND for student " + student.id() +
                " because wallet balance is Rs. 0.00.");
        }
        // Zero-balance carry forward records a 0-amount settlement cleanly
        SettlementRecord rec;
        rec.id = IdGenerator::nextSettleId();
        rec.studentId = student.id();
        rec.yearMonth = yearMonth;
        rec.choice = choice;
        rec.settledAmount = Money::fromPaise(0);
        rec.payoutId = "";
        rec.settledAtUtc = nowUtc;
        rec.nextPlanId = student.subscription().nextCyclePlanId().empty()
            ? student.subscription().planId()
            : student.subscription().nextCyclePlanId();
        return Result{rec, false, PayoutRecord{}, nullptr};
    }

    SettlementRecord rec;
    rec.id = IdGenerator::nextSettleId();
    rec.studentId = student.id();
    rec.yearMonth = yearMonth;
    rec.choice = choice;
    rec.settledAmount = currentBal;
    rec.settledAtUtc = nowUtc;
    rec.nextPlanId = student.subscription().nextCyclePlanId().empty()
        ? student.subscription().planId()
        : student.subscription().nextCyclePlanId();

    Result res;
    res.settlement = rec;

    if (choice == SettlementChoice::REFUND) {
        // Create offline PayoutRecord and append PayoutTxn directly to private wallet.ledger_
        PayoutRecord pay;
        pay.id = IdGenerator::nextPayoutId();
        pay.studentId = student.id();
        pay.settlementId = rec.id;
        pay.amount = currentBal;
        pay.paidOffline = false;
        pay.createdAtUtc = nowUtc;
        pay.paidAtUtc = 0;
        pay.paidNote = "Pending offline bank/UPI payout by Admin";

        res.settlement.payoutId = pay.id;
        res.createdPayout = true;
        res.payout = pay;

        std::string txnId = IdGenerator::nextTxnId();
        auto payoutTxn = std::make_unique<PayoutTxn>(
            txnId, student.id(), currentBal,
            "Month-end REFUND settlement (" + yearMonth + ") -> Payout " + pay.id,
            pay.id, nowUtc);
        res.ledgerEntry = payoutTxn->clone();
        // Accessing private wallet.ledger_ via friend class SettlementEngine!
        wallet.ledger_.push_back(std::move(payoutTxn));
    } else {
        // CARRY_FORWARD: debit current wallet balance and apply it as a discount on next cycle's plan
        std::string txnId = IdGenerator::nextTxnId();
        auto debitTxn = std::make_unique<DebitTxn>(
            txnId, student.id(), currentBal,
            "Month-end CARRY_FORWARD applied to next plan cycle (" + yearMonth + ")",
            rec.id, nowUtc);
        res.ledgerEntry = debitTxn->clone();
        wallet.ledger_.push_back(std::move(debitTxn));
    }

    // Advance subscription cycle by 30 days and apply any queued nextCyclePlanId
    Date todayIst = TimeUtils::currentIstDate(nowUtc);
    Date curEnd   = student.subscription().endDate();
    Date nextStart = curEnd.addDays(1);
    Date nextEnd   = nextStart.addDays(29);

    // Keep active month continuous if settlement is run before current cycle expires
    Date effectiveStart = (todayIst <= curEnd && student.subscription().startDate() <= todayIst)
        ? student.subscription().startDate()
        : nextStart;

    PlanVariant nextVar = student.subscription().planVariant();
    if (student.subscription().nextCyclePlanId() == "PLAN-VEG") nextVar = PlanVariant::VEG;
    else if (student.subscription().nextCyclePlanId() == "PLAN-NONVEG") nextVar = PlanVariant::NON_VEG;
    else if (student.subscription().nextCyclePlanId() == "PLAN-PREMIUM") nextVar = PlanVariant::PREMIUM;

    Money carryDiscount = (choice == SettlementChoice::CARRY_FORWARD) ? currentBal : Money::fromPaise(0);
    student.subscription().rollToNextCycle(effectiveStart, nextEnd, nextVar, carryDiscount);

    return res;
}

} // namespace mealwallet

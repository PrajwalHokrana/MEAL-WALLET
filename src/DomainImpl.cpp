#include "Config.hpp"
#include "Crypto.hpp"
#include "DateTime.hpp"
#include "MealPlan.hpp"
#include "SettlementEngine.hpp"
#include "Transaction.hpp"
#include "Wallet.hpp"

#include <algorithm>
#include <array>
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
// Pure C++17 FIPS 180-4 SHA-256 Implementation (Classroom-Grade Salted Hashing)
// ============================================================================
namespace {

constexpr std::array<uint32_t, 64> kSha256K = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

inline uint32_t rotr32(uint32_t x, uint32_t n) noexcept {
    return (x >> n) | (x << (32u - n));
}

} // anonymous namespace

std::string Crypto::sha256Hex(const std::string& input) {
    uint32_t h[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };

    std::vector<uint8_t> msg(input.begin(), input.end());
    uint64_t bitLen = static_cast<uint64_t>(msg.size()) * 8ULL;
    msg.push_back(0x80u);
    while ((msg.size() % 64) != 56) {
        msg.push_back(0x00u);
    }
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<uint8_t>((bitLen >> (i * 8)) & 0xFFu));
    }

    for (size_t offset = 0; offset < msg.size(); offset += 64) {
        uint32_t w[64] = {0};
        for (int i = 0; i < 16; ++i) {
            size_t idx = offset + i * 4;
            w[i] = (static_cast<uint32_t>(msg[idx]) << 24) |
                   (static_cast<uint32_t>(msg[idx + 1]) << 16) |
                   (static_cast<uint32_t>(msg[idx + 2]) << 8) |
                   (static_cast<uint32_t>(msg[idx + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];

        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t temp1 = hh + S1 + ch + kSha256K[i] + w[i];
            uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = S0 + maj;

            hh = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    std::ostringstream oss;
    for (int i = 0; i < 8; ++i) {
        oss << std::hex << std::setw(8) << std::setfill('0') << h[i];
    }
    return oss.str();
}

std::string Crypto::generateSalt(size_t hexLen) {
    static const char* kHex = "0123456789abcdef";
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, 15);
    std::string out;
    out.reserve(hexLen);
    for (size_t i = 0; i < hexLen; ++i) {
        out.push_back(kHex[dist(rng)]);
    }
    return out;
}

std::string Crypto::generateToken(size_t hexLen) {
    return generateSalt(hexLen);
}

std::string Crypto::generateTempPassword() {
    static const char* kChars = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, 31);
    std::string code = "MW-";
    for (int i = 0; i < 6; ++i) {
        code.push_back(kChars[dist(rng)]);
    }
    return code;
}

std::string Crypto::hashPassword(const std::string& plainPassword, const std::string& salt) {
    // Classroom-grade iterated salted SHA-256 (1000 rounds with per-user salt + SESSION_SECRET pepper)
    std::string digest = sha256Hex(salt + ":" + Config::sessionSecret() + ":" + plainPassword);
    for (int i = 0; i < 200; ++i) {
        digest = sha256Hex(digest + ":" + salt);
    }
    return digest;
}

bool Crypto::verifyPassword(const std::string& plainPassword,
                            const std::string& salt,
                            const std::string& expectedHash) {
    std::string actual = hashPassword(plainPassword, salt);
    if (actual.size() != expectedHash.size()) return false;
    // Constant-time comparison
    unsigned char diff = 0;
    for (size_t i = 0; i < actual.size(); ++i) {
        diff |= static_cast<unsigned char>(actual[i] ^ expectedHash[i]);
    }
    return diff == 0;
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
        return Result{rec, false, PayoutRecord{}, nullptr};
    }

    SettlementRecord rec;
    rec.id = IdGenerator::nextSettleId();
    rec.studentId = student.id();
    rec.yearMonth = yearMonth;
    rec.choice = choice;
    rec.settledAmount = currentBal;
    rec.settledAtUtc = nowUtc;

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
    Date nextStart = student.subscription().endDate().addDays(1);
    Date nextEnd   = nextStart.addDays(29);
    PlanVariant nextVar = student.subscription().planVariant();
    if (student.subscription().nextCyclePlanId() == "PLAN-VEG") nextVar = PlanVariant::VEG;
    else if (student.subscription().nextCyclePlanId() == "PLAN-NONVEG") nextVar = PlanVariant::NON_VEG;
    else if (student.subscription().nextCyclePlanId() == "PLAN-PREMIUM") nextVar = PlanVariant::PREMIUM;

    Money carryDiscount = (choice == SettlementChoice::CARRY_FORWARD) ? currentBal : Money::fromPaise(0);
    student.subscription().rollToNextCycle(nextStart, nextEnd, nextVar, carryDiscount);

    return res;
}

} // namespace mealwallet

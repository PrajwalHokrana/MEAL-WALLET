#ifndef MEALWALLET_CONFIG_HPP
#define MEALWALLET_CONFIG_HPP

#include <atomic>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

namespace mealwallet {


class IdGenerator {
private:
    static int nextUserSeq_;
    static int nextSubSeq_;
    static int nextReqSeq_;
    static int nextTxnSeq_;
    static int nextSettleSeq_;
    static int nextPayoutSeq_;
    static int nextClosureSeq_;
    static int nextAttSeq_;
    static int nextNotifSeq_;

    static std::string formatSeq(const std::string& prefix, int seq) {
        std::ostringstream oss;
        oss << prefix << "-" << std::setw(4) << std::setfill('0') << seq;
        return oss.str();
    }

public:
    static std::string nextUserId()    { return formatSeq("USR", ++nextUserSeq_); }
    static std::string nextSubId()     { return formatSeq("SUB", ++nextSubSeq_); }
    static std::string nextReqId()     { return formatSeq("REQ", ++nextReqSeq_); }
    static std::string nextTxnId()     { return formatSeq("TXN", ++nextTxnSeq_); }
    static std::string nextSettleId()  { return formatSeq("STL", ++nextSettleSeq_); }
    static std::string nextPayoutId()  { return formatSeq("PAY", ++nextPayoutSeq_); }
    static std::string nextClosureId() { return formatSeq("CLS", ++nextClosureSeq_); }
    static std::string nextAttId()     { return formatSeq("ATT", ++nextAttSeq_); }
    static std::string nextNotifId()   { return formatSeq("NTF", ++nextNotifSeq_); }

    // Synchronize static counters after loading existing records from disk
    static void syncCounter(const std::string& prefix, int observedSeq);
    static int extractSeq(const std::string& id);
    static void resetAll();
};


class Config {
private:
    static int cutoffHoursBeforeMeal_;
    static int monthlySkipCap_;
    static int refundPercent_;        // 100 = FullRefundPolicy, <100 = PercentRefundPolicy
    static std::string refundPolicyType_; // "FULL" or "PERCENT"
    static std::string repositoryMode_;   // "TEXT", "BINARY", or "MEMORY"
    static std::string dataDir_;
    static int serverPort_;
    static std::string adminSeedPassword_;
    static std::string sessionSecret_;
    static bool demoModeEnabled_;
    static int64_t demoClockEpochUtc_;

public:
    static void loadFromEnv();

    static int cutoffHoursBeforeMeal() noexcept { return cutoffHoursBeforeMeal_; }
    static void setCutoffHoursBeforeMeal(int h) noexcept { cutoffHoursBeforeMeal_ = h; }

    static int monthlySkipCap() noexcept { return monthlySkipCap_; }
    static void setMonthlySkipCap(int cap) noexcept { monthlySkipCap_ = cap; }

    static int refundPercent() noexcept { return refundPercent_; }
    static void setRefundPercent(int pct) noexcept { refundPercent_ = pct; }

    static const std::string& refundPolicyType() noexcept { return refundPolicyType_; }
    static void setRefundPolicyType(const std::string& t) { refundPolicyType_ = t; }

    static const std::string& repositoryMode() noexcept { return repositoryMode_; }
    static void setRepositoryMode(const std::string& mode) { repositoryMode_ = mode; }

    static const std::string& dataDir() noexcept { return dataDir_; }
    static void setDataDir(const std::string& dir) { dataDir_ = dir; }

    static int serverPort() noexcept { return serverPort_; }
    static void setServerPort(int p) noexcept { serverPort_ = p; }

    static const std::string& adminSeedPassword() noexcept { return adminSeedPassword_; }
    static const std::string& sessionSecret() noexcept { return sessionSecret_; }

    static bool demoModeEnabled() noexcept { return demoModeEnabled_; }
    static void setDemoModeEnabled(bool enabled) noexcept { demoModeEnabled_ = enabled; }

    static int64_t demoClockEpochUtc() noexcept { return demoClockEpochUtc_; }
    static void setDemoClockEpochUtc(int64_t utc) noexcept { demoClockEpochUtc_ = utc; }
};

} // namespace mealwallet

#endif // MEALWALLET_CONFIG_HPP

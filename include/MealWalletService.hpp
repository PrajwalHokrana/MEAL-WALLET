#ifndef MEALWALLET_SERVICE_HPP
#define MEALWALLET_SERVICE_HPP

#include "Compat.hpp"
#include "Config.hpp"
#include "Crypto.hpp"
#include "DateTime.hpp"
#include "Enums.hpp"
#include "Exceptions.hpp"
#include "IRepository.hpp"
#include "MealPlan.hpp"
#include "Records.hpp"
#include "RefundPolicy.hpp"
#include "Request.hpp"
#include "SettlementEngine.hpp"
#include "Subscription.hpp"
#include "Transaction.hpp"
#include "User.hpp"
#include "Wallet.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mealwallet {

// Persisted row structures for IRepository<T>
struct PersistedUserRow {
    std::string id;
    UserRole role = UserRole::STUDENT;
    std::string username;
    std::string fullName;
    std::string extraField; // roomNumber for Student, title/shift for Admin/Staff
    std::string salt;
    std::string passwordHash;
    bool mustChangePassword = true;
};

struct PersistedPlanRow {
    std::string id;
    PlanVariant variant = PlanVariant::VEG;
    std::string name;
    Money breakfast;
    Money lunch;
    Money dinner;
};

struct PersistedTxnRow {
    std::string id;
    std::string studentId;
    TxnType type = TxnType::CREDIT;
    Money amount;
    std::string reason;
    std::string refId;
    int64_t timestampUtc = 0;
};

/**
 * [OOP Concept: Facade / Domain Service with Thread-Safe Write-Through Persistence]
 * Coordinates all domain objects, enforces every business rule, manages the active
 * polymorphic Clock and RefundPolicy, and wraps every state mutation in a single
 * global std::recursive_mutex with write-through file persistence and audit logging.
 * Contains ZERO HTTP, SQL, or JSON code — can be driven from a plain console main().
 */
class MealWalletService {
private:
    mutable compat::RecursiveMutex mutex_;
    std::string dataDir_;
    std::string repoMode_;

    // Polymorphic Clock (SystemClock or DemoClock)
    std::unique_ptr<SystemClock> systemClock_;
    std::unique_ptr<DemoClock> demoClock_;
    Clock* activeClock_ = nullptr;

    // Polymorphic RefundPolicy (FullRefundPolicy or PercentRefundPolicy)
    std::unique_ptr<RefundPolicy> activeRefundPolicy_;

    // In-memory domain state (loaded at startup, written through on every change)
    std::map<std::string, std::unique_ptr<MealPlan>> plans_;
    std::map<std::string, Student> students_;
    std::map<std::string, Admin> admins_;
    std::map<std::string, MessStaff> staff_;
    std::vector<SingleSkipRequest> skipRequests_;
    std::vector<PersistedTxnRow> ledgerRows_;
    std::vector<SettlementRecord> settlements_;
    std::vector<PayoutRecord> payouts_;
    std::vector<ClosureRecord> closures_;
    std::vector<AttendanceRecord> attendance_;
    std::vector<NotificationRecord> notifications_;
    std::map<std::string, SessionRecord> sessions_;

    // Polymorphic Template Repositories (IRepository<T>)
    std::unique_ptr<IRepository<PersistedUserRow>> userRepo_;
    std::unique_ptr<IRepository<PersistedPlanRow>> planRepo_;
    std::unique_ptr<IRepository<Subscription>> subRepo_;
    std::unique_ptr<IRepository<SingleSkipRequest>> skipRepo_;
    std::unique_ptr<IRepository<PersistedTxnRow>> ledgerRepo_;
    std::unique_ptr<IRepository<SettlementRecord>> settlementRepo_;
    std::unique_ptr<IRepository<PayoutRecord>> payoutRepo_;
    std::unique_ptr<IRepository<ClosureRecord>> closureRepo_;
    std::unique_ptr<IRepository<AttendanceRecord>> attendanceRepo_;
    std::unique_ptr<IRepository<NotificationRecord>> notifRepo_;
    std::unique_ptr<IRepository<SessionRecord>> sessionRepo_;

    void initRepositories();
    void rebuildRefundPolicy();
    void saveUsersAndSubsUnlocked();
    void saveConfigUnlocked() const;
    void loadConfigUnlocked();
    void writeAuditUnlocked(const std::string& actor,
                            const std::string& action,
                            const std::string& detail);
    void pushNotificationUnlocked(const std::string& studentId,
                                  const std::string& title,
                                  const std::string& message);

public:
    explicit MealWalletService(std::string dataDir = Config::dataDir(),
                               std::string repoMode = Config::repositoryMode());

    // Initialize: loads from disk if present; if empty/missing, seeds demo data automatically
    void initialize(bool autoSeedIfEmpty = true);

    // Seed & Reset (<2s restore for live two-laptop demo)
    struct SeedCredential {
        std::string role;
        std::string username;
        std::string password;
        std::string fullName;
        std::string planInfo;
    };
    std::vector<SeedCredential> resetAndSeedDemoData();

    // Clock & Demo Mode controls
    int64_t nowUtc() const;
    bool isDemoMode() const;
    void setDemoMode(bool enabled, int64_t customUtcEpoch = 0);
    void advanceDemoClock(int hours, int days);
    const Clock& clock() const { return *activeClock_; }
    const RefundPolicy& refundPolicy() const { return *activeRefundPolicy_; }

    // Authentication & Session handling
    struct LoginResult {
        SessionRecord session;
        std::string fullName;
        bool mustChangePassword = false;
        std::string dashboardRoute;
    };
    LoginResult login(const std::string& username, const std::string& password);
    void logout(const std::string& token);
    SessionRecord authenticateToken(const std::string& token) const;
    void changePassword(const std::string& userId,
                        const std::string& oldPassword,
                        const std::string& newPassword);

    // Admin: Create Student account + generate credentials
    struct CreatedStudentInfo {
        Student student;
        std::string generatedPassword;
    };
    CreatedStudentInfo createStudent(const std::string& username,
                                     const std::string& fullName,
                                     const std::string& roomNumber,
                                     const std::string& planId,
                                     const Date& cycleStart,
                                     const Date& cycleEnd,
                                     const std::string& customInitialPassword = "");

    // Student: Skip Request Flow (Single & Multi-Day)
    SingleSkipRequest submitSingleSkip(const std::string& studentId,
                                       const Date& mealDate,
                                       MealType mealType,
                                       const std::string& reason);

    std::vector<SingleSkipRequest> submitMultiDaySkip(const std::string& studentId,
                                                      const Date& startDate,
                                                      const Date& endDate,
                                                      const std::vector<MealType>& meals,
                                                      const std::string& reason);

    void cancelPendingSkip(const std::string& studentId, const std::string& requestId);

    SingleSkipRequest appealRejectedSkip(const std::string& studentId,
                                         const std::string& requestId,
                                         const std::string& appealReason);

    // Student: Meal Swap (e.g., Veg to Non-Veg on a given day debits the price difference)
    Money swapMealUpgrade(const std::string& studentId,
                          const Date& mealDate,
                          MealType mealType,
                          const std::string& targetPlanId);

    // Student: Plan change for next cycle
    void requestNextCyclePlanChange(const std::string& studentId, const std::string& newPlanId);

    // Student: Month-end settlement choice (REFUND or CARRY_FORWARD)
    void setSettlementChoice(const std::string& studentId, SettlementChoice choice);

    // Admin: Suggested Verdict + Decision (Approve / Reject)
    SuggestedVerdict computeSuggestedVerdict(const SingleSkipRequest& req) const;

    SingleSkipRequest decideSkipRequest(const std::string& requestId,
                                        bool approve,
                                        const std::string& adminNote,
                                        const std::string& adminUsername = "admin");

    // Admin: Counter Attendance Logging
    AttendanceRecord markAttendance(const std::string& studentId,
                                    const Date& mealDate,
                                    MealType mealType,
                                    const std::string& markedBy);

    // Admin: Mess-Closed Day (auto-credits every active student for that meal)
    ClosureRecord markMessClosure(const Date& closureDate,
                                  MealType mealType,
                                  const std::string& reason,
                                  const std::string& adminUsername = "admin");

    // Admin: Month-End Settlement Runner & Offline Payout Marking
    std::vector<SettlementRecord> runMonthlySettlement(const std::string& yearMonth,
                                                       const std::string& adminUsername = "admin");

    PayoutRecord markPayoutPaid(const std::string& payoutId,
                                const std::string& note,
                                const std::string& adminUsername = "admin");

    // Admin: Forecast Tomorrow's (or any date's) Headcount per Meal
    std::vector<MealForecastItem> getForecastForDate(const Date& targetDate) const;

    // Queries for Dashboards, Rules, Audit Log, and Backup Export
    Student getStudentById(const std::string& studentId) const;
    std::vector<Student> listStudents() const;
    std::vector<const MealPlan*> listPlans() const;
    const MealPlan* getPlanById(const std::string& planId) const;
    std::vector<SingleSkipRequest> listSkipRequests(const std::string& statusFilter = "",
                                                    const std::string& studentIdFilter = "") const;
    std::vector<NotificationRecord> listNotificationsForStudent(const std::string& studentId) const;
    std::vector<SettlementRecord> listSettlements() const;
    std::vector<PayoutRecord> listPayouts() const;
    std::vector<ClosureRecord> listClosures() const;
    std::vector<AttendanceRecord> listAttendance() const;
    std::vector<std::string> readAuditLogLines(size_t maxLines = 100) const;
    std::string exportBackupDump() const;
    int countStudentSkipsInMonth(const std::string& studentId, const std::string& yearMonth) const;
};

} // namespace mealwallet

#endif // MEALWALLET_SERVICE_HPP

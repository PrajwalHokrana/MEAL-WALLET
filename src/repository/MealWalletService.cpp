#include "MealWalletService.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_set>

namespace mealwallet {

namespace {

// Helper template factory that selects TextFileRepository<T>, BinaryFileRepository<T>,
// or InMemoryRepository<T> at startup based on repoMode without changing domain code.
template <typename T>
std::unique_ptr<IRepository<T>> makeRepository(
    const std::string& mode,
    const std::string& filePath,
    const std::string& headerTag,
    size_t expectedCols,
    typename TextFileRepository<T>::Serializer ser,
    typename TextFileRepository<T>::Deserializer deser) {
    if (mode == "MEMORY") {
        return std::make_unique<InMemoryRepository<T>>();
    }
    if (mode == "BINARY") {
        return std::make_unique<BinaryFileRepository<T>>(filePath + ".bin", expectedCols, ser, deser);
    }
    return std::make_unique<TextFileRepository<T>>(filePath, headerTag, expectedCols, ser, deser);
}

} // anonymous namespace

MealWalletService::MealWalletService(std::string dataDir, std::string repoMode)
    : dataDir_(std::move(dataDir)),
      repoMode_(std::move(repoMode)),
      systemClock_(std::make_unique<SystemClock>()),
      demoClock_(std::make_unique<DemoClock>(
          TimeUtils::istToEpochUtc(Date(2026, 11, 13), 7, 15, 0))) {
    activeClock_ = systemClock_.get();
    rebuildRefundPolicy();
    initRepositories();
}

void MealWalletService::rebuildRefundPolicy() {
    if (Config::refundPolicyType() == "FULL" || Config::refundPercent() >= 100) {
        activeRefundPolicy_ = std::make_unique<FullRefundPolicy>();
    } else {
        activeRefundPolicy_ = std::make_unique<PercentRefundPolicy>(Config::refundPercent());
    }
}

void MealWalletService::initRepositories() {
    compat::ensureDirectory(dataDir_);

    // 1. students.txt (stores all users: Student, Admin, MessStaff)
    // Columns: id|role|username|fullName|extraField|salt|passwordHash|mustChangePassword
    userRepo_ = makeRepository<PersistedUserRow>(
        repoMode_, dataDir_ + "/students.txt", "students", 8,
        [](const PersistedUserRow& u) {
            std::ostringstream oss;
            oss << escapeField(u.id) << "|"
                << toString(u.role) << "|"
                << escapeField(u.username) << "|"
                << escapeField(u.fullName) << "|"
                << escapeField(u.extraField) << "|"
                << escapeField(u.salt) << "|"
                << escapeField(u.passwordHash) << "|"
                << (u.mustChangePassword ? "1" : "0");
            return oss.str();
        },
        [](const std::vector<std::string>& c, PersistedUserRow& u) {
            u.id = c[0];
            u.role = userRoleFromString(c[1]);
            u.username = c[2];
            u.fullName = c[3];
            u.extraField = c[4];
            u.salt = c[5];
            u.passwordHash = c[6];
            u.mustChangePassword = (c[7] == "1" || c[7] == "true");
            return !u.id.empty() && !u.username.empty();
        });

    // 2. plans.txt
    // Columns: id|variant|name|breakfastPaise|lunchPaise|dinnerPaise
    planRepo_ = makeRepository<PersistedPlanRow>(
        repoMode_, dataDir_ + "/plans.txt", "plans", 6,
        [](const PersistedPlanRow& p) {
            std::ostringstream oss;
            oss << escapeField(p.id) << "|"
                << toString(p.variant) << "|"
                << escapeField(p.name) << "|"
                << p.breakfast.paise() << "|"
                << p.lunch.paise() << "|"
                << p.dinner.paise();
            return oss.str();
        },
        [](const std::vector<std::string>& c, PersistedPlanRow& p) {
            p.id = c[0];
            p.variant = planVariantFromString(c[1]);
            p.name = c[2];
            p.breakfast = Money::fromPaise(std::stoll(c[3]));
            p.lunch = Money::fromPaise(std::stoll(c[4]));
            p.dinner = Money::fromPaise(std::stoll(c[5]));
            return !p.id.empty();
        });

    // 3. subscriptions.txt
    // Columns: id|studentId|planId|variant|startDate|endDate|settlementChoice|nextCyclePlanId|carryForwardPaise|active
    subRepo_ = makeRepository<Subscription>(
        repoMode_, dataDir_ + "/subscriptions.txt", "subscriptions", 10,
        [](const Subscription& s) {
            std::ostringstream oss;
            oss << escapeField(s.id()) << "|"
                << escapeField(s.studentId()) << "|"
                << escapeField(s.planId()) << "|"
                << toString(s.planVariant()) << "|"
                << s.startDate().toString() << "|"
                << s.endDate().toString() << "|"
                << toString(s.settlementChoice()) << "|"
                << escapeField(s.nextCyclePlanId()) << "|"
                << s.carryForwardCreditApplied().paise() << "|"
                << (s.active() ? "1" : "0");
            return oss.str();
        },
        [](const std::vector<std::string>& c, Subscription& s) {
            s = Subscription(
                c[0], c[1], c[2], planVariantFromString(c[3]),
                Date::parse(c[4]), Date::parse(c[5]),
                settlementChoiceFromString(c[6]), c[7],
                Money::fromPaise(std::stoll(c[8])),
                (c[9] == "1" || c[9] == "true"));
            return !s.id().empty();
        });

    // 4. skip_requests.txt
    // Columns: id|studentId|mealDate|mealType|status|reason|createdAtUtc|appealed|appealReason|adminNote|decidedAtUtc|batchGroupId
    skipRepo_ = makeRepository<SingleSkipRequest>(
        repoMode_, dataDir_ + "/skip_requests.txt", "skip_requests", 12,
        [](const SingleSkipRequest& r) {
            std::ostringstream oss;
            oss << escapeField(r.id()) << "|"
                << escapeField(r.studentId()) << "|"
                << r.mealDate().toString() << "|"
                << toString(r.mealType()) << "|"
                << toString(r.status()) << "|"
                << escapeField(r.reason()) << "|"
                << r.createdAtUtc() << "|"
                << (r.appealed() ? "1" : "0") << "|"
                << escapeField(r.appealReason()) << "|"
                << escapeField(r.adminNote()) << "|"
                << r.decidedAtUtc() << "|"
                << escapeField(r.batchGroupId());
            return oss.str();
        },
        [](const std::vector<std::string>& c, SingleSkipRequest& r) {
            r = SingleSkipRequest(
                c[0], c[1], Date::parse(c[2]), mealTypeFromString(c[3]),
                c[5], std::stoll(c[6]), requestStatusFromString(c[4]),
                (c[7] == "1" || c[7] == "true"), c[8], c[9],
                std::stoll(c[10]), c[11]);
            return !r.id().empty();
        });

    // 5. ledger.txt (Append-only!)
    // Columns: id|studentId|type|amountPaise|reason|refId|timestampUtc|isoUtc
    ledgerRepo_ = makeRepository<PersistedTxnRow>(
        repoMode_, dataDir_ + "/ledger.txt", "ledger", 7,
        [](const PersistedTxnRow& t) {
            std::ostringstream oss;
            oss << escapeField(t.id) << "|"
                << escapeField(t.studentId) << "|"
                << toString(t.type) << "|"
                << t.amount.paise() << "|"
                << escapeField(t.reason) << "|"
                << escapeField(t.refId) << "|"
                << t.timestampUtc << "|"
                << TimeUtils::formatUtcIso(t.timestampUtc);
            return oss.str();
        },
        [](const std::vector<std::string>& c, PersistedTxnRow& t) {
            t.id = c[0];
            t.studentId = c[1];
            t.type = txnTypeFromString(c[2]);
            t.amount = Money::fromPaise(std::stoll(c[3]));
            t.reason = c[4];
            t.refId = c[5];
            t.timestampUtc = std::stoll(c[6]);
            return !t.id.empty() && t.amount.paise() > 0;
        });

    // 6. settlements.txt
    // Columns: id|studentId|yearMonth|choice|settledPaise|payoutId|settledAtUtc
    settlementRepo_ = makeRepository<SettlementRecord>(
        repoMode_, dataDir_ + "/settlements.txt", "settlements", 7,
        [](const SettlementRecord& s) {
            std::ostringstream oss;
            oss << escapeField(s.id) << "|"
                << escapeField(s.studentId) << "|"
                << escapeField(s.yearMonth) << "|"
                << toString(s.choice) << "|"
                << s.settledAmount.paise() << "|"
                << escapeField(s.payoutId) << "|"
                << s.settledAtUtc;
            return oss.str();
        },
        [](const std::vector<std::string>& c, SettlementRecord& s) {
            s.id = c[0];
            s.studentId = c[1];
            s.yearMonth = c[2];
            s.choice = settlementChoiceFromString(c[3]);
            s.settledAmount = Money::fromPaise(std::stoll(c[4]));
            s.payoutId = c[5];
            s.settledAtUtc = std::stoll(c[6]);
            return !s.id.empty();
        });

    // 7. payouts.txt
    // Columns: id|studentId|settlementId|amountPaise|paidOffline|createdAtUtc|paidAtUtc|paidNote
    payoutRepo_ = makeRepository<PayoutRecord>(
        repoMode_, dataDir_ + "/payouts.txt", "payouts", 8,
        [](const PayoutRecord& p) {
            std::ostringstream oss;
            oss << escapeField(p.id) << "|"
                << escapeField(p.studentId) << "|"
                << escapeField(p.settlementId) << "|"
                << p.amount.paise() << "|"
                << (p.paidOffline ? "1" : "0") << "|"
                << p.createdAtUtc << "|"
                << p.paidAtUtc << "|"
                << escapeField(p.paidNote);
            return oss.str();
        },
        [](const std::vector<std::string>& c, PayoutRecord& p) {
            p.id = c[0];
            p.studentId = c[1];
            p.settlementId = c[2];
            p.amount = Money::fromPaise(std::stoll(c[3]));
            p.paidOffline = (c[4] == "1" || c[4] == "true");
            p.createdAtUtc = std::stoll(c[5]);
            p.paidAtUtc = std::stoll(c[6]);
            p.paidNote = c[7];
            return !p.id.empty();
        });

    // 8. closures.txt
    // Columns: id|closureDate|mealType|reason|studentsCredited|totalCreditedPaise|createdAtUtc
    closureRepo_ = makeRepository<ClosureRecord>(
        repoMode_, dataDir_ + "/closures.txt", "closures", 7,
        [](const ClosureRecord& cl) {
            std::ostringstream oss;
            oss << escapeField(cl.id) << "|"
                << cl.closureDate.toString() << "|"
                << toString(cl.mealType) << "|"
                << escapeField(cl.reason) << "|"
                << cl.studentsCredited << "|"
                << cl.totalCredited.paise() << "|"
                << cl.createdAtUtc;
            return oss.str();
        },
        [](const std::vector<std::string>& c, ClosureRecord& cl) {
            cl.id = c[0];
            cl.closureDate = Date::parse(c[1]);
            cl.mealType = mealTypeFromString(c[2]);
            cl.reason = c[3];
            cl.studentsCredited = std::stoi(c[4]);
            cl.totalCredited = Money::fromPaise(std::stoll(c[5]));
            cl.createdAtUtc = std::stoll(c[6]);
            return !cl.id.empty();
        });

    // 9. attendance.txt
    // Columns: id|studentId|mealDate|mealType|markedBy|timestampUtc
    attendanceRepo_ = makeRepository<AttendanceRecord>(
        repoMode_, dataDir_ + "/attendance.txt", "attendance", 6,
        [](const AttendanceRecord& a) {
            std::ostringstream oss;
            oss << escapeField(a.id) << "|"
                << escapeField(a.studentId) << "|"
                << a.mealDate.toString() << "|"
                << toString(a.mealType) << "|"
                << escapeField(a.markedBy) << "|"
                << a.timestampUtc;
            return oss.str();
        },
        [](const std::vector<std::string>& c, AttendanceRecord& a) {
            a.id = c[0];
            a.studentId = c[1];
            a.mealDate = Date::parse(c[2]);
            a.mealType = mealTypeFromString(c[3]);
            a.markedBy = c[4];
            a.timestampUtc = std::stoll(c[5]);
            return !a.id.empty();
        });

    // 10. notifications.txt
    // Columns: id|studentId|title|message|isRead|createdAtUtc
    notifRepo_ = makeRepository<NotificationRecord>(
        repoMode_, dataDir_ + "/notifications.txt", "notifications", 6,
        [](const NotificationRecord& n) {
            std::ostringstream oss;
            oss << escapeField(n.id) << "|"
                << escapeField(n.studentId) << "|"
                << escapeField(n.title) << "|"
                << escapeField(n.message) << "|"
                << (n.isRead ? "1" : "0") << "|"
                << n.createdAtUtc;
            return oss.str();
        },
        [](const std::vector<std::string>& c, NotificationRecord& n) {
            n.id = c[0];
            n.studentId = c[1];
            n.title = c[2];
            n.message = c[3];
            n.isRead = (c[4] == "1" || c[4] == "true");
            n.createdAtUtc = std::stoll(c[5]);
            return !n.id.empty();
        });

    // 11. sessions.txt
    // Columns: token|userId|username|role|createdAtUtc|expiresAtUtc
    sessionRepo_ = makeRepository<SessionRecord>(
        repoMode_, dataDir_ + "/sessions.txt", "sessions", 6,
        [](const SessionRecord& s) {
            std::ostringstream oss;
            oss << escapeField(s.token) << "|"
                << escapeField(s.userId) << "|"
                << escapeField(s.username) << "|"
                << toString(s.role) << "|"
                << s.createdAtUtc << "|"
                << s.expiresAtUtc;
            return oss.str();
        },
        [](const std::vector<std::string>& c, SessionRecord& s) {
            s.token = c[0];
            s.userId = c[1];
            s.username = c[2];
            s.role = userRoleFromString(c[3]);
            s.createdAtUtc = std::stoll(c[4]);
            s.expiresAtUtc = std::stoll(c[5]);
            return !s.token.empty();
        });
}

void MealWalletService::saveConfigUnlocked() const {
    if (repoMode_ == "MEMORY") return;
    std::string cfgPath = dataDir_ + "/config.txt";
    std::string tmpPath = cfgPath + ".tmp";
    {
        std::ofstream out(tmpPath, std::ios::out | std::ios::trunc);
        out << "#MEALWALLET_V1|config\n";
        out << "CUTOFF_HOURS|" << Config::cutoffHoursBeforeMeal() << "\n";
        out << "MONTHLY_SKIP_CAP|" << Config::monthlySkipCap() << "\n";
        out << "REFUND_POLICY_TYPE|" << Config::refundPolicyType() << "\n";
        out << "REFUND_PERCENT|" << Config::refundPercent() << "\n";
        out << "DEMO_MODE_ENABLED|" << ( isDemoMode() ? "1" : "0" ) << "\n";
        out << "DEMO_CLOCK_UTC|" << demoClock_->now() << "\n";
        out.flush();
    }
    std::rename(tmpPath.c_str(), cfgPath.c_str());
}

void MealWalletService::loadConfigUnlocked() {
    if (repoMode_ == "MEMORY") return;
    std::ifstream in(dataDir_ + "/config.txt");
    if (!in.is_open()) return;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto cols = splitPipeLine(line, '|');
        if (cols.size() < 2) continue;
        try {
            if (cols[0] == "CUTOFF_HOURS") Config::setCutoffHoursBeforeMeal(std::stoi(cols[1]));
            else if (cols[0] == "MONTHLY_SKIP_CAP") Config::setMonthlySkipCap(std::stoi(cols[1]));
            else if (cols[0] == "REFUND_POLICY_TYPE") Config::setRefundPolicyType(cols[1]);
            else if (cols[0] == "REFUND_PERCENT") Config::setRefundPercent(std::stoi(cols[1]));
            else if (cols[0] == "DEMO_MODE_ENABLED") {
                bool en = (cols[1] == "1" || cols[1] == "true");
                Config::setDemoModeEnabled(en);
                activeClock_ = en ? static_cast<Clock*>(demoClock_.get())
                                  : static_cast<Clock*>(systemClock_.get());
            } else if (cols[0] == "DEMO_CLOCK_UTC") {
                int64_t ts = std::stoll(cols[1]);
                if (ts > 0) demoClock_->setNow(ts);
            }
        } catch (...) {}
    }
    rebuildRefundPolicy();
}

void MealWalletService::writeAuditUnlocked(const std::string& actor,
                                           const std::string& action,
                                           const std::string& detail) {
    int64_t ts = nowUtc();
    std::ostringstream line;
    line << TimeUtils::formatUtcIso(ts) << " | "
         << TimeUtils::formatIstPretty(ts) << " | Actor=" << escapeField(actor)
         << " | Action=" << escapeField(action)
         << " | " << escapeField(detail);

    // Print live readable line to console so C++ server activity is visible during live presentation
    std::cout << "[MEAL-WALLET EVENT] " << line.str() << std::endl;

    if (repoMode_ != "MEMORY") {
        std::ofstream out(dataDir_ + "/audit.log", std::ios::out | std::ios::app);
        if (out.is_open()) {
            out << line.str() << "\n";
            out.flush();
        }
    }
}

void MealWalletService::pushNotificationUnlocked(const std::string& studentId,
                                                 const std::string& title,
                                                 const std::string& message) {
    NotificationRecord n;
    n.id = IdGenerator::nextNotifId();
    n.studentId = studentId;
    n.title = title;
    n.message = message;
    n.isRead = false;
    n.createdAtUtc = nowUtc();
    notifications_.push_back(n);
    notifRepo_->saveAll(notifications_);
}

void MealWalletService::saveUsersAndSubsUnlocked() {
    std::vector<PersistedUserRow> uRows;
    std::vector<Subscription> sRows;

    for (const auto& kv : admins_) {
        const auto& adm = kv.second;
        uRows.push_back(PersistedUserRow{
            adm.id(), UserRole::ADMIN, adm.username(), adm.fullName(),
            adm.title(), adm.passwordSalt(), adm.passwordHash(), adm.mustChangePassword()
        });
    }
    for (const auto& kv : staff_) {
        const auto& stf = kv.second;
        uRows.push_back(PersistedUserRow{
            stf.id(), UserRole::MESS_STAFF, stf.username(), stf.fullName(),
            stf.shift(), stf.passwordSalt(), stf.passwordHash(), stf.mustChangePassword()
        });
    }
    for (const auto& kv : students_) {
        const auto& stu = kv.second;
        uRows.push_back(PersistedUserRow{
            stu.id(), UserRole::STUDENT, stu.username(), stu.fullName(),
            stu.roomNumber(), stu.passwordSalt(), stu.passwordHash(), stu.mustChangePassword()
        });
        sRows.push_back(stu.subscription());
    }
    userRepo_->saveAll(uRows);
    subRepo_->saveAll(sRows);
}

void MealWalletService::initialize(bool autoSeedIfEmpty) {
    compat::LockGuard lock(mutex_);
    compat::ensureDirectory(dataDir_);
    loadConfigUnlocked();

    auto planRows = planRepo_->loadAll();
    auto userRows = userRepo_->loadAll();

    if (userRows.empty() || planRows.empty()) {
        if (autoSeedIfEmpty) {
            resetAndSeedDemoData();
        }
        return;
    }

    plans_.clear();
    for (const auto& pr : planRows) {
        plans_[pr.id] = MealPlan::create(pr.id, pr.variant, pr.name, pr.breakfast, pr.lunch, pr.dinner);
    }

    auto subRows = subRepo_->loadAll();
    std::map<std::string, Subscription> subByStudent;
    for (const auto& s : subRows) {
        IdGenerator::syncCounter("SUB", IdGenerator::extractSeq(s.id()));
        subByStudent[s.studentId()] = s;
    }

    students_.clear();
    admins_.clear();
    staff_.clear();
    for (const auto& u : userRows) {
        IdGenerator::syncCounter("USR", IdGenerator::extractSeq(u.id));
        if (u.role == UserRole::ADMIN) {
            admins_[u.id] = Admin(u.id, u.username, u.fullName, u.salt, u.passwordHash, u.mustChangePassword, u.extraField);
        } else if (u.role == UserRole::MESS_STAFF) {
            staff_[u.id] = MessStaff(u.id, u.username, u.fullName, u.salt, u.passwordHash, u.mustChangePassword, u.extraField);
        } else {
            Subscription sub = subByStudent.count(u.id) ? subByStudent[u.id] : Subscription();
            students_[u.id] = Student(u.id, u.username, u.fullName, u.extraField, u.salt, u.passwordHash, u.mustChangePassword, sub);
        }
    }

    // Load skip requests enforcing uniqueness on (studentId, mealDate, mealType)
    skipRequests_.clear();
    std::unordered_set<std::string> seenSlots;
    for (const auto& req : skipRepo_->loadAll()) {
        IdGenerator::syncCounter("REQ", IdGenerator::extractSeq(req.id()));
        std::string slotKey = req.studentId() + "|" + req.mealDate().toString() + "|" + toString(req.mealType());
        if (req.status() != RequestStatus::CANCELLED && seenSlots.count(slotKey)) {
            std::cerr << "[WARN] Skipping duplicate slot in skip_requests.txt: " << slotKey << "\n";
            continue;
        }
        if (req.status() != RequestStatus::CANCELLED) {
            seenSlots.insert(slotKey);
        }
        skipRequests_.push_back(req);
    }

    // Replay append-only ledger into each Student's private Wallet
    ledgerRows_ = ledgerRepo_->loadAll();
    for (const auto& row : ledgerRows_) {
        IdGenerator::syncCounter("TXN", IdGenerator::extractSeq(row.id));
        auto it = students_.find(row.studentId);
        if (it != students_.end()) {
            it->second.wallet().replayPersistedTransaction(
                Transaction::create(row.id, row.studentId, row.type, row.amount, row.reason, row.refId, row.timestampUtc));
        }
    }

    settlements_ = settlementRepo_->loadAll();
    for (const auto& s : settlements_) IdGenerator::syncCounter("STL", IdGenerator::extractSeq(s.id));

    payouts_ = payoutRepo_->loadAll();
    for (const auto& p : payouts_) IdGenerator::syncCounter("PAY", IdGenerator::extractSeq(p.id));

    closures_ = closureRepo_->loadAll();
    for (const auto& c : closures_) IdGenerator::syncCounter("CLS", IdGenerator::extractSeq(c.id));

    attendance_ = attendanceRepo_->loadAll();
    for (const auto& a : attendance_) IdGenerator::syncCounter("ATT", IdGenerator::extractSeq(a.id));

    notifications_ = notifRepo_->loadAll();
    for (const auto& n : notifications_) IdGenerator::syncCounter("NTF", IdGenerator::extractSeq(n.id));

    sessions_.clear();
    for (const auto& s : sessionRepo_->loadAll()) {
        sessions_[s.token] = s;
    }
}

std::vector<MealWalletService::SeedCredential> MealWalletService::resetAndSeedDemoData() {
    compat::LockGuard lock(mutex_);
    IdGenerator::resetAll();

    plans_.clear();
    students_.clear();
    admins_.clear();
    staff_.clear();
    skipRequests_.clear();
    ledgerRows_.clear();
    settlements_.clear();
    payouts_.clear();
    closures_.clear();
    attendance_.clear();
    notifications_.clear();
    sessions_.clear();

    if (repoMode_ != "MEMORY") {
        compat::ensureDirectory(dataDir_);
        std::remove((dataDir_ + "/ledger.txt").c_str());
        std::remove((dataDir_ + "/ledger.txt.bin").c_str());
        std::remove((dataDir_ + "/audit.log").c_str());
    }

    // Default rules
    Config::setCutoffHoursBeforeMeal(2);
    Config::setMonthlySkipCap(10);
    Config::setRefundPolicyType("PERCENT");
    Config::setRefundPercent(80);
    rebuildRefundPolicy();

    // Set DemoClock to a predictable morning time (2026-11-13 06:00 AM IST)
    // Breakfast is at 08:00 AM (cutoff 02:00 AM passed),
    // Lunch is at 12:30 PM (cutoff 06:30 AM -> 30 minutes left!),
    // Dinner is at 08:00 PM (cutoff 02:00 PM -> 8 hours left!).
    Date todayIst = TimeUtils::currentIstDate(systemClock_->now());
    demoClock_->setNow(TimeUtils::istToEpochUtc(todayIst, 6, 0, 0));
    activeClock_ = Config::demoModeEnabled()
                       ? static_cast<Clock*>(demoClock_.get())
                       : static_cast<Clock*>(systemClock_.get());

    // 1. Seed 3 MealPlans (Veg, Non-Veg, Premium)
    plans_["PLAN-VEG"]     = std::make_unique<VegPlan>("PLAN-VEG", "Standard Veg Plan",
                                                       Money::fromRupees(35), Money::fromRupees(55), Money::fromRupees(55));
    plans_["PLAN-NONVEG"]  = std::make_unique<NonVegPlan>("PLAN-NONVEG", "Protein Non-Veg Plan",
                                                          Money::fromRupees(45), Money::fromRupees(75), Money::fromRupees(75));
    plans_["PLAN-PREMIUM"] = std::make_unique<PremiumPlan>("PLAN-PREMIUM", "Executive Premium Plan",
                                                           Money::fromRupees(55), Money::fromRupees(90), Money::fromRupees(95));

    std::vector<PersistedPlanRow> planRows;
    for (const auto& kv : plans_) {
        const auto& p = kv.second;
        planRows.push_back(PersistedPlanRow{
            p->id(), p->variant(), p->displayName(),
            p->mealPrice(MealType::BREAKFAST),
            p->mealPrice(MealType::LUNCH),
            p->mealPrice(MealType::DINNER)
        });
    }
    planRepo_->saveAll(planRows);

    std::vector<SeedCredential> creds;

    // 2. Seed 1 Admin
    {
        std::string admId = IdGenerator::nextUserId(); // USR-0001
        std::string salt  = Crypto::generateSalt();
        std::string pass  = Config::adminSeedPassword();
        std::string hash  = Crypto::hashPassword(pass, salt);
        admins_[admId] = Admin(admId, "admin", "Dr. Rajesh Sharma (Chief Warden)", salt, hash, false, "Chief Mess Warden");
        creds.push_back({"ADMIN", "admin", pass, "Dr. Rajesh Sharma (Chief Warden)", "Full Admin Console"});
    }

    // 3. Seed 1 MessStaff
    {
        std::string stfId = IdGenerator::nextUserId(); // USR-0002
        std::string salt  = Crypto::generateSalt();
        std::string pass  = "staff123";
        std::string hash  = Crypto::hashPassword(pass, salt);
        staff_[stfId] = MessStaff(stfId, "messstaff", "Ramesh Kulkarni (Mess Counter)", salt, hash, false, "ALL_MEALS");
        creds.push_back({"MESS_STAFF", "messstaff", pass, "Ramesh Kulkarni (Mess Counter)", "Counter Attendance & Forecast"});
    }

    // 4. Seed 5 Students with different plans (veg, non-veg, premium) and one near month-end
    struct StuSeed {
        std::string username;
        std::string fullName;
        std::string room;
        std::string planId;
        PlanVariant variant;
        std::string password;
        bool forcePasswordChange;
        Date start;
        Date end;
        SettlementChoice choice;
    };

    Date cycleStart(todayIst.year(), todayIst.month(), 1);
    Date cycleEnd(todayIst.year(), todayIst.month(), Date::daysInMonth(todayIst.year(), todayIst.month()));

    std::vector<StuSeed> stuSeeds = {
        {"aarav",   "Aarav Deshmukh",  "B-204", "PLAN-VEG",     PlanVariant::VEG,     "student123", false,
         cycleStart, cycleEnd, SettlementChoice::CARRY_FORWARD},
        {"priya",   "Priya Nair",      "A-112", "PLAN-NONVEG",  PlanVariant::NON_VEG, "student123", false,
         cycleStart, cycleEnd, SettlementChoice::REFUND},
        {"rohan",   "Rohan Verma",     "C-305", "PLAN-PREMIUM", PlanVariant::PREMIUM, "student123", false,
         cycleStart, todayIst.addDays(1), SettlementChoice::REFUND}, // Near plan validity / month-end!
        {"sneha",   "Sneha Joshi",     "A-208", "PLAN-VEG",     PlanVariant::VEG,     "student123", false,
         cycleStart, cycleEnd, SettlementChoice::CARRY_FORWARD},
        {"vikram",  "Vikram Patil",    "B-410", "PLAN-NONVEG",  PlanVariant::NON_VEG, "firstlogin", true,
         cycleStart, cycleEnd, SettlementChoice::CARRY_FORWARD}  // Forces password change on first login!
    };

    std::vector<std::string> stuIds;
    for (const auto& ss : stuSeeds) {
        std::string uid  = IdGenerator::nextUserId();
        std::string sid  = IdGenerator::nextSubId();
        std::string salt = Crypto::generateSalt();
        std::string hash = Crypto::hashPassword(ss.password, salt);
        Subscription sub(sid, uid, ss.planId, ss.variant, ss.start, ss.end, ss.choice);
        students_[uid] = Student(uid, ss.username, ss.fullName, ss.room, salt, hash, ss.forcePasswordChange, sub);
        stuIds.push_back(uid);
        creds.push_back({"STUDENT", ss.username, ss.password, ss.fullName,
                         ss.planId + (ss.forcePasswordChange ? " (First-Login Password Change)" : "")});
    }

    int64_t basePastUtc = nowUtc() - 3 * 86400LL;

    // 5. Seed past approved skip requests + append-only ledger entries
    auto addSeedApprovedSkip = [&](const std::string& stuId, const Date& d, MealType m, const std::string& reason, int64_t ts) {
        std::string reqId = IdGenerator::nextReqId();
        SingleSkipRequest req(reqId, stuId, d, m, reason, ts - 28800,
                              RequestStatus::APPROVED, false, "",
                              "Approved: submitted before 6h cutoff & not in counter log", ts);
        skipRequests_.push_back(req);

        Student& stu = students_[stuId];
        const MealPlan* plan = plans_[stu.subscription().planId()].get();
        const Transaction& txn = stu.wallet().credit(req, plan->mealPrice(m), *activeRefundPolicy_, ts);
        PersistedTxnRow row{txn.id(), stuId, txn.type(), txn.amount(), txn.reason(), txn.refId(), txn.timestampUtc()};
        ledgerRows_.push_back(row);
    };

    // Aarav (USR-0003): 2 past approved skips
    addSeedApprovedSkip(stuIds[0], todayIst.addDays(-3), MealType::LUNCH,  "Department lab workshop", basePastUtc);
    addSeedApprovedSkip(stuIds[0], todayIst.addDays(-2), MealType::DINNER, "Family dinner in city",   basePastUtc + 36000);

    // Priya (USR-0004): 2 past approved skips
    addSeedApprovedSkip(stuIds[1], todayIst.addDays(-2), MealType::LUNCH,  "Hackathon qualifier",     basePastUtc + 7200);
    addSeedApprovedSkip(stuIds[1], todayIst.addDays(-1), MealType::DINNER, "Traveling home",          basePastUtc + 86400);

    // Rohan (USR-0005 - near month-end with REFUND selected): 3 past approved skips (Executive Premium Plan)
    addSeedApprovedSkip(stuIds[2], todayIst.addDays(-4), MealType::BREAKFAST, "Early morning clinic visit", basePastUtc - 40000);
    addSeedApprovedSkip(stuIds[2], todayIst.addDays(-3), MealType::LUNCH,     "Internship interview",       basePastUtc);
    addSeedApprovedSkip(stuIds[2], todayIst.addDays(-1), MealType::DINNER,    "Sports tournament",          basePastUtc + 90000);

    // 6. Seed 2 Pending Requests in the Admin Queue (1 clean, 1 suspicious fraud case seen at counter!)
    {
        // Clean pending request from Aarav for tomorrow's Dinner
        std::string r1 = IdGenerator::nextReqId();
        skipRequests_.push_back(SingleSkipRequest(
            r1, stuIds[0], todayIst.addDays(1), MealType::DINNER,
            "Going to railway station", nowUtc() - 1800, RequestStatus::PENDING));

        // Fraud-case pending request from Sneha (stuIds[3]): submitted for today's Breakfast,
        // AND MessStaff logged Sneha eating at the counter!
        std::string r2 = IdGenerator::nextReqId();
        Date fraudDate = todayIst;
        int64_t bfastCutoff = TimeUtils::mealCutoffUtc(fraudDate, MealType::BREAKFAST, 6);
        skipRequests_.push_back(SingleSkipRequest(
            r2, stuIds[3], fraudDate, MealType::BREAKFAST,
            "Skipped breakfast due to headache", bfastCutoff - 3600, RequestStatus::PENDING));

        AttendanceRecord att;
        att.id = IdGenerator::nextAttId();
        att.studentId = stuIds[3];
        att.mealDate = fraudDate;
        att.mealType = MealType::BREAKFAST;
        att.markedBy = "messstaff";
        att.timestampUtc = TimeUtils::mealStartUtc(fraudDate, MealType::BREAKFAST) + 900;
        attendance_.push_back(att);
    }

    // 7. Seed welcome notifications
    pushNotificationUnlocked(stuIds[0], "Welcome to Meal Wallet",
                             "Your Standard Veg Plan is active. Approved missed meals credit 80% to your wallet.");
    pushNotificationUnlocked(stuIds[2], "Plan Validity Ending Soon",
                             "Your Executive Premium Plan ends tomorrow. Your current choice is REFUND (Rs. 192.00).");

    // Persist everything cleanly to DATA_DIR
    saveUsersAndSubsUnlocked();
    skipRepo_->saveAll(skipRequests_);
    ledgerRepo_->saveAll(ledgerRows_);
    settlementRepo_->saveAll(settlements_);
    payoutRepo_->saveAll(payouts_);
    closureRepo_->saveAll(closures_);
    attendanceRepo_->saveAll(attendance_);
    notifRepo_->saveAll(notifications_);
    sessionRepo_->saveAll({});
    saveConfigUnlocked();

    writeAuditUnlocked("SYSTEM", "SEED_DEMO", "Initialized demo seed data (1 admin, 1 staff, 5 students, 3 plans).");
    return creds;
}

int64_t MealWalletService::nowUtc() const {
    compat::LockGuard lock(mutex_);
    return activeClock_->now();
}

bool MealWalletService::isDemoMode() const {
    compat::LockGuard lock(mutex_);
    return activeClock_->isDemoMode();
}

void MealWalletService::setDemoMode(bool enabled, int64_t customUtcEpoch) {
    compat::LockGuard lock(mutex_);
    if (customUtcEpoch > 0) {
        demoClock_->setNow(customUtcEpoch);
    }
    Config::setDemoModeEnabled(enabled);
    activeClock_ = enabled ? static_cast<Clock*>(demoClock_.get())
                           : static_cast<Clock*>(systemClock_.get());
    saveConfigUnlocked();
    writeAuditUnlocked("admin", "DEMO_CLOCK_MODE",
                       std::string("Switched clock mode to ") + activeClock_->modeName() +
                       " (" + TimeUtils::formatIstPretty(activeClock_->now()) + ")");
}

void MealWalletService::advanceDemoClock(int hours, int days) {
    compat::LockGuard lock(mutex_);
    if (!activeClock_->isDemoMode()) {
        demoClock_->setNow(systemClock_->now());
        Config::setDemoModeEnabled(true);
        activeClock_ = demoClock_.get();
    }
    if (hours != 0) demoClock_->advanceHours(hours);
    if (days != 0)  demoClock_->advanceDays(days);
    saveConfigUnlocked();
    writeAuditUnlocked("admin", "DEMO_CLOCK_ADVANCE",
                       "Advanced DemoClock by " + std::to_string(hours) + "h, " +
                       std::to_string(days) + "d -> " + TimeUtils::formatIstPretty(demoClock_->now()));
}

MealWalletService::LoginResult MealWalletService::login(const std::string& username,
                                                        const std::string& password) {
    compat::LockGuard lock(mutex_);
    const User* matchedUser = nullptr;

    for (const auto& kv : admins_) {
        if (kv.second.username() == username) { matchedUser = &kv.second; break; }
    }
    if (!matchedUser) {
        for (const auto& kv : staff_) {
            if (kv.second.username() == username) { matchedUser = &kv.second; break; }
        }
    }
    if (!matchedUser) {
        for (const auto& kv : students_) {
            if (kv.second.username() == username) { matchedUser = &kv.second; break; }
        }
    }

    if (!matchedUser || !Crypto::verifyPassword(password, matchedUser->passwordSalt(), matchedUser->passwordHash())) {
        writeAuditUnlocked(username, "LOGIN_FAILED", "Invalid username or password");
        throw MealWalletException("Invalid username or password. Please check your credentials.", "AUTH_RULE");
    }

    SessionRecord sess;
    sess.token = Crypto::generateToken(40);
    sess.userId = matchedUser->id();
    sess.username = matchedUser->username();
    sess.role = matchedUser->role();
    sess.createdAtUtc = nowUtc();
    sess.expiresAtUtc = sess.createdAtUtc + 7 * 86400LL;
    sessions_[sess.token] = sess;

    std::vector<SessionRecord> allSessions;
    for (const auto& kv : sessions_) allSessions.push_back(kv.second);
    sessionRepo_->saveAll(allSessions);

    writeAuditUnlocked(matchedUser->username(), "LOGIN_SUCCESS",
                       "Role=" + toString(matchedUser->role()) + " UserID=" + matchedUser->id());

    return LoginResult{
        sess,
        matchedUser->fullName(),
        matchedUser->mustChangePassword(),
        matchedUser->dashboardRoute()
    };
}

void MealWalletService::logout(const std::string& token) {
    compat::LockGuard lock(mutex_);
    auto it = sessions_.find(token);
    if (it != sessions_.end()) {
        std::string uname = it->second.username;
        sessions_.erase(it);
        std::vector<SessionRecord> allSessions;
        for (const auto& kv : sessions_) allSessions.push_back(kv.second);
        sessionRepo_->saveAll(allSessions);
        writeAuditUnlocked(uname, "LOGOUT", "Session terminated");
    }
}

SessionRecord MealWalletService::authenticateToken(const std::string& token) const {
    compat::LockGuard lock(mutex_);
    auto it = sessions_.find(token);
    if (it == sessions_.end()) {
        throw MealWalletException("Session expired or invalid. Please log in again.", "SESSION_AUTH_RULE");
    }
    return it->second;
}

void MealWalletService::changePassword(const std::string& userId,
                                       const std::string& oldPassword,
                                       const std::string& newPassword) {
    compat::LockGuard lock(mutex_);
    if (newPassword.size() < 6) {
        throw MealWalletException("New password must be at least 6 characters long.", "PASSWORD_POLICY_RULE");
    }

    User* target = nullptr;
    if (admins_.count(userId)) target = &admins_[userId];
    else if (staff_.count(userId)) target = &staff_[userId];
    else if (students_.count(userId)) target = &students_[userId];

    if (!target) {
        throw MealWalletException("User account not found.", "USER_LOOKUP_RULE");
    }
    if (!Crypto::verifyPassword(oldPassword, target->passwordSalt(), target->passwordHash())) {
        throw MealWalletException("Current password is incorrect.", "PASSWORD_POLICY_RULE");
    }

    std::string newSalt = Crypto::generateSalt();
    std::string newHash = Crypto::hashPassword(newPassword, newSalt);
    target->updatePasswordHash(newSalt, newHash, false);
    saveUsersAndSubsUnlocked();

    writeAuditUnlocked(target->username(), "PASSWORD_CHANGED", "Completed password update");
}

MealWalletService::CreatedStudentInfo MealWalletService::createStudent(
    const std::string& username,
    const std::string& fullName,
    const std::string& roomNumber,
    const std::string& planId,
    const Date& cycleStart,
    const Date& cycleEnd,
    const std::string& customInitialPassword) {

    compat::LockGuard lock(mutex_);
    if (username.empty() || fullName.empty()) {
        throw MealWalletException("Username and full name are required.", "VALIDATION_RULE");
    }
    for (const auto& kv : students_) {
        const auto& stu = kv.second;
        if (stu.username() == username) {
            throw MealWalletException("Username '" + username + "' is already taken.", "UNIQUE_USERNAME_RULE");
        }
    }
    if (plans_.find(planId) == plans_.end()) {
        throw MealWalletException("Selected meal plan '" + planId + "' does not exist.", "VALID_PLAN_RULE");
    }

    std::string uid = IdGenerator::nextUserId();
    std::string sid = IdGenerator::nextSubId();
    std::string tempPass = customInitialPassword.empty() ? Crypto::generateTempPassword() : customInitialPassword;
    std::string salt = Crypto::generateSalt();
    std::string hash = Crypto::hashPassword(tempPass, salt);

    PlanVariant variant = plans_[planId]->variant();
    Subscription sub(sid, uid, planId, variant, cycleStart, cycleEnd, SettlementChoice::CARRY_FORWARD);
    Student stu(uid, username, fullName, roomNumber.empty() ? "Hostel-1" : roomNumber, salt, hash, true, sub);
    students_[uid] = stu;

    saveUsersAndSubsUnlocked();
    pushNotificationUnlocked(uid, "Account Created",
                             "Assigned " + plans_[planId]->displayName() + " (" +
                             cycleStart.toString() + " to " + cycleEnd.toString() + ").");
    writeAuditUnlocked("admin", "STUDENT_CREATED",
                       "Created student " + username + " (" + uid + ") with plan " + planId);

    return CreatedStudentInfo{stu, tempPass};
}

Subscription MealWalletService::renewStudentSubscription(const std::string& studentId,
                                                         const std::string& planId,
                                                         const Date& cycleStart,
                                                         const Date& cycleEnd,
                                                         const std::string& adminUsername) {
    compat::LockGuard lock(mutex_);
    auto stuIt = students_.find(studentId);
    if (stuIt == students_.end()) {
        throw MealWalletException("Student not found.", "NOT_FOUND");
    }
    auto pIt = plans_.find(planId);
    if (pIt == plans_.end()) {
        throw MealWalletException("Meal plan not found: " + planId, "INVALID_PLAN");
    }
    if (cycleEnd < cycleStart) {
        throw MealWalletException("Cycle end date cannot precede start date.", "INVALID_DATES");
    }

    auto& stu = stuIt->second;
    stu.subscription().renew(cycleStart, cycleEnd, planId, pIt->second->variant());
    saveUsersAndSubsUnlocked();

    std::string planName = pIt->second->displayName();
    pushNotificationUnlocked(studentId, "Meal Plan Renewed",
                             "Your " + planName + " has been renewed from " +
                             cycleStart.toString() + " to " + cycleEnd.toString() + ".");
    writeAuditUnlocked(adminUsername, "SUBSCRIPTION_RENEWED",
                       "Student=" + stu.username() + " Plan=" + planId +
                       " Period=" + cycleStart.toString() + ".." + cycleEnd.toString());
    return stu.subscription();
}

int MealWalletService::countStudentSkipsInMonth(const std::string& studentId,
                                                const std::string& yearMonth) const {
    compat::LockGuard lock(mutex_);
    return static_cast<int>(std::count_if(
        skipRequests_.begin(), skipRequests_.end(),
        [&](const SingleSkipRequest& r) {
            return r.studentId() == studentId &&
                   r.mealDate().yearMonth() == yearMonth &&
                   r.status() != RequestStatus::CANCELLED &&
                   r.status() != RequestStatus::REJECTED;
        }));
}

SingleSkipRequest MealWalletService::submitSingleSkip(const std::string& studentId,
                                                      const Date& mealDate,
                                                      MealType mealType,
                                                      const std::string& reason) {
    compat::LockGuard lock(mutex_);
    auto it = students_.find(studentId);
    if (it == students_.end()) {
        throw MealWalletException("Student not found: " + studentId, "USER_LOOKUP_RULE");
    }
    const Student& stu = it->second;

    // 1. Check subscription validity
    if (!stu.subscription().coversDate(mealDate)) {
        throw MealWalletException(
            "Meal date " + mealDate.toString() + " is outside your active plan validity (" +
            stu.subscription().startDate().toString() + " to " +
            stu.subscription().endDate().toString() + ").",
            "PLAN_VALIDITY_RULE");
    }

    // 2. Check if mess is already closed for that slot
    for (const auto& cl : closures_) {
        if (cl.closureDate == mealDate && cl.mealType == mealType) {
            throw MealWalletException(
                "Mess is already marked closed on " + mealDate.toString() + " for " +
                toString(mealType) + " (100% auto-credit already applied).",
                "MESS_CLOSURE_OVERLAP_RULE");
        }
    }

    // 3. Check uniqueness: one request per (student, date, meal)
    for (const auto& existing : skipRequests_) {
        if (existing.studentId() == studentId &&
            existing.mealDate() == mealDate &&
            existing.mealType() == mealType &&
            existing.status() != RequestStatus::CANCELLED) {
            throw DuplicateRequestException(stu.username(), mealDate.toString(), toString(mealType));
        }
    }

    // 4. Enforce cutoff rule using polymorphic Clock::now()
    int64_t reqUtc = nowUtc();
    int64_t cutoffUtc = TimeUtils::mealCutoffUtc(mealDate, mealType, Config::cutoffHoursBeforeMeal());
    if (reqUtc > cutoffUtc) {
        throw CutoffPassedException(
            toString(mealType) + " (" + mealDate.toString() + ")",
            TimeUtils::formatIstPretty(cutoffUtc),
            TimeUtils::formatIstPretty(reqUtc));
    }

    // 5. Enforce monthly skip cap (default 10 meals)
    std::string ym = mealDate.yearMonth();
    int usedInMonth = countStudentSkipsInMonth(studentId, ym);
    if (usedInMonth >= Config::monthlySkipCap()) {
        throw SkipLimitExceededException(usedInMonth, Config::monthlySkipCap(), ym);
    }

    std::string reqId = IdGenerator::nextReqId();
    SingleSkipRequest created(
        reqId, studentId, mealDate, mealType,
        reason.empty() ? "Going home / personal work" : reason,
        reqUtc, RequestStatus::PENDING);

    skipRequests_.push_back(created);
    skipRepo_->saveAll(skipRequests_);

    writeAuditUnlocked(stu.username(), "SKIP_RAISED",
                       "Request " + reqId + " for " + mealDate.toString() + " " + toString(mealType));
    return created;
}

std::vector<SingleSkipRequest> MealWalletService::submitMultiDaySkip(
    const std::string& studentId,
    const Date& startDate,
    const Date& endDate,
    const std::vector<MealType>& meals,
    const std::string& reason) {

    compat::LockGuard lock(mutex_);
    if (endDate < startDate) {
        throw MealWalletException("End date cannot be earlier than start date.", "DATE_RANGE_RULE");
    }

    std::string batchId = IdGenerator::nextReqId();
    MultiDaySkipRequest multiReq(batchId, studentId, startDate, endDate, meals, reason, nowUtc());
    // Runtime polymorphism: Request::expandSlots() expands the range into individual slots
    const Request& baseRef = multiReq;
    std::vector<MealSlotSpec> slots = baseRef.expandSlots();

    if (slots.empty()) {
        throw MealWalletException("No meal slots selected in multi-day request.", "DATE_RANGE_RULE");
    }

    // Pre-validate all slots atomically before inserting any slot
    auto it = students_.find(studentId);
    if (it == students_.end()) {
        throw MealWalletException("Student not found: " + studentId, "USER_LOOKUP_RULE");
    }
    const Student& stu = it->second;
    int64_t reqUtc = nowUtc();

    std::map<std::string, int> monthCounts;
    for (const auto& slot : slots) {
        if (!stu.subscription().coversDate(slot.mealDate)) {
            throw MealWalletException(
                "Date " + slot.mealDate.toString() + " is outside your active plan validity (" +
                stu.subscription().startDate().toString() + " to " +
                stu.subscription().endDate().toString() + ").",
                "PLAN_VALIDITY_RULE");
        }
        int64_t cutoffUtc = TimeUtils::mealCutoffUtc(slot.mealDate, slot.mealType, Config::cutoffHoursBeforeMeal());
        if (reqUtc > cutoffUtc) {
            throw CutoffPassedException(
                toString(slot.mealType) + " (" + slot.mealDate.toString() + ")",
                TimeUtils::formatIstPretty(cutoffUtc),
                TimeUtils::formatIstPretty(reqUtc));
        }
        for (const auto& existing : skipRequests_) {
            if (existing.studentId() == studentId &&
                existing.mealDate() == slot.mealDate &&
                existing.mealType() == slot.mealType &&
                existing.status() != RequestStatus::CANCELLED) {
                throw DuplicateRequestException(stu.username(), slot.mealDate.toString(), toString(slot.mealType));
            }
        }
        std::string ym = slot.mealDate.yearMonth();
        if (monthCounts.find(ym) == monthCounts.end()) {
            monthCounts[ym] = countStudentSkipsInMonth(studentId, ym);
        }
        if (monthCounts[ym] + 1 > Config::monthlySkipCap()) {
            throw SkipLimitExceededException(monthCounts[ym], Config::monthlySkipCap(), ym);
        }
        monthCounts[ym] += 1;
    }

    std::vector<SingleSkipRequest> createdSlots;
    for (const auto& slot : slots) {
        // Skip slots where mess is already closed
        bool closed = false;
        for (const auto& cl : closures_) {
            if (cl.closureDate == slot.mealDate && cl.mealType == slot.mealType) {
                closed = true;
                break;
            }
        }
        if (closed) continue;

        std::string slotId = IdGenerator::nextReqId();
        SingleSkipRequest single(
            slotId, studentId, slot.mealDate, slot.mealType,
            reason.empty() ? "Multi-day leave" : reason,
            reqUtc, RequestStatus::PENDING, false, "", "", 0, batchId);
        skipRequests_.push_back(single);
        createdSlots.push_back(single);
    }

    skipRepo_->saveAll(skipRequests_);
    writeAuditUnlocked(stu.username(), "MULTI_SKIP_RAISED",
                       "Batch " + batchId + " expanded into " + std::to_string(createdSlots.size()) + " meal slots");
    return createdSlots;
}

void MealWalletService::cancelPendingSkip(const std::string& studentId, const std::string& requestId) {
    compat::LockGuard lock(mutex_);
    for (auto& req : skipRequests_) {
        if (req.id() == requestId && req.studentId() == studentId) {
            if (req.status() != RequestStatus::PENDING) {
                throw MealWalletException("Only PENDING skip requests can be cancelled.", "CANCEL_STATUS_RULE");
            }
            int64_t cutoffUtc = TimeUtils::mealCutoffUtc(req.mealDate(), req.mealType(), Config::cutoffHoursBeforeMeal());
            if (nowUtc() > cutoffUtc) {
                throw CutoffPassedException(
                    toString(req.mealType()),
                    TimeUtils::formatIstPretty(cutoffUtc),
                    TimeUtils::formatIstPretty(nowUtc()));
            }
            req.setStatus(RequestStatus::CANCELLED, "Cancelled by student before cutoff", nowUtc());
            skipRepo_->saveAll(skipRequests_);
            writeAuditUnlocked(studentId, "SKIP_CANCELLED", "Cancelled request " + requestId);
            return;
        }
    }
    throw MealWalletException("Skip request not found: " + requestId, "LOOKUP_RULE");
}

SingleSkipRequest MealWalletService::appealRejectedSkip(const std::string& studentId,
                                                        const std::string& requestId,
                                                        const std::string& appealReason) {
    compat::LockGuard lock(mutex_);
    for (auto& req : skipRequests_) {
        if (req.id() == requestId && req.studentId() == studentId) {
            if (req.status() != RequestStatus::REJECTED) {
                throw MealWalletException("Only REJECTED skip requests can be appealed.", "APPEAL_RULE");
            }
            if (req.appealed()) {
                throw MealWalletException(
                    "Request " + requestId + " has already been appealed once (Rule: rejected requests can be appealed at most once).",
                    "SINGLE_APPEAL_RULE");
            }
            req.markAppealed(appealReason.empty() ? "Requesting manual re-verification" : appealReason);
            skipRepo_->saveAll(skipRequests_);
            writeAuditUnlocked(studentId, "SKIP_APPEALED",
                               "Appealed request " + requestId + ": " + req.appealReason());
            return req;
        }
    }
    throw MealWalletException("Skip request not found: " + requestId, "LOOKUP_RULE");
}

Money MealWalletService::swapMealUpgrade(const std::string& studentId,
                                         const Date& mealDate,
                                         MealType mealType,
                                         const std::string& targetPlanId) {
    compat::LockGuard lock(mutex_);
    auto sIt = students_.find(studentId);
    if (sIt == students_.end()) throw MealWalletException("Student not found.", "LOOKUP_RULE");
    Student& stu = sIt->second;

    auto curPlanIt = plans_.find(stu.subscription().planId());
    auto tgtPlanIt = plans_.find(targetPlanId);
    if (curPlanIt == plans_.end() || tgtPlanIt == plans_.end()) {
        throw MealWalletException("Invalid meal plan for swap.", "VALID_PLAN_RULE");
    }

    // Runtime polymorphism: MealPlan::mealPrice(mealType) called via base pointers
    Money currentPrice = curPlanIt->second->mealPrice(mealType);
    Money targetPrice  = tgtPlanIt->second->mealPrice(mealType);
    if (targetPrice <= currentPrice) {
        throw MealWalletException(
            "Target meal plan (" + tgtPlanIt->second->displayName() + ") is not higher-priced than your current plan.",
            "MEAL_SWAP_RULE");
    }

    Money diff = targetPrice - currentPrice;
    std::string reason = "Meal swap on " + mealDate.toString() + " (" + toString(mealType) +
                         " upgrade to " + tgtPlanIt->second->displayName() + ")";
    const Transaction& txn = stu.wallet().debit(diff, reason, nowUtc(), "SWAP-" + mealDate.toString());

    PersistedTxnRow row{txn.id(), studentId, txn.type(), txn.amount(), txn.reason(), txn.refId(), txn.timestampUtc()};
    ledgerRows_.push_back(row);
    ledgerRepo_->appendOne(row);

    pushNotificationUnlocked(studentId, "Meal Swap Confirmed",
                             "Debited " + diff.toRupeeString() + " for " + reason);
    writeAuditUnlocked(stu.username(), "MEAL_SWAP_DEBIT",
                       "Debited " + diff.toRupeeString() + " (" + reason + ")");
    return diff;
}

void MealWalletService::requestNextCyclePlanChange(const std::string& studentId,
                                                   const std::string& newPlanId) {
    compat::LockGuard lock(mutex_);
    auto sIt = students_.find(studentId);
    if (sIt == students_.end()) throw MealWalletException("Student not found.", "LOOKUP_RULE");
    if (plans_.find(newPlanId) == plans_.end()) {
        throw MealWalletException("Invalid target plan ID: " + newPlanId, "VALID_PLAN_RULE");
    }
    sIt->second.subscription().scheduleNextCyclePlan(newPlanId);
    saveUsersAndSubsUnlocked();
    pushNotificationUnlocked(studentId, "Next-Cycle Plan Queued",
                             "Your plan change to " + plans_[newPlanId]->displayName() +
                             " will take effect from the next cycle (after " +
                             sIt->second.subscription().endDate().toString() + ").");
    writeAuditUnlocked(sIt->second.username(), "NEXT_CYCLE_PLAN_QUEUED",
                       "Queued plan change to " + newPlanId + " for next cycle");
}

void MealWalletService::setSettlementChoice(const std::string& studentId, SettlementChoice choice) {
    compat::LockGuard lock(mutex_);
    auto sIt = students_.find(studentId);
    if (sIt == students_.end()) throw MealWalletException("Student not found.", "LOOKUP_RULE");
    sIt->second.subscription().setSettlementChoice(choice);
    saveUsersAndSubsUnlocked();
    writeAuditUnlocked(sIt->second.username(), "SETTLEMENT_CHOICE_UPDATED",
                       "Selected month-end option: " + toString(choice));
}

SuggestedVerdict MealWalletService::computeSuggestedVerdict(const SingleSkipRequest& req) const {
    compat::LockGuard lock(mutex_);
    SuggestedVerdict sv;
    int64_t cutoffUtc = TimeUtils::mealCutoffUtc(req.mealDate(), req.mealType(), Config::cutoffHoursBeforeMeal());
    sv.beforeCutoff = (req.createdAtUtc() <= cutoffUtc);

    if (sv.beforeCutoff) {
        sv.reasons.push_back("Submitted before " + std::to_string(Config::cutoffHoursBeforeMeal()) +
                             "h cutoff (" + TimeUtils::formatIstTimeOnly(cutoffUtc) + ")");
    } else {
        sv.reasons.push_back("Submitted AFTER cutoff (" + TimeUtils::formatIstPretty(cutoffUtc) + ")");
    }

    for (const auto& att : attendance_) {
        if (att.studentId == req.studentId() &&
            att.mealDate == req.mealDate() &&
            att.mealType == req.mealType()) {
            sv.seenAtCounter = true;
            sv.reasons.push_back("SUSPICIOUS: Student was logged eating at mess counter by " +
                                 att.markedBy + " at " + TimeUtils::formatIstTimeOnly(att.timestampUtc));
            break;
        }
    }
    if (!sv.seenAtCounter) {
        sv.reasons.push_back("No counter attendance conflict found");
    }

    if (req.appealed()) {
        sv.reasons.push_back("Appealed by student: \"" + req.appealReason() + "\"");
    }

    sv.verdict = (sv.beforeCutoff && !sv.seenAtCounter) ? "APPROVE" : "REJECT";
    return sv;
}

SingleSkipRequest MealWalletService::decideSkipRequest(const std::string& requestId,
                                                       bool approve,
                                                       const std::string& adminNote,
                                                       const std::string& adminUsername) {
    compat::LockGuard lock(mutex_);
    for (auto& req : skipRequests_) {
        if (req.id() != requestId) continue;

        if (req.status() == RequestStatus::APPROVED) {
            throw MealWalletException("Request " + requestId + " is already APPROVED and credited.", "IDEMPOTENCY_RULE");
        }
        if (req.status() == RequestStatus::CANCELLED) {
            throw MealWalletException("Request " + requestId + " was cancelled by the student.", "IDEMPOTENCY_RULE");
        }

        auto sIt = students_.find(req.studentId());
        if (sIt == students_.end()) {
            throw MealWalletException("Student not found for request " + requestId, "LOOKUP_RULE");
        }
        Student& stu = sIt->second;
        int64_t decisionUtc = nowUtc();

        if (approve) {
            req.setStatus(RequestStatus::APPROVED,
                          adminNote.empty() ? "Approved by Admin" : adminNote,
                          decisionUtc);
            const MealPlan* plan = plans_[stu.subscription().planId()].get();
            Money basePrice = plan->mealPrice(req.mealType());

            // Function Overloading + Runtime Polymorphism:
            // Calls Wallet::credit(const SingleSkipRequest&, Money, const RefundPolicy&, int64_t)
            const Transaction& txn = stu.wallet().credit(req, basePrice, *activeRefundPolicy_, decisionUtc);
            PersistedTxnRow row{txn.id(), stu.id(), txn.type(), txn.amount(), txn.reason(), txn.refId(), txn.timestampUtc()};
            ledgerRows_.push_back(row);
            ledgerRepo_->appendOne(row);

            pushNotificationUnlocked(
                stu.id(),
                "Skip Approved & Credited (" + txn.amount().toRupeeString() + ")",
                "Your skip for " + toString(req.mealType()) + " on " + req.mealDate().toString() +
                " was approved. Credited " + txn.amount().toRupeeString() + " to your wallet.");

            writeAuditUnlocked(adminUsername, "SKIP_APPROVED_AND_CREDITED",
                               "Req=" + req.id() + " Student=" + stu.username() +
                               " Credited=" + txn.amount().toRupeeString());
        } else {
            SuggestedVerdict sv = computeSuggestedVerdict(req);
            std::string defaultRejReason = sv.seenAtCounter
                ? "Rejected: Counter attendance log shows meal was consumed at counter."
                : "Rejected by Admin review.";
            req.setStatus(RequestStatus::REJECTED,
                          adminNote.empty() ? defaultRejReason : adminNote,
                          decisionUtc);

            pushNotificationUnlocked(
                stu.id(),
                "Skip Request Rejected",
                "Your skip for " + toString(req.mealType()) + " on " + req.mealDate().toString() +
                " was rejected. Reason: " + req.adminNote() + " (You may appeal once).");

            writeAuditUnlocked(adminUsername, "SKIP_REJECTED",
                               "Req=" + req.id() + " Student=" + stu.username() +
                               " Note=" + req.adminNote());
        }

        skipRepo_->saveAll(skipRequests_);
        return req;
    }
    throw MealWalletException("Skip request not found: " + requestId, "LOOKUP_RULE");
}

AttendanceRecord MealWalletService::markAttendance(const std::string& studentId,
                                                   const Date& mealDate,
                                                   MealType mealType,
                                                   const std::string& markedBy) {
    compat::LockGuard lock(mutex_);
    if (students_.find(studentId) == students_.end()) {
        throw MealWalletException("Student not found: " + studentId, "LOOKUP_RULE");
    }
    for (const auto& existing : attendance_) {
        if (existing.studentId == studentId &&
            existing.mealDate == mealDate &&
            existing.mealType == mealType) {
            return existing; // Already logged
        }
    }
    AttendanceRecord rec;
    rec.id = IdGenerator::nextAttId();
    rec.studentId = studentId;
    rec.mealDate = mealDate;
    rec.mealType = mealType;
    rec.markedBy = markedBy.empty() ? "messstaff" : markedBy;
    rec.timestampUtc = nowUtc();

    attendance_.push_back(rec);
    attendanceRepo_->saveAll(attendance_);

    writeAuditUnlocked(rec.markedBy, "COUNTER_ATTENDANCE_MARKED",
                       "Student=" + students_[studentId].username() + " Date=" +
                       mealDate.toString() + " Meal=" + toString(mealType));
    return rec;
}

ClosureRecord MealWalletService::markMessClosure(const Date& closureDate,
                                                 MealType mealType,
                                                 const std::string& reason,
                                                 const std::string& adminUsername) {
    compat::LockGuard lock(mutex_);
    for (const auto& existing : closures_) {
        if (existing.closureDate == closureDate && existing.mealType == mealType) {
            throw MealWalletException(
                "Mess closure is already recorded for " + closureDate.toString() +
                " " + toString(mealType) + ".",
                "DUPLICATE_CLOSURE_RULE");
        }
    }

    FullRefundPolicy fullClosurePolicy; // Mess closure credits 100% of meal price
    int creditedCount = 0;
    Money totalCredited = Money::fromPaise(0);
    int64_t ts = nowUtc();

    for (auto& kv : students_) {
        const auto& stuId = kv.first;
        auto& stu = kv.second;
        if (!stu.subscription().coversDate(closureDate)) continue;

        // Edge case: Check if student already has an existing skip request on this slot!
        bool alreadyCreditedViaSkip = false;
        for (auto& req : skipRequests_) {
            if (req.studentId() == stuId &&
                req.mealDate() == closureDate &&
                req.mealType() == mealType) {
                if (req.status() == RequestStatus::APPROVED) {
                    alreadyCreditedViaSkip = true;
                    // Top up any difference if regular policy was <100%
                    const MealPlan* plan = plans_[stu.subscription().planId()].get();
                    Money fullPrice = plan->mealPrice(mealType);
                    Money alreadyGiven = activeRefundPolicy_->compute(fullPrice);
                    Money diff = fullPrice - alreadyGiven;
                    if (diff > Money::fromPaise(0)) {
                        const Transaction& topUp = stu.wallet().credit(
                            diff,
                            "Mess closure top-up to 100% for " + toString(mealType) + " on " + closureDate.toString(),
                            ts, "CLS-TOPUP-" + req.id());
                        PersistedTxnRow row{topUp.id(), stuId, topUp.type(), topUp.amount(), topUp.reason(), topUp.refId(), topUp.timestampUtc()};
                        ledgerRows_.push_back(row);
                        ledgerRepo_->appendOne(row);
                        totalCredited += diff;
                        ++creditedCount;
                    }
                } else if (req.status() == RequestStatus::PENDING || req.status() == RequestStatus::APPEALED) {
                    req.setStatus(RequestStatus::APPROVED,
                                  "Auto-approved by Mess Closure (" + reason + ")", ts);
                }
            }
        }
        if (alreadyCreditedViaSkip) continue;

        const MealPlan* plan = plans_[stu.subscription().planId()].get();
        Money mealPrice = fullClosurePolicy.compute(plan->mealPrice(mealType));
        std::string desc = "Mess Closed Auto-Credit (100%): " + toString(mealType) +
                           " on " + closureDate.toString() + " - " +
                           (reason.empty() ? "Maintenance / Holiday" : reason);
        const Transaction& txn = stu.wallet().credit(mealPrice, desc, ts, "CLOSURE-" + closureDate.toString());
        PersistedTxnRow row{txn.id(), stuId, txn.type(), txn.amount(), txn.reason(), txn.refId(), txn.timestampUtc()};
        ledgerRows_.push_back(row);
        ledgerRepo_->appendOne(row);

        totalCredited += mealPrice;
        ++creditedCount;

        pushNotificationUnlocked(stuId, "Mess Closure Credit (" + mealPrice.toRupeeString() + ")", desc);
    }

    ClosureRecord cl;
    cl.id = IdGenerator::nextClosureId();
    cl.closureDate = closureDate;
    cl.mealType = mealType;
    cl.reason = reason.empty() ? "Mess closed by administration" : reason;
    cl.studentsCredited = creditedCount;
    cl.totalCredited = totalCredited;
    cl.createdAtUtc = ts;

    closures_.push_back(cl);
    closureRepo_->saveAll(closures_);
    skipRepo_->saveAll(skipRequests_);

    writeAuditUnlocked(adminUsername, "MESS_CLOSURE_CREDITED",
                       "Date=" + closureDate.toString() + " Meal=" + toString(mealType) +
                       " Students=" + std::to_string(creditedCount) +
                       " Total=" + totalCredited.toRupeeString());
    return cl;
}

std::vector<SettlementRecord> MealWalletService::runMonthlySettlement(const std::string& yearMonth,
                                                                      const std::string& adminUsername) {
    compat::LockGuard lock(mutex_);
    std::string targetYm = yearMonth.empty()
        ? TimeUtils::currentIstDate(nowUtc()).yearMonth()
        : yearMonth;

    std::vector<SettlementRecord> results;
    int64_t ts = nowUtc();

    for (auto& kv : students_) {
        const auto& stuId = kv.first;
        auto& stu = kv.second;
        // Avoid settling the same student twice for the same yearMonth if balance is 0
        if (stu.wallet().balance() <= Money::fromPaise(0)) {
            continue;
        }
        auto res = SettlementEngine::settleStudentMonthEnd(stu, targetYm, ts);
        settlements_.push_back(res.settlement);
        results.push_back(res.settlement);

        if (res.createdPayout) {
            payouts_.push_back(res.payout);
        }
        if (res.ledgerEntry) {
            PersistedTxnRow row{
                res.ledgerEntry->id(),
                stuId,
                res.ledgerEntry->type(),
                res.ledgerEntry->amount(),
                res.ledgerEntry->reason(),
                res.ledgerEntry->refId(),
                res.ledgerEntry->timestampUtc()
            };
            ledgerRows_.push_back(row);
            ledgerRepo_->appendOne(row);
        }

        std::string msg = (res.settlement.choice == SettlementChoice::REFUND)
            ? ("Month-end REFUND processed for " + res.settlement.settledAmount.toRupeeString() +
               " (Payout ID: " + res.settlement.payoutId + "). Admin will mark paid offline.")
            : ("Month-end CARRY_FORWARD applied: " + res.settlement.settledAmount.toRupeeString() +
               " deducted from your next mess plan bill.");
        pushNotificationUnlocked(stuId, "Month-End Settlement (" + targetYm + ")", msg);
    }

    saveUsersAndSubsUnlocked();
    settlementRepo_->saveAll(settlements_);
    payoutRepo_->saveAll(payouts_);

    writeAuditUnlocked(adminUsername, "SETTLEMENT_RUN",
                       "Month=" + targetYm + " SettledCount=" + std::to_string(results.size()));
    return results;
}

PayoutRecord MealWalletService::markPayoutPaid(const std::string& payoutId,
                                               const std::string& note,
                                               const std::string& adminUsername) {
    compat::LockGuard lock(mutex_);
    for (auto& p : payouts_) {
        if (p.id == payoutId) {
            if (p.paidOffline) {
                throw MealWalletException("Payout " + payoutId + " is already marked as paid.", "IDEMPOTENCY_RULE");
            }
            p.paidOffline = true;
            p.paidAtUtc = nowUtc();
            p.paidNote = note.empty() ? "Paid offline via UPI/Cash by Mess Office" : note;
            payoutRepo_->saveAll(payouts_);

            pushNotificationUnlocked(p.studentId, "Refund Payout Completed (" + p.amount.toRupeeString() + ")",
                                     "Payout " + p.id + " marked paid offline: " + p.paidNote);
            writeAuditUnlocked(adminUsername, "PAYOUT_MARKED_PAID",
                               "Payout=" + p.id + " Student=" + p.studentId +
                               " Amount=" + p.amount.toRupeeString());
            return p;
        }
    }
    throw MealWalletException("Payout record not found: " + payoutId, "LOOKUP_RULE");
}

std::vector<MealForecastItem> MealWalletService::getForecastForDate(const Date& targetDate) const {
    compat::LockGuard lock(mutex_);
    std::vector<MealForecastItem> items;
    const MealType meals[3] = {MealType::BREAKFAST, MealType::LUNCH, MealType::DINNER};

    int enrolled = 0;
    for (const auto& kv : students_) {
        const auto& stu = kv.second;
        if (stu.subscription().coversDate(targetDate)) {
            ++enrolled;
        }
    }

    for (MealType m : meals) {
        MealForecastItem item;
        item.date = targetDate;
        item.mealType = m;
        item.totalEnrolled = enrolled;

        for (const auto& cl : closures_) {
            if (cl.closureDate == targetDate && cl.mealType == m) {
                item.messClosed = true;
                break;
            }
        }

        int skips = 0;
        for (const auto& req : skipRequests_) {
            if (req.mealDate() == targetDate &&
                req.mealType() == m &&
                req.status() == RequestStatus::APPROVED) {
                ++skips;
            }
        }
        item.approvedSkips = skips;
        item.expectedHeadcount = item.messClosed ? 0 : std::max(0, enrolled - skips);
        items.push_back(item);
    }
    return items;
}

Student MealWalletService::getStudentById(const std::string& studentId) const {
    compat::LockGuard lock(mutex_);
    auto it = students_.find(studentId);
    if (it == students_.end()) {
        throw MealWalletException("Student not found: " + studentId, "LOOKUP_RULE");
    }
    return it->second;
}

std::vector<Student> MealWalletService::listStudents() const {
    compat::LockGuard lock(mutex_);
    std::vector<Student> out;
    for (const auto& kv : students_) out.push_back(kv.second);
    return out;
}

std::vector<const MealPlan*> MealWalletService::listPlans() const {
    compat::LockGuard lock(mutex_);
    std::vector<const MealPlan*> out;
    for (const auto& kv : plans_) out.push_back(kv.second.get());
    return out;
}

const MealPlan* MealWalletService::getPlanById(const std::string& planId) const {
    compat::LockGuard lock(mutex_);
    auto it = plans_.find(planId);
    return (it != plans_.end()) ? it->second.get() : nullptr;
}

std::vector<SingleSkipRequest> MealWalletService::listSkipRequests(
    const std::string& statusFilter,
    const std::string& studentIdFilter) const {
    compat::LockGuard lock(mutex_);
    std::vector<SingleSkipRequest> out;
    for (const auto& r : skipRequests_) {
        if (!studentIdFilter.empty() && r.studentId() != studentIdFilter) continue;
        if (!statusFilter.empty()) {
            if (statusFilter == "pending" || statusFilter == "PENDING") {
                if (r.status() != RequestStatus::PENDING && r.status() != RequestStatus::APPEALED) continue;
            } else {
                RequestStatus target = requestStatusFromString(statusFilter);
                if (r.status() != target) continue;
            }
        }
        out.push_back(r);
    }
    // Sort newest first using STL std::sort
    std::sort(out.begin(), out.end(), [](const SingleSkipRequest& a, const SingleSkipRequest& b) {
        if (a.createdAtUtc() != b.createdAtUtc()) return a.createdAtUtc() > b.createdAtUtc();
        return a.id() > b.id();
    });
    return out;
}

std::vector<NotificationRecord> MealWalletService::listNotificationsForStudent(
    const std::string& studentId) const {
    compat::LockGuard lock(mutex_);
    std::vector<NotificationRecord> out;
    for (const auto& n : notifications_) {
        if (n.studentId == studentId) out.push_back(n);
    }
    std::sort(out.begin(), out.end(), [](const NotificationRecord& a, const NotificationRecord& b) {
        if (a.createdAtUtc != b.createdAtUtc) return a.createdAtUtc > b.createdAtUtc;
        return a.id > b.id;
    });
    return out;
}

std::vector<SettlementRecord> MealWalletService::listSettlements() const {
    compat::LockGuard lock(mutex_);
    return settlements_;
}

std::vector<PayoutRecord> MealWalletService::listPayouts() const {
    compat::LockGuard lock(mutex_);
    return payouts_;
}

std::vector<ClosureRecord> MealWalletService::listClosures() const {
    compat::LockGuard lock(mutex_);
    return closures_;
}

std::vector<AttendanceRecord> MealWalletService::listAttendance() const {
    compat::LockGuard lock(mutex_);
    return attendance_;
}

std::vector<std::string> MealWalletService::readAuditLogLines(size_t maxLines) const {
    compat::LockGuard lock(mutex_);
    std::vector<std::string> lines;
    std::ifstream in(dataDir_ + "/audit.log");
    if (!in.is_open()) return lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) lines.push_back(line);
    }
    if (lines.size() > maxLines) {
        lines.erase(lines.begin(), lines.end() - static_cast<std::ptrdiff_t>(maxLines));
    }
    std::reverse(lines.begin(), lines.end());
    return lines;
}

std::string MealWalletService::exportBackupDump() const {
    compat::LockGuard lock(mutex_);
    std::ostringstream oss;
    oss << "# ====================================================================\n";
    oss << "# MEAL WALLET FULL TEXT / CSV BACKUP EXPORT\n";
    oss << "# Generated at (UTC): " << TimeUtils::formatUtcIso(nowUtc()) << "\n";
    oss << "# Generated at (IST): " << TimeUtils::formatIstPretty(nowUtc()) << "\n";
    oss << "# ====================================================================\n\n";

    oss << "[STUDENTS]\nid,username,fullName,roomNumber,planId,walletBalancePaise,walletBalanceRupees,settlementChoice\n";
    for (const auto& kv : students_) {
        const auto& stu = kv.second;
        oss << stu.id() << "," << stu.username() << ",\"" << stu.fullName() << "\","
            << stu.roomNumber() << "," << stu.subscription().planId() << ","
            << stu.wallet().balance().paise() << ",\"" << stu.wallet().balance().toRupeeString() << "\","
            << toString(stu.subscription().settlementChoice()) << "\n";
    }

    oss << "\n[SKIP_REQUESTS]\nid,studentId,mealDate,mealType,status,reason,createdAtUtc,adminNote\n";
    for (const auto& r : skipRequests_) {
        oss << r.id() << "," << r.studentId() << "," << r.mealDate().toString() << ","
            << toString(r.mealType()) << "," << toString(r.status()) << ",\""
            << r.reason() << "\"," << r.createdAtUtc() << ",\"" << r.adminNote() << "\"\n";
    }

    oss << "\n[APPEND_ONLY_LEDGER]\nid,studentId,type,amountPaise,reason,refId,timestampUtc\n";
    for (const auto& row : ledgerRows_) {
        oss << row.id << "," << row.studentId << "," << toString(row.type) << ","
            << row.amount.paise() << ",\"" << row.reason << "\"," << row.refId << ","
            << row.timestampUtc << "\n";
    }

    oss << "\n[PAYOUTS]\nid,studentId,settlementId,amountPaise,paidOffline,paidNote\n";
    for (const auto& p : payouts_) {
        oss << p.id << "," << p.studentId << "," << p.settlementId << ","
            << p.amount.paise() << "," << (p.paidOffline ? "true" : "false") << ",\""
            << p.paidNote << "\"\n";
    }
    return oss.str();
}

} // namespace mealwallet

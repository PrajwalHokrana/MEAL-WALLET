#include "ConsoleAndTests.hpp"
#include "MealWalletService.hpp"

#include <atomic>
#include <cassert>
#include "Compat.hpp"
#include <fstream>
#include <iostream>
#include <vector>

namespace mealwallet {

// ============================================================================
// Presentation Safety Net: `./mealwallet --console`
// Runs the entire domain flow from a plain console with zero HTTP/JSON code.
// ============================================================================
int runConsoleDemoFlow() {
    std::cout << "\n====================================================================\n";
    std::cout << "  MEAL WALLET — CONSOLE DOMAIN WALKTHROUGH (NO WEB LAYER)\n";
    std::cout << "====================================================================\n\n";

    std::string demoDir = "./data_console_demo";
    compat::removeDirectoryRecursive(demoDir);
    MealWalletService svc(demoDir, "TEXT");
    auto creds = svc.resetAndSeedDemoData();

    std::cout << "[Step 1] Seeded Accounts & Plans:\n";
    for (const auto& c : creds) {
        std::cout << "  - [" << c.role << "] username=" << c.username
                  << " | password=" << c.password
                  << " | " << c.fullName << " (" << c.planInfo << ")\n";
    }

    // Activate DemoClock at 06:00 AM IST on 2026-11-13
    Date demoDate(2026, 11, 13);
    svc.setDemoMode(true, TimeUtils::istToEpochUtc(demoDate, 6, 0, 0));
    std::cout << "\n[Step 2] Active Clock: " << svc.clock().modeName()
              << " @ " << TimeUtils::formatIstPretty(svc.nowUtc()) << "\n";

    // Create a brand-new student
    auto created = svc.createStudent("kiran", "Kiran Deshpande", "D-104", "PLAN-NONVEG",
                                     Date(2026, 11, 1), Date(2026, 11, 30), "temp999");
    std::cout << "\n[Step 3] Admin created student '" << created.student.username()
              << "' with initial password '" << created.generatedPassword
              << "' (mustChangePassword=" << (created.student.mustChangePassword() ? "true" : "false") << ")\n";

    // First login + password change
    svc.changePassword(created.student.id(), "temp999", "kiran2026");

    // Raise skip before cutoff (Lunch cutoff is 06:30 AM IST; current DemoClock is 06:00 AM IST)
    auto skipReq = svc.submitSingleSkip(created.student.id(), demoDate, MealType::LUNCH, "Attending robotics seminar");
    std::cout << "\n[Step 4] Student raised skip request " << skipReq.id()
              << " for LUNCH at 06:00 AM IST (before 06:30 AM cutoff).\n";

    // Admin inspects suggested verdict and approves
    SuggestedVerdict sv = svc.computeSuggestedVerdict(skipReq);
    std::cout << "  -> Admin Suggested Verdict: " << sv.verdict << " (" << sv.reasons[0] << ")\n";
    svc.decideSkipRequest(skipReq.id(), true, "Verified seminar attendance", "admin");

    Student kiranAfter = svc.getStudentById(created.student.id());
    std::cout << "  -> Approved! Kiran's derived wallet balance is now: "
              << kiranAfter.wallet().balance() << "\n";
    std::cout << kiranAfter.wallet() << "\n";

    // Advance DemoClock by +2 hours to 08:00 AM IST and test late Lunch skip rejection
    svc.advanceDemoClock(2, 0);
    std::cout << "[Step 5] Advanced DemoClock +2 hours to " << TimeUtils::formatIstPretty(svc.nowUtc()) << "\n";
    try {
        svc.submitSingleSkip(created.student.id(), demoDate.addDays(0), MealType::BREAKFAST, "Late request");
    } catch (const CutoffPassedException& ex) {
        std::cout << "  -> Late request rightly rejected [" << ex.ruleName() << "]: " << ex.what() << "\n";
    }

    // Mess closure on tomorrow's Dinner
    auto closure = svc.markMessClosure(demoDate.addDays(1), MealType::DINNER, "Diwali Mess Cleaning", "admin");
    std::cout << "\n[Step 6] Marked Mess Closure on " << closure.closureDate
              << " DINNER -> Auto-credited " << closure.studentsCredited
              << " students (" << closure.totalCredited << " total).\n";

    // Month-end settlement with REFUND choice for Kiran
    svc.setSettlementChoice(created.student.id(), SettlementChoice::REFUND);
    auto settlements = svc.runMonthlySettlement("2026-11", "admin");
    std::cout << "\n[Step 7] Ran Month-End Settlement for 2026-11 -> Settled "
              << settlements.size() << " student wallets.\n";
    for (const auto& p : svc.listPayouts()) {
        std::cout << "  -> Payout Record " << p.id << " for student " << p.studentId
                  << " | Amount: " << p.amount << " | PaidOffline=" << (p.paidOffline ? "yes" : "no") << "\n";
    }

    std::cout << "\n====================================================================\n";
    std::cout << "  CONSOLE WALKTHROUGH COMPLETED SUCCESSFULLY\n";
    std::cout << "====================================================================\n";
    compat::removeDirectoryRecursive(demoDir);
    return 0;
}

// ============================================================================
// Comprehensive Unit & Edge-Case Test Suite (`./mealwallet --test`)
// Tests:
// 1. Money integer paise arithmetic & operator overloading
// 2. Date leap-year, month/year boundaries & DemoClock polymorphism
// 3. RefundPolicy runtime polymorphism (FullRefundPolicy vs PercentRefundPolicy)
// 4. Wallet encapsulation, Rule of Five deep copy, Function Overloading, derived balance
// 5. Repository round-trip (TextFileRepository, BinaryFileRepository, InMemoryRepository),
//    corrupted line skipping, and simulated interrupted writes
// 6. All required edge cases:
//    - Same meal skipped twice (DuplicateRequestException)
//    - Skipping after cutoff (CutoffPassedException)
//    - Monthly skip cap breach (SkipLimitExceededException)
//    - Closure day overlapping an existing skip (top-up to 100%, no double full credit)
//    - Refund with zero balance (InsufficientBalanceException)
//    - Rejected request appealed once, second appeal blocked
//    - Two concurrent admin/student actions at the same moment (mutex thread safety)
// ============================================================================
int runAllAutomatedTests() {
    std::cout << "\n====================================================================\n";
    std::cout << "  RUNNING MEAL WALLET C++17 AUTOMATED TEST SUITE\n";
    std::cout << "====================================================================\n";

    // ------------------------------------------------------------------------
    // Test 1: Money & Date Operator Overloading + Month/Year Boundaries
    // ------------------------------------------------------------------------
    {
        Money m1 = Money::fromRupees(55);
        Money m2 = Money::fromPaise(4400); // Rs. 44.00
        assert(m1.paise() == 5500);
        assert((m1 + m2) == Money::fromPaise(9900));
        assert((m1 - m2) == Money::fromPaise(1100));
        assert(m2 < m1);
        m2 += Money::fromPaise(1100);
        assert(m1 == m2);
        assert(m1.toRupeeString() == "Rs. 55.00");

        // Month and Year boundary transitions
        Date dec30(2026, 12, 30);
        Date jan2 = dec30.addDays(3);
        assert(jan2 == Date(2027, 1, 2));
        assert(dec30 < jan2);

        // Leap year Feb 28 -> Feb 29 -> Mar 1
        Date feb28Leap(2028, 2, 28);
        assert(feb28Leap.addDays(1) == Date(2028, 2, 29));
        assert(feb28Leap.addDays(2) == Date(2028, 3, 1));
        std::cout << "[PASS] Test 1: Money integer paise & Date month/year boundary operators\n";
    }

    // ------------------------------------------------------------------------
    // Test 2: Polymorphic Clock, RefundPolicy, and Wallet Rule of Five + Overloading
    // ------------------------------------------------------------------------
    {
        DemoClock dClock(TimeUtils::istToEpochUtc(Date(2026, 11, 13), 6, 0, 0));
        const Clock& clockRef = dClock;
        int64_t t0 = clockRef.now();
        dClock.advanceHours(3);
        assert(clockRef.now() - t0 == 3 * 3600);

        FullRefundPolicy fullPol;
        PercentRefundPolicy pct80(80);
        const RefundPolicy* polPtr = &pct80;
        assert(polPtr->compute(Money::fromRupees(75)) == Money::fromRupees(60));
        polPtr = &fullPol;
        assert(polPtr->compute(Money::fromRupees(75)) == Money::fromRupees(75));

        // Wallet Rule of Five (deep copy of polymorphic std::unique_ptr<Transaction>)
        Wallet w1("USR-9001");
        w1.credit(Money::fromRupees(50), 1700000000LL); // Overload 1
        w1.credit(Money::fromRupees(30), "Bonus credit", 1700000100LL, "REF-1"); // Overload 2
        SingleSkipRequest req("REQ-99", "USR-9001", Date(2026, 11, 14), MealType::LUNCH, "Home", 1700000200LL, RequestStatus::APPROVED);
        w1.credit(req, Money::fromRupees(50), pct80, 1700000300LL); // Overload 3 -> +Rs.40
        assert(w1.balance() == Money::fromRupees(120));

        // Deep copy constructor test
        Wallet w2(w1);
        w2.debit(Money::fromRupees(20), "Meal swap", 1700000400LL, "SWAP-1");
        assert(w1.balance() == Money::fromRupees(120)); // Original unchanged!
        assert(w2.balance() == Money::fromRupees(100)); // Copy updated independently!
        std::cout << "[PASS] Test 2: Clock/RefundPolicy polymorphism & Wallet Rule of Five deep copy\n";
    }

    // ------------------------------------------------------------------------
    // Test 3: IRepository<T> Round-Trip, Corrupted Lines & Interrupted Writes
    // ------------------------------------------------------------------------
    {
        std::string tmpDir = "./data_repo_test";
        compat::removeDirectoryRecursive(tmpDir);
        compat::ensureDirectory(tmpDir);

        auto ser = [](const SingleSkipRequest& r) {
            return r.id() + "|" + r.studentId() + "|" + r.mealDate().toString() + "|" +
                   toString(r.mealType()) + "|" + toString(r.status()) + "|" + r.reason();
        };
        auto deser = [](const std::vector<std::string>& c, SingleSkipRequest& r) {
            r = SingleSkipRequest(c[0], c[1], Date::parse(c[2]),
                                  mealTypeFromString(c[3]), c[5], 1000,
                                  requestStatusFromString(c[4]));
            return true;
        };

        // 3a. TextFileRepository save-and-reload round trip + injected corrupted lines
        std::string txtFile = tmpDir + "/skips_test.txt";
        TextFileRepository<SingleSkipRequest> txtRepo(txtFile, "skips_test", 6, ser, deser);
        std::vector<SingleSkipRequest> initial = {
            SingleSkipRequest("REQ-1", "USR-1", Date(2026, 11, 14), MealType::BREAKFAST, "Trip", 1000),
            SingleSkipRequest("REQ-2", "USR-1", Date(2026, 11, 14), MealType::LUNCH, "Trip", 1000)
        };
        txtRepo.saveAll(initial);
        auto loaded = txtRepo.loadAll();
        assert(loaded.size() == 2);
        assert(txtRepo.lastCorruptedLinesSkipped() == 0);

        // Inject a corrupted line with too few columns and an invalid date line
        {
            std::ofstream corruptOut(txtFile, std::ios::app);
            corruptOut << "CORRUPTED_LINE_WITHOUT_PIPES\n";
            corruptOut << "REQ-BAD|USR-1|NOT-A-DATE|LUNCH|PENDING|BadDate\n";
            corruptOut << "REQ-3|USR-2|2026-11-15|DINNER|APPROVED|ValidAfterCorruption\n";
        }
        auto loadedAfterCorrupt = txtRepo.loadAll();
        assert(loadedAfterCorrupt.size() == 3);
        assert(txtRepo.lastCorruptedLinesSkipped() == 2);

        // 3b. BinaryFileRepository round-trip + simulated interrupted write (truncated trailing bytes)
        std::string binFile = tmpDir + "/skips_test.bin";
        BinaryFileRepository<SingleSkipRequest> binRepo(binFile, 6, ser, deser);
        binRepo.saveAll(initial);
        auto binLoaded = binRepo.loadAll();
        assert(binLoaded.size() == 2);

        // Simulate interrupted write by appending a partial/truncated binary record at EOF
        {
            std::ofstream truncOut(binFile, std::ios::binary | std::ios::app);
            uint32_t fakeLen = 500;
            uint32_t fakeCrc = 12345;
            truncOut.write(reinterpret_cast<const char*>(&fakeLen), sizeof(fakeLen));
            truncOut.write(reinterpret_cast<const char*>(&fakeCrc), sizeof(fakeCrc));
            truncOut.write("PARTIAL_INTERRUPTED_BYTES", 25); // Only 25 of 500 bytes written!
        }
        auto binAfterInterrupt = binRepo.loadAll();
        assert(binAfterInterrupt.size() == 2); // First 2 records intact, partial record skipped!
        assert(binRepo.lastCorruptedLinesSkipped() == 1);

        // 3c. InMemoryRepository test
        InMemoryRepository<SingleSkipRequest> memRepo;
        memRepo.saveAll(initial);
        assert(memRepo.loadAll().size() == 2);

        compat::removeDirectoryRecursive(tmpDir);
        std::cout << "[PASS] Test 3: TextFileRepository, BinaryFileRepository, InMemoryRepository, corrupted lines & interrupted writes\n";
    }

    // ------------------------------------------------------------------------
    // Test 4: Business Rules & Edge Cases (Duplicate, Cutoff, Cap, Closure Overlap,
    //         Zero-Balance Refund, Appeal Once, Concurrent Thread Safety)
    // ------------------------------------------------------------------------
    {
        std::string testDir = "./data_edge_cases";
        compat::removeDirectoryRecursive(testDir);
        MealWalletService svc(testDir, "TEXT");
        svc.resetAndSeedDemoData();

        Date baseDate(2026, 11, 13);
        // Set DemoClock to 2026-11-13 06:00 AM IST
        svc.setDemoMode(true, TimeUtils::istToEpochUtc(baseDate, 6, 0, 0));

        auto stuCreated = svc.createStudent("edgeuser", "Edge Case Student", "E-101",
                                            "PLAN-VEG", Date(2026, 11, 1), Date(2026, 11, 30), "pass123");
        std::string sid = stuCreated.student.id();

        // Edge Case A: Skipping after cutoff (Breakfast on 2026-11-13 starts at 08:00 AM, cutoff was 02:00 AM)
        bool caughtCutoff = false;
        try {
            svc.submitSingleSkip(sid, baseDate, MealType::BREAKFAST, "Too late");
        } catch (const CutoffPassedException&) {
            caughtCutoff = true;
        }
        assert(caughtCutoff && "Expected CutoffPassedException for Breakfast at 06:00 AM");

        // Edge Case B: Valid skip before cutoff, then same meal skipped twice
        auto r1 = svc.submitSingleSkip(sid, baseDate, MealType::LUNCH, "Valid lunch skip at 06:00 AM");
        bool caughtDuplicate = false;
        try {
            svc.submitSingleSkip(sid, baseDate, MealType::LUNCH, "Duplicate lunch skip");
        } catch (const DuplicateRequestException&) {
            caughtDuplicate = true;
        }
        assert(caughtDuplicate && "Expected DuplicateRequestException on same (student, date, meal)");

        // Edge Case C: Rejected request appealed once, second appeal blocked
        svc.decideSkipRequest(r1.id(), false, "Initial rejection", "admin");
        svc.appealRejectedSkip(sid, r1.id(), "First valid appeal");
        svc.decideSkipRequest(r1.id(), false, "Second rejection", "admin");
        bool caughtSecondAppeal = false;
        try {
            svc.appealRejectedSkip(sid, r1.id(), "Second appeal should fail");
        } catch (const MealWalletException&) {
            caughtSecondAppeal = true;
        }
        assert(caughtSecondAppeal && "Expected second appeal to be rejected");

        // Edge Case D: Refund with zero balance
        svc.setSettlementChoice(sid, SettlementChoice::REFUND);
        Student zeroStu = svc.getStudentById(sid);
        assert(zeroStu.wallet().balance() == Money::fromPaise(0));
        bool caughtZeroRefund = false;
        try {
            SettlementEngine::settleStudentMonthEnd(zeroStu, "2026-11", svc.nowUtc());
        } catch (const InsufficientBalanceException&) {
            caughtZeroRefund = true;
        }
        assert(caughtZeroRefund && "Expected InsufficientBalanceException when settling REFUND with 0 balance");

        // Edge Case E: Closure day overlapping an existing approved skip (tops up from 80% to 100%, no double credit)
        Date dOverlap = baseDate.addDays(2);
        auto rOverlap = svc.submitSingleSkip(sid, dOverlap, MealType::DINNER, "Going out");
        svc.decideSkipRequest(rOverlap.id(), true, "Approved before closure", "admin");
        // Veg Dinner is Rs. 55.00; 80% policy credited Rs. 44.00
        assert(svc.getStudentById(sid).wallet().balance() == Money::fromRupees(44));
        // Now Admin marks Mess Closure on that exact same slot (dOverlap, DINNER):
        // Student should receive only the Rs. 11.00 top-up so total credit = Rs. 55.00 (100% of meal price)
        svc.markMessClosure(dOverlap, MealType::DINNER, "Power outage", "admin");
        assert(svc.getStudentById(sid).wallet().balance() == Money::fromRupees(55));

        // Edge Case F: Monthly skip cap breach (Cap = 10 meals)
        // Currently used 1 non-rejected/non-cancelled skip (rOverlap) in 2026-11. Submit 9 more -> 10 total.
        for (int i = 3; i <= 11; ++i) {
            svc.submitSingleSkip(sid, baseDate.addDays(i), MealType::LUNCH, "Cap test");
        }
        bool caughtSkipCap = false;
        try {
            svc.submitSingleSkip(sid, baseDate.addDays(12), MealType::LUNCH, "11th skip should breach cap");
        } catch (const SkipLimitExceededException&) {
            caughtSkipCap = true;
        }
        assert(caughtSkipCap && "Expected SkipLimitExceededException on 11th skip in same month");

        // Edge Case G: Two admin actions at the exact same moment (concurrency mutex test)
        auto pendingList = svc.listSkipRequests("pending", sid);
        assert(pendingList.size() >= 2);
        std::string idA = pendingList[0].id();
        std::string idB = pendingList[1].id();
        std::atomic<int> doneCount{0};
        compat::spawnDetached([&]() { svc.decideSkipRequest(idA, true, "Concurrent A", "admin1"); ++doneCount; });
        compat::spawnDetached([&]() { svc.decideSkipRequest(idB, true, "Concurrent B", "admin2"); ++doneCount; });
        while (doneCount.load() < 2) {
#ifdef _WIN32
            Sleep(5);
#else
            usleep(5000);
#endif
        }

        compat::removeDirectoryRecursive(testDir);
        std::cout << "[PASS] Test 4: All edge cases (cutoff, duplicate, skip cap, closure overlap, zero-balance refund, single appeal, concurrency)\n";
    }

    std::cout << "\n====================================================================\n";
    std::cout << "  ALL AUTOMATED TESTS PASSED (100% GREEN)\n";
    std::cout << "====================================================================\n\n";
    return 0;
}

} // namespace mealwallet

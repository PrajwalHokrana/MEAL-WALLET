# DESIGN.md — Architectural Decisions, OOP Viva Defense & Data File Specifications

This document explains every architectural and Object-Oriented Programming (OOP) decision in **Meal Wallet** in clear language that a 3rd-semester Computer Science student can defend during an oral viva examination, followed by the exact specification of every persisted file in `DATA_DIR`.

---

## Part 1: Defending Our OOP Decisions in a Viva

### 1. Why is Money an integer (`int64_t paise_`) inside a `Money` class instead of `float` or `double`?
- **Viva Answer:** Binary floating-point types (`float` and `double`) cannot represent base-10 fractions like `0.10` or `0.05` exactly in IEEE-754 binary, causing cumulative rounding drift across wallet credits and month-end settlements. By encapsulating `int64_t paise_` (where Rs. 1.00 = 100 paise) inside the `Money` class and overloading `+`, `-`, `+=`, `<`, `==`, and `<<`, all currency calculations are 100% exact integer arithmetic while still reading naturally in domain code.

### 2. Why is the Wallet balance derived from an append-only ledger instead of storing a `int64_t balance_` variable?
- **Viva Answer:** If a system updates a single `balance_` number in place, a crash or bug makes it impossible to audit how that number was reached. In our `Wallet` class (`include/Wallet.hpp`), `ledger_` is a `private` vector of polymorphic `std::unique_ptr<Transaction>` objects (`CreditTxn`, `DebitTxn`, `PayoutTxn`). Calling `wallet.balance()` uses `std::accumulate` to sum `txn->signedAmount()` polymorphically across all transactions. Because `CreditTxn::signedAmount()` returns `+amount` and `DebitTxn`/`PayoutTxn` return `-amount`, the balance is always mathematically guaranteed to match the audit trail in `ledger.txt`.

### 3. Why did we implement the Rule of Five on `Wallet`?
- **Viva Answer:** `Wallet` owns a `std::vector<std::unique_ptr<Transaction>>`. Because `std::unique_ptr` is move-only, the compiler deletes `Wallet`'s copy constructor and copy assignment operator by default. Yet `MealWalletService::getStudentById()` returns a value copy of `Student` (which contains `Wallet` by composition) so readers can inspect student state safely outside the mutex. We therefore implemented a custom deep-copy constructor and copy-assignment operator on `Wallet` that calls the virtual `txn->clone()` method (Prototype pattern) on each transaction, alongside move constructor, move assignment, and destructor.

### 4. Why did we use an abstract `Clock` (`SystemClock` vs. `DemoClock`)?
- **Viva Answer:** If business logic called `std::time(nullptr)` directly inside `submitSingleSkip()`, we could never test a 6-hour cutoff breach or a month-end settlement during a 7-minute live classroom presentation without waiting hours or changing the laptop's OS clock. By injecting a base pointer `Clock* activeClock_`, the domain calls `activeClock_->now()`. In normal operation it points to `SystemClock`; during the live demo or unit tests, the Admin switches it to `DemoClock` and advances time by +1 hour or +1 day with one click.

### 5. Why did we use `friend class SettlementEngine` and `friend operator<<`?
- **Viva Answer:** We intentionally kept `Wallet::ledger_` strictly `private` so external code cannot tamper with historical transactions. However:
  1. `std::ostream& operator<<(std::ostream&, const Wallet&)` must take `std::ostream&` as its left-hand parameter, so it cannot be a member function of `Wallet`. Making it a `friend` lets it print ledger statements cleanly.
  2. `SettlementEngine` performs atomic month-end settlement (creating `PayoutTxn` or `DebitTxn` records and rolling the subscription cycle). Granting `friend class SettlementEngine` allows it to inspect and append settlement entries directly without exposing public low-level ledger mutators to the rest of the application.

### 6. How does `IRepository<T>` achieve storage independence?
- **Viva Answer:** `IRepository<T>` is a C++ class template with pure virtual methods `loadAll()`, `saveAll()`, and `appendOne()`. We provide three concrete template implementations:
  1. `TextFileRepository<T>`: writes human-readable pipe-delimited files using **write-to-temp-then-rename** (`<file>.tmp` -> `<file>`) for atomic updates, plus immediate `std::flush` on append-only files (`ledger.txt`), and skips/logs corrupted lines on startup instead of crashing.
  2. `BinaryFileRepository<T>`: writes a `MWBIN_V1` magic header plus length-prefixed, FNV-1a checksummed binary frames, detecting and skipping truncated records from interrupted writes.
  3. `InMemoryRepository<T>`: stores entities in a `std::vector<T>` in RAM for fast unit tests.
  All writes across the service are serialized by a single `std::recursive_mutex`.

---

## Part 2: Complete Data File Format Specifications (`DATA_DIR`)

All text files in `DATA_DIR` (default `./data`) use:
- **Encoding:** UTF-8 plain text, one record per line (`\n`)
- **Delimiter:** Pipe character (`|`). Any `|` or newline in user input is sanitized by `escapeField()` before writing.
- **Version Header:** Line 1 of every managed repository file is `#MEALWALLET_V1|<table_name>`. Any line starting with `#` is ignored on load.
- **Money Format:** Stored strictly as integer **paise** (e.g., `5500` = Rs. 55.00).
- **Timestamp Format:** Stored strictly in **UTC epoch seconds** (`int64_t`) and/or ISO-8601 UTC (`YYYY-MM-DDTHH:MM:SSZ`), and converted to `Asia/Kolkata` (`IST = UTC + 05:30`) for display.

### 1. `students.txt` (All User Accounts: Student, Admin, MessStaff)
- **Header:** `#MEALWALLET_V1|students`
- **Fields (7):** `id|role|username|fullName|extraField|password|mustChangePassword`
  - `extraField` stores `roomNumber` for `STUDENT`, `title` for `ADMIN`, and `shift` for `MESS_STAFF`.
- **Example Line:**
  ```text
  USR-0003|STUDENT|bt25cse147|Prajwal Hokrana|A-514|student123|0
  ```

### 2. `plans.txt` (Meal Plans & Per-Meal Prices in Paise)
- **Header:** `#MEALWALLET_V1|plans`
- **Fields (6):** `id|variant|name|breakfastPaise|lunchPaise|dinnerPaise`
- **Example Line:**
  ```text
  PLAN-VEG|VEG|Standard Veg Plan|3500|5500|5500
  ```

### 3. `subscriptions.txt` (Student Plan Cycles & Month-End Preferences)
- **Header:** `#MEALWALLET_V1|subscriptions`
- **Fields (10):** `id|studentId|planId|variant|startDate|endDate|settlementChoice|nextCyclePlanId|carryForwardPaise|active`
- **Example Line:**
  ```text
  SUB-0001|USR-0003|PLAN-VEG|VEG|2026-11-01|2026-11-30|CARRY_FORWARD||0|1
  ```

### 4. `skip_requests.txt` (Individual Meal Skip Requests)
- **Header:** `#MEALWALLET_V1|skip_requests`
- **Fields (12):** `id|studentId|mealDate|mealType|status|reason|createdAtUtc|appealed|appealReason|adminNote|decidedAtUtc|batchGroupId`
- **Example Line:**
  ```text
  REQ-0001|USR-0003|2026-11-10|LUNCH|APPROVED|Department lab workshop|1794280200|0||Approved: submitted before 6h cutoff & not in counter log|1794309000|
  ```

### 5. `ledger.txt` (Append-Only Virtual Wallet Ledger)
- **Header:** `#MEALWALLET_V1|ledger`
- **Write Mode:** Append-only (`std::ios::app` with immediate `out.flush()`)
- **Fields (8):** `id|studentId|type|amountPaise|reason|refId|timestampUtc|isoUtc`
- **Example Line:**
  ```text
  TXN-0001|USR-0003|CREDIT|4400|Approved skip refund: LUNCH on 2026-11-10 (80% of Rs. 55.00)|REQ-0001|1794309000|2026-11-10T10:30:00Z
  ```

### 6. `settlements.txt` (Month-End Settlement Records)
- **Header:** `#MEALWALLET_V1|settlements`
- **Fields (7):** `id|studentId|yearMonth|choice|settledPaise|payoutId|settledAtUtc`
- **Example Line:**
  ```text
  STL-0001|USR-0004|2026-11|REFUND|12000|PAY-0001|1796056200
  ```

### 7. `payouts.txt` (Offline Refund Payout Records)
- **Header:** `#MEALWALLET_V1|payouts`
- **Fields (8):** `id|studentId|settlementId|amountPaise|paidOffline|createdAtUtc|paidAtUtc|paidNote`
- **Example Line:**
  ```text
  PAY-0001|USR-0004|STL-0001|12000|1|1796056200|1796059800|Paid offline via UPI by Admin
  ```

### 8. `closures.txt` (Mess-Closed Meals & Auto-Credits)
- **Header:** `#MEALWALLET_V1|closures`
- **Fields (7):** `id|closureDate|mealType|reason|studentsCredited|totalCreditedPaise|createdAtUtc`
- **Example Line:**
  ```text
  CLS-0001|2026-11-14|DINNER|Diwali Mess Cleaning|5|35500|1794580200
  ```

### 9. `attendance.txt` (Mess Counter Attendance Log for Fraud Detection)
- **Header:** `#MEALWALLET_V1|attendance`
- **Fields (6):** `id|studentId|mealDate|mealType|markedBy|timestampUtc`
- **Example Line:**
  ```text
  ATT-0001|USR-0006|2026-11-13|BREAKFAST|messstaff|1794537900
  ```

### 10. `notifications.txt` (Student Inbox Notifications)
- **Header:** `#MEALWALLET_V1|notifications`
- **Fields (6):** `id|studentId|title|message|isRead|createdAtUtc`
- **Example Line:**
  ```text
  NTF-0001|USR-0003|Skip Approved & Credited (Rs. 44.00)|Your skip for LUNCH on 2026-11-10 was approved. Credited Rs. 44.00 to your wallet.|0|1794309000
  ```

### 11. `sessions.txt` (Token-Based Auth Sessions)
- **Header:** `#MEALWALLET_V1|sessions`
- **Fields (6):** `token|userId|username|role|createdAtUtc|expiresAtUtc`
- **Example Line:**
  ```text
  9a8b7c6d5e4f3a2b1c0d9e8f7a6b5c4d3e2f1a0b|USR-0001|admin|ADMIN|1794580000|1795184800
  ```

### 12. `config.txt` & `audit.log`
- **`config.txt`:** Key-value pairs (`CUTOFF_HOURS|6`, `MONTHLY_SKIP_CAP|10`, `REFUND_POLICY_TYPE|PERCENT`, `REFUND_PERCENT|80`, `DEMO_MODE_ENABLED|0`, `DEMO_CLOCK_UTC|...`).
- **`audit.log`:** Append-only human-readable log line written on every state transition:
  ```text
  2026-11-13T00:30:00Z | 2026-11-13 06:00 AM IST | Actor=admin | Action=SKIP_APPROVED_AND_CREDITED | Req=REQ-0010 Student=kiran Credited=Rs. 60.00
  ```

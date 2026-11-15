# Meal Wallet — C++17 Object-Oriented Hostel Mess Refund System

**Meal Wallet** is a 3rd-semester Computer Science Object-Oriented Programming (OOP) course project written in standard **C++17** with zero external database dependencies. It is engineered around one core idea: **fairly refunding missed hostel meals** while giving mess staff accurate next-day headcount forecasts to prevent food waste.

---

## 0. Running Directly in VS Code on Windows 

### Method 1: One-Click Run in VS Code (Fastest)
1. Unzip `meal-wallet-vscode-windows.zip` and open the `meal-wallet-project` folder in **VS Code** (`File -> Open Folder...`).
2. Press **`Ctrl + Shift + B`** (or open the Command Palette `Ctrl + Shift + P` -> `Tasks: Run Task` -> **`2. Build & Start Web Server (Open http://localhost:8080)`**).
3. VS Code will compile all 5 `.cpp` files with `g++` into `mealwallet.exe`, automatically open **`http://localhost:8080`** in your browser, and start the live server!
   *(If you use the **Code Runner** extension play button `▷` in the top-right of VS Code, `.vscode/settings.json` is already configured to compile all 5 source files and link `-lws2_32` automatically.)*

### Method 2: Double-Click `run_windows.bat` (or Run in PowerShell / CMD)
In the VS Code PowerShell/CMD terminal (or by double-clicking in Windows File Explorer), run:
```powershell
.\run_windows.bat
```
Or run specific modes from PowerShell/CMD:
```powershell
.\run_windows.bat --test        # Run automated unit & edge-case test suite
.\run_windows.bat --console     # Run pure console domain demo (no web layer)
.\run_windows.bat --seed-demo   # Reset data folder & print demo credentials
```

---

## 1. Quick Start — Build & Run Instructions

### Prerequisites
- A C++17 compiler (`g++ >= 9`, `clang++ >= 10`, or MSVC 2019+)
- `cmake >= 3.14` **or** `make` (both `CMakeLists.txt` and `Makefile` are included; `third_party/httplib.h` and `third_party/nlohmann/json.hpp` are bundled inside `third_party/` so no package installation is needed)

### Option A: Build with CMake (Linux / WSL / macOS / Windows)
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
./build/mealwallet
```

### Option B: Build with Make / Direct `g++` (Fastest in Terminal / WSL)
```bash
make
./mealwallet
```
Or compile directly with a single `g++` command:
```bash
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -pthread -Iinclude -Ithird_party \
    src/main.cpp src/DomainImpl.cpp src/ConsoleAndTests.cpp \
    src/repository/MealWalletService.cpp src/api/ApiServer.cpp \
    -o mealwallet -pthread
```

---

## 2. CLI Modes & Presentation Safety Nets

| Command | Purpose |
| :--- | :--- |
| `./mealwallet` | Starts the HTTP server on `0.0.0.0:8080` (or `$PORT`), auto-creates `./data` if missing, seeds demo accounts if empty, and prints both the Local URL and LAN/Hotspot URL. |
| `./mealwallet --seed-demo` | Resets `DATA_DIR` to the clean demo seed state (1 Admin, 1 Mess Staff, 5 Students across Veg/Non-Veg/Premium plans, past ledger credits, 1 fraud case, and 1 student near month-end) and prints the credential table. |
| `./mealwallet --console` | Runs the complete end-to-end domain flow in the terminal **with zero web/HTTP layer** as a live presentation fallback. |
| `./mealwallet --test` | Runs the automated unit & edge-case test suite (`Money`, `Date` leap-year/month-end boundaries, `DemoClock`, `RefundPolicy`, `Wallet` Rule of Five, `TextFileRepository` / `BinaryFileRepository` / `InMemoryRepository` round-trips, corrupted line recovery, simulated interrupted writes, cutoff breaches, duplicate requests, skip cap, closure overlap, zero-balance refund, and thread-safe concurrency). |

---

## 3. Demo Credentials (Printed by `./mealwallet --seed-demo`)

| Role | Username | Password | Full Name | Plan / Viva Demo Purpose |
| :--- | :--- | :--- | :--- | :--- |
| **ADMIN** | `admin` | `admin123` | Dr. Rajesh Sharma (Chief Warden) | Full Admin Console, Demo Clock, Approvals, Settlements |
| **MESS_STAFF** | `messstaff` | `staff123` | Ramesh Kulkarni (Mess Counter) | Counter Attendance Marker & Headcount Forecast |
| **STUDENT** | `aarav` | `student123` | Aarav Deshmukh | `PLAN-VEG` (Standard Veg Plan, clean pending skip) |
| **STUDENT** | `priya` | `student123` | Priya Nair | `PLAN-NONVEG` (Protein Non-Veg Plan, `REFUND` choice) |
| **STUDENT** | `rohan` | `student123` | Rohan Verma | `PLAN-PREMIUM` (Executive Premium, **Near Month-End**) |
| **STUDENT** | `sneha` | `student123` | Sneha Joshi | `PLAN-VEG` (**Fraud Case**: skip raised + seen at counter) |
| **STUDENT** | `vikram` | `firstlogin` | Vikram Patil | `PLAN-NONVEG` (**Forces Password Change on First Login**) |

---

## 4. Text Class Diagram (Architecture Overview)

```text
+-----------------------------------------------------------------------------------+
|                                ABSTRACT INTERFACES                                |
+-----------------------------------------------------------------------------------+
  [User] (abstract)                 [MealPlan] (abstract)         [Clock] (abstract)
   |-- role() = 0                    |-- mealPrice(MealType) = 0   |-- now() = 0
   |-- dashboardRoute() = 0          |-- variant() = 0             |
   +--> Student (HAS-A Wallet,       +--> VegPlan                  +--> SystemClock
   |             HAS-A Subscription) +--> NonVegPlan               +--> DemoClock
   +--> Admin                        +--> PremiumPlan
   +--> MessStaff

  [Request] (abstract)              [Transaction] (abstract)      [RefundPolicy] (abstract)
   |-- expandSlots() = 0             |-- signedAmount() = 0        |-- compute(Money) = 0
   |-- requestKind() = 0             |-- type() = 0                |
   +--> SingleSkipRequest            +--> CreditTxn (+amount)      +--> FullRefundPolicy (100%)
   +--> MultiDaySkipRequest          +--> DebitTxn  (-amount)      +--> PercentRefundPolicy (80%)
                                     +--> PayoutTxn (-amount)

  [IRepository<T>] (template abstract interface)
   |-- loadAll() = 0, saveAll() = 0, appendOne() = 0
   +--> TextFileRepository<T>   (pipe-delimited + write-to-temp-then-rename + skip corrupted lines)
   +--> BinaryFileRepository<T> (magic header MWBIN_V1 + FNV-1a checksum + truncated EOF recovery)
   +--> InMemoryRepository<T>   (RAM-only std::vector<T> for unit tests)

+-----------------------------------------------------------------------------------+
|                       VALUE OBJECTS, COMPOSITION & FRIENDS                        |
+-----------------------------------------------------------------------------------+
  [Money] (int64_t paise_; operators +, -, +=, <, ==, friend operator<<)
  [Date]  (YYYY-MM-DD; leap-year aware; operators ==, <, <=, >, >=)
  [Wallet]
   |-- private: std::vector<std::unique_ptr<Transaction>> ledger_ (NO stored balance!)
   |-- Rule of Five: deep-cloning Copy Ctor, Copy Assignment, Move Ctor, Move Assign, Dtor
   |-- Function Overloading: 3 overloads of credit(...)
   |-- friend class SettlementEngine
   |-- friend std::ostream& operator<<(std::ostream&, const Wallet&)
```

---

## 5. Viva Cheat Sheet — Where Every OOP Concept Lives

| OOP Requirement | Exact File & Line / Class | How to Explain It in Your Viva |
| :--- | :--- | :--- |
| **1. Abstraction** | `include/User.hpp` (`User`), `include/MealPlan.hpp` (`MealPlan`), `include/Request.hpp` (`Request`), `include/Transaction.hpp` (`Transaction`), `include/RefundPolicy.hpp` (`RefundPolicy`), `include/DateTime.hpp` (`Clock`), `include/IRepository.hpp` (`IRepository<T>`) | Every core concept is modeled as an abstract base class with pure virtual functions (`= 0`) and a `virtual ~ClassName() = default` destructor so derived objects can be safely deleted through base pointers. |
| **2. Inheritance** | `Student`/`Admin`/`MessStaff` : `User`<br>`VegPlan`/`NonVegPlan`/`PremiumPlan` : `MealPlan`<br>`SingleSkipRequest`/`MultiDaySkipRequest` : `Request`<br>`CreditTxn`/`DebitTxn`/`PayoutTxn` : `Transaction`<br>`FullRefundPolicy`/`PercentRefundPolicy` : `RefundPolicy`<br>`SystemClock`/`DemoClock` : `Clock` | Models true **IS-A** relationships. For example, `CreditTxn` IS-A `Transaction`, and `DemoClock` IS-A `Clock`. |
| **3. Runtime Polymorphism** | `MealPlan::mealPrice()` (`MealWalletService.cpp`)<br>`Transaction::signedAmount()` (`Wallet.hpp`)<br>`RefundPolicy::compute()` (`DomainImpl.cpp`)<br>`Clock::now()` (`MealWalletService.cpp`)<br>`Request::expandSlots()` (`MealWalletService.cpp`) | Virtual dispatch happens at runtime through base pointers/references (`activeClock_->now()`, `activeRefundPolicy_->compute()`, `txn->signedAmount()`). Switching from `SystemClock` to `DemoClock` changes live cutoff behavior without touching a single line of business logic. |
| **4. Encapsulation** | `include/Wallet.hpp` (`Wallet::ledger_`) | `ledger_` is `private`. There is no mutable `balance_` field anywhere in `Wallet`. Balance is derived by summing `txn->signedAmount()` over the append-only ledger, and only `credit()`/`debit()` can append transactions. |
| **5. `friend` Keyword** | `include/Wallet.hpp` (`friend class SettlementEngine` and `friend operator<<`) | Used sparingly in two justified places: `operator<<` needs stream formatting access, and `SettlementEngine` needs atomic month-end access to append `PayoutTxn`/`DebitTxn` entries without exposing raw ledger mutators to public callers. |
| **6. Function Overloading** | `include/Wallet.hpp` & `src/DomainImpl.cpp` (`Wallet::credit` 3 overloads) | `credit(Money)`, `credit(Money, reason, ts, refId)`, and `credit(const SingleSkipRequest&, Money, const RefundPolicy&, ts)` provide compile-time polymorphism for different callers. |
| **7. Operator Overloading** | `include/Money.hpp` (`+`, `-`, `+=`, `<`, `==`, `<<`) & `include/DateTime.hpp` (`Date` `<`, `<=`, `>`, `>=`, `==`, `!=`) | Makes domain expressions read like natural math (`targetPrice - currentPrice`, `mealDate >= startDate`) while keeping paise integer safety. |
| **8. Rule of Three / Five** | `include/Wallet.hpp` (`Wallet` Copy Ctor, Copy Assignment, Move Ctor, Move Assignment, Destructor) | Because `Wallet` owns `std::vector<std::unique_ptr<Transaction>>`, a shallow copy is illegal. Our copy constructor and copy assignment operator deep-clone each polymorphic transaction via `txn->clone()`. |
| **9. Static Members** | `include/Config.hpp` & `src/DomainImpl.cpp` (`IdGenerator` static sequence counters & `Config` class) | Static members belong to the class itself rather than any single instance, ensuring process-wide unique IDs (`REQ-0001`, `TXN-0001`) and shared rule parameters. |
| **10. Templates** | `include/IRepository.hpp` (`IRepository<T>`, `TextFileRepository<T>`, `BinaryFileRepository<T>`, `InMemoryRepository<T>`) | One generic template class persists 11 different entity types (`Student`, `Subscription`, `SingleSkipRequest`, `PersistedTxnRow`, etc.) without duplicating file I/O logic. |
| **11. Custom Exceptions** | `include/Exceptions.hpp` (`MealWalletException`, `CutoffPassedException`, `DuplicateRequestException`, `SkipLimitExceededException`, `InsufficientBalanceException`) | Inherits from `std::runtime_error` and carries the violated rule identifier (`CUTOFF_WINDOW_RULE`, `UNIQUE_SLOT_RULE`, etc.) so both console and HTTP layers report exact rule violations. |
| **12. Composition** | `include/User.hpp` (`Student` HAS-A `Wallet` and `Subscription`) | A `Student` owns a `Wallet` and a `Subscription` by value; their lifetimes are bound to the `Student` instance. |
| **13. `enum class`** | `include/Enums.hpp` (`MealType`, `RequestStatus`, `TxnType`, `UserRole`, `PlanVariant`, `SettlementChoice`) | Strongly-typed scoped enums prevent accidental comparisons like `MealType::LUNCH == RequestStatus::APPROVED`. |
| **14. STL Containers & Algorithms** | `std::map`, `std::vector`, `std::unordered_set`, `std::accumulate` (`Wallet.hpp`), `std::count_if` & `std::sort` (`MealWalletService.cpp`) | Used meaningfully for derived wallet balance summation, monthly skip cap counting, and chronological sorting. |

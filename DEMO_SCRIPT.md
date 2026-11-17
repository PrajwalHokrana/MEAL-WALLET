# DEMO_SCRIPT.md — 7-Minute Two-Laptop Live Presentation Walkthrough

**Setup Before Calling the Teacher Over (30 seconds):**
- **Laptop A (Admin Screen):** Open the public URL (or `http://localhost:8080`), sign in as **`admin` / `admin123`**. Keep the C++ terminal visible on half the screen so the teacher can watch live `[MEAL-WALLET EVENT]` log lines appear.
- **Laptop B (Student Screen):** Open the same URL on the login page (`/`).

---

## Minute 0:00 – 1:00 | Step 1: Admin Creates a Student Account & First-Login Password Change
1. **On Laptop A (Admin Console):**
   - Scroll to **"1. Create Student Account"**.
   - Enter Username: `prajwal`, Full Name: `Prajwal Hokrana`, Room: `B-302`, Plan: `Protein Non-Veg Plan (B:45, L:75, D:75)`.
   - Click **"Create & Generate Credentials"**.
   - **Expected Result:** Blue box displays `Username: prajwal` and a generated temporary password like `MW-8K4P2X` (and the C++ terminal prints `Action=STUDENT_CREATED`).
2. **On Laptop B (Student Screen):**
   - Enter `prajwal` and the generated password `MW-8K4P2X` (use the **Show** button to verify). Click **Sign In**.
   - **Expected Result:** Because `mustChangePassword` is `true` on first login, the page prompts **"First Login: Change Temporary Password"**.
   - Enter `student123` and click **Save New Password & Continue**.
   - **Expected Result:** Redirects to the Student Dashboard showing **Rs. 0.00** balance and `Protein Non-Veg Plan`.

---

## Minute 1:00 – 2:30 | Step 2: Live Skip Request -> 3-Second Polling -> Approval & Wallet Credit
1. **On Laptop A (Admin Console):**
   - In the **Admin Demo Clock Controller**, set the simulated clock to **`2026-11-13 06:00 AM IST`** (or click **Advance +1 Hour** if needed so Lunch cutoff at 06:30 AM is still open).
   - Point out the orange **DEMO CLOCK ACTIVE** banner that appears across both laptops within 3 seconds.
2. **On Laptop B (Student Dashboard):**
   - Look at **Today's Three Meals**:
     - `BREAKFAST (08:00 AM)` shows **Cutoff Passed (2:00 AM)** and the button is disabled.
     - `LUNCH (12:30 PM)` shows a live countdown (`0h 30m left` before the `6:30 AM IST` cutoff) and an active green **"I'll miss this meal"** button.
   - Click **"I'll miss this meal"** on **LUNCH**.
3. **On Laptop A (Admin Console — do not refresh!):**
   - **Expected Result:** Within **3 seconds**, the **Pending & Appealed Skip Requests Queue** flashes amber and shows Prajwal's `LUNCH` request with:
     - `SUGGESTED: APPROVE`
     - Reason: `Submitted before 6h cutoff (6:30 AM)` and `No counter attendance conflict found`
     - Estimated credit: **`Rs. 60.00`** (`80%` of `Rs. 75.00` via `PercentRefundPolicy::compute()`).
   - Click **Approve**.
4. **On Laptop B (Student Dashboard — do not refresh!):**
   - **Expected Result:** Within **3 seconds**, the top Wallet Hero Card flashes and updates from **Rs. 0.00 -> Rs. 60.00**, a new `CREDIT` row appears in the **Append-Only Wallet Ledger Statement**, and an approval notification lands in the **Notifications Inbox**.
   - On Laptop A, point out that **Tomorrow's / Today's Headcount Forecast** decremented by 1 plate.

---

## Minute 2:30 – 3:45 | Step 3: Late Request Rejected via Demo Clock (Polymorphic `Clock::now()`)
1. **On Laptop A (Admin Console):**
   - In the **Admin Demo Clock Controller**, click **"Advance +1 Hour"** so the simulated time moves from `06:00 AM IST` to **`07:00 AM IST`** (past the `06:30 AM IST` Lunch cutoff).
2. **On Laptop B (Student Dashboard):**
   - Notice that within 3 seconds the Lunch card disables its button (`Cutoff Passed`).
   - Using the **Plan Ahead** form, try submitting a `Single Future Meal Skip` for **today's `LUNCH`** anyway.
   - **Expected Result:** Red error banner explains the exact rule violated:
     `Cutoff for LUNCH (2026-11-13) was 2026-11-13 06:30 AM IST; the request came at 2026-11-13 07:00 AM IST. [CUTOFF_WINDOW_RULE]`

---

## Minute 3:45 – 4:45 | Step 4: Counter Attendance Fraud Detection & One-Time Appeal
1. **On Laptop A (Admin Console):**
   - Use **"2. Counter Attendance Log"** to log counter attendance for `bt25cse147` on a meal they requested to skip.
   - Point out the red badge on Prajwal's request:
     - `SUGGESTED: REJECT`
     - `SUSPICIOUS: Student was logged eating at mess counter by messstaff`
   - Click **Reject** on the suspicious request.
2. **On Laptop B (Student Screen):**
   - In **My Skip Requests & One-Time Appeals**, click **"Appeal Once"**, enter `"My roommate used my ID card by mistake"`, and submit.
   - **Expected Result:** Status changes to `APPEALED` and reappears in the Admin queue on Laptop A within 3 seconds. If rejected again, the `Appeal Once` button disappears (enforcing the single-appeal rule).

---

## Minute 4:45 – 5:45 | Step 5: Multi-Day Skip Expansion & Mess-Closed Auto-Credit
1. **On Laptop B (Student Screen — signed in as `bt25cse147`):**
   - In **Plan Ahead**, select **Multi-Day Leave Range**, pick a 2-day future range, keep Breakfast/Lunch/Dinner checked, enter `"Going home for weekend"`, and click **Submit Request**.
   - **Expected Result:** Banner confirms `MultiDaySkipRequest::expandSlots()` expanded the range into **6 individual meal slots**, each visible in the calendar and Admin queue.
2. **On Laptop A (Admin Console):**
   - In **"3. Mark Mess-Closed Meal"**, select tomorrow's date, `DINNER`, reason `"Kitchen Deep Cleaning"`, and click **"Mark Closed & Auto-Credit All"**.
   - **Expected Result:** Banner confirms every active student was auto-credited **100%** of their plan's Dinner price (`FullRefundPolicy`), and tomorrow's Dinner forecast headcount drops to **`0 plates (MESS CLOSED)`**.
   - **Cancel / Reopen Demonstration (10 seconds):** Under **Active Closures & Reopen**, click **"Reopen / Cancel"** on that closure.
   - **Expected Result:** Banner confirms closure is cancelled, auto-credits are reversed safely from student wallets without risking negative balances, students receive a "Mess Reopened" notification, and headcount forecast is restored.

---

## Minute 5:45 – 7:00 | Step 6: Month-End Settlement, Offline Payout, & Role Swap
1. **On Laptop A (Admin Console):**
   - In the **Enrolled Students** table, point out `bt25cse147` who selected `REFUND` (or `CARRY_FORWARD`).
   - Click **"Run Month-End Settlement Now"** in the Settlement card.
   - **Expected Result:**
     - Students with `CARRY_FORWARD` have their wallet balance debited and applied as a discount on next month's cycle.
     - Students with `REFUND` appear in the **Offline Payouts** table as `PENDING PAYOUT`.
   - Click **"Mark Paid Offline"** next to the payout -> status updates to **`PAID OFFLINE`**.
2. **Inspect File Handling Live:**
   - Open `data/ledger.txt` or `data/students.txt` in the terminal to show pipe-delimited plaintext records with integer paise and UTC/IST timestamps.
3. **Midway / Final Role Swap (15 seconds):**
   - On **Laptop A**, click **Logout** and sign in as `bt25cse147` (`student123`) to show settled wallet statement (`PAYOUT` transaction) and notification.
   - On **Laptop B**, click **Logout** and sign in as `admin` (`admin123`) to show the updated Admin Console from the second machine.

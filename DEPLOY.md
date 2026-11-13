# DEPLOY.md — Cloud Deployment, Local Persistence & Two-Laptop Hotspot Demo Guide

## 1. Environment Variables
The C++ server reads configuration from environment variables at startup (`Config::loadFromEnv()`):

| Variable | Default | Description |
| :--- | :--- | :--- |
| `PORT` | `8080` | HTTP port bound on `0.0.0.0` |
| `DATA_DIR` | `./data` | Folder storing all pipe-delimited text files and `audit.log` |
| `ADMIN_SEED_PASSWORD` | `admin123` | Initial password for the seeded `admin` account |
| `SESSION_SECRET` | `mealwallet-classroom-secret-2026` | Server-side pepper mixed into password hashing |
| `REPO_MODE` | `TEXT` | Repository template backend: `TEXT`, `BINARY`, or `MEMORY` |

> **Zero SQL / Database Dependencies:** `DATABASE_URL`, `libpq`, and `libpqxx` are completely absent from the codebase and `Dockerfile`. All persistence uses standard C++17 `<fstream>`.

---

## 2. Important Note on Free Cloud Hosting (Ephemeral Disk Limitation)
On free cloud container platforms (such as Render Free, Railway Trial, or Fly.io without volumes), the container's filesystem is **ephemeral** (temporary) — when the cloud container sleeps or restarts, local disk changes are wiped.

How **Meal Wallet** handles this safely:
1. **Auto-Seed on Startup:** Whenever `./mealwallet` starts and detects that `DATA_DIR` is missing or empty, it automatically creates `DATA_DIR` and populates the complete demo seed dataset so the app never crashes on missing files.
2. **Visible Admin Console Notice:** The Admin Console displays a clear banner: *"Data is stored in text files; on free hosting, data resets when the server restarts."*
3. **One-Click Reset & CSV Backup:** Before or during a demo, the Admin can click **Export Backup (CSV)** to download all records to their laptop, or click **Reset Demo Data** to restore the seed state in under 1 second.

### Deploying to Render / Railway / Fly.io with Docker
1. Push this repository to GitHub.
2. Create a new **Web Service** on Render or Railway pointing to the repository (it will automatically detect the `Dockerfile`).
3. Set environment variables:
   - `ADMIN_SEED_PASSWORD=admin123`
   - `SESSION_SECRET=your-random-secret-string`
   - `DATA_DIR=/app/data`
4. Once deployed, verify `https://<your-app-url>/healthz` returns `{"ok":true,"service":"meal-wallet-cpp17"}`.
5. Open `https://<your-app-url>` on **Laptop A (Admin)** and **Laptop B (Student)**.

---

## 3. Running Locally with Persistent Text Files
When running on your own laptop (Linux, WSL2 Ubuntu, macOS, or Windows), `./data` is a normal folder on your hard drive, so **all changes persist permanently across server restarts**:

```bash
make
./mealwallet
```
- Inspect live text files anytime in another terminal:
  ```bash
  cat data/ledger.txt
  cat data/audit.log
  ```

---

## 4. Hotspot / LAN Two-Laptop Demo (Zero Internet Required!)
If classroom Wi-Fi or cloud internet is slow during your viva, you can run the C++ server on **Laptop A** and connect **Laptop B** over a mobile hotspot or college Wi-Fi LAN:

1. Connect **Laptop A** and **Laptop B** to the same mobile hotspot or Wi-Fi network.
2. On **Laptop A**, start the server:
   ```bash
   ./mealwallet
   ```
3. The server binds to `0.0.0.0:8080` and prints its LAN IP address automatically on startup:
   ```text
   Local URL       : http://localhost:8080
   LAN / Hotspot   : http://192.168.43.105:8080  <-- Open on 2nd Laptop!
   ```
   *(Note: If you are running inside WSL2 on Windows and want a second laptop on the hotspot to reach WSL2, either run the compiled binary natively on Windows or forward port 8080 in PowerShell as Admin: `netsh interface portproxy add v4tov4 listenport=8080 listenaddress=0.0.0.0 connectport=8080 connectaddress=$(wsl hostname -I | awk '{print $1}')`)*
4. On **Laptop A**, open `http://localhost:8080` and sign in as `admin` (`admin123`).
5. On **Laptop B**, open `http://<LAN-IP>:8080` and sign in as a student (`aarav` / `student123`).

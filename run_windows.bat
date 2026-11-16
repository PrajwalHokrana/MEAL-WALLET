@echo off
setlocal
cd /d "%~dp0"

echo.
echo ====================================================================
echo   MEAL WALLET — Hostel Mess Refund System
echo ====================================================================
echo.

rem 1. Terminate any previous orphaned instance holding port 8080
taskkill /f /im mealwallet.exe >nul 2>nul

rem 2. Replace with any pre-compiled update binary
if exist mealwallet_update.exe (
    copy /y mealwallet_update.exe mealwallet.exe >nul 2>nul
    if not errorlevel 1 del mealwallet_update.exe
)

rem 3. Compile if binary does not exist
if not exist mealwallet.exe (
    where g++ >nul 2>nul
    if not errorlevel 1 (
        echo mealwallet.exe not found. Compiling with MinGW g++ ...
        g++ -std=c++17 -O2 -Wall -Wextra -Iinclude -Ithird_party src/main.cpp src/DomainImpl.cpp src/ConsoleAndTests.cpp src/repository/MealWalletService.cpp src/api/ApiServer.cpp -o mealwallet.exe -lws2_32
    )
    if not exist mealwallet.exe (
        echo [ERROR] mealwallet.exe not found and could not be compiled.
        pause
        exit /b 1
    )
)

rem 4. Detect Wi-Fi / LAN IP address
set "MY_LAN_IP="
for /f "tokens=2 delims=:" %%a in ('ipconfig ^| findstr /i "IPv4"') do (
    for /f "tokens=1" %%b in ("%%a") do if not "%%b"=="" set "MY_LAN_IP=%%b"
)
if "%MY_LAN_IP%"=="" set "MY_LAN_IP=127.0.0.1"

echo Starting server...
echo.
echo   Host Laptop (This Machine)   : http://localhost:8080
echo   SECOND LAPTOP (Same Network) : http://%MY_LAN_IP%:8080  ^<-- OPEN THIS ON 2nd LAPTOP
echo.
echo   ------------------------------------------------------------------
echo   IMPORTANT FOR 2nd LAPTOP:
echo   - Both laptops must be on the SAME Wi-Fi or Mobile Hotspot.
echo   - If you are on College Wi-Fi and the 2nd laptop cannot connect:
echo     College Wi-Fi blocks laptop-to-laptop connections (AP isolation).
echo     Turn ON your phone or laptop Mobile Hotspot, connect BOTH laptops
echo     to it, and re-run this file!
echo   ------------------------------------------------------------------
echo.
echo ====================================================================
echo.

start "" "http://localhost:8080"
.\mealwallet.exe %*
if errorlevel 1 (
    echo.
    echo [ERROR] Server exited with an error.
    pause
)

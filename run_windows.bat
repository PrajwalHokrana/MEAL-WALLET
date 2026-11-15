@echo off
setlocal
cd /d "%~dp0"

echo.
echo ====================================================================
echo   MEAL WALLET — Hostel Mess Refund System
echo ====================================================================
echo.

if exist mealwallet_update.exe (
    copy /y mealwallet_update.exe mealwallet.exe >nul 2>nul
    if not errorlevel 1 del mealwallet_update.exe
)

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

echo Starting server...
echo.
echo   Browser will open automatically at: http://localhost:8080
echo.
echo   For the SECOND LAPTOP: open your browser and go to the
echo   LAN address shown below (look for "LAN / Hotspot" line).
echo   Both laptops must be on the same Wi-Fi or hotspot.
echo.
echo ====================================================================
echo.

start "" "http://localhost:8080"
.\mealwallet.exe %*

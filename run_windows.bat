@echo off
setlocal
cd /d "%~dp0"

echo.
echo ====================================================================
echo   MEAL WALLET — Hostel Mess Refund System
echo ====================================================================
echo.

if not exist mealwallet.exe (
    echo [ERROR] mealwallet.exe not found in this folder.
    echo         Please copy mealwallet.exe here and try again.
    pause
    exit /b 1
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

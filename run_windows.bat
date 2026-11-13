@echo off
setlocal
cd /d "%~dp0"

echo ========================================================================
echo   MEAL WALLET - WINDOWS VS CODE BUILD ^& LAUNCHER
echo ========================================================================

where g++ >nul 2>nul
if not errorlevel 1 goto :compile_gcc

where cl >nul 2>nul
if not errorlevel 1 goto :compile_msvc

echo [ERROR] No C++ compiler (g++ or cl) found in your Windows PATH.
pause
exit /b 1

:compile_gcc
echo [1/2] Compiling C++17 project with MinGW g++ ...
g++ -std=c++17 -O2 -Wall -Wextra -Iinclude -Ithird_party src/main.cpp src/DomainImpl.cpp src/ConsoleAndTests.cpp src/repository/MealWalletService.cpp src/api/ApiServer.cpp -o mealwallet.exe -lws2_32
if errorlevel 1 (
    echo [ERROR] g++ compilation failed!
    pause
    exit /b 1
)
goto :run_server

:compile_msvc
echo [1/2] Compiling C++17 project with MSVC cl.exe ...
cl /std:c++17 /EHsc /O2 /W4 /Iinclude /Ithird_party src/main.cpp src/DomainImpl.cpp src/ConsoleAndTests.cpp src/repository/MealWalletService.cpp src/api/ApiServer.cpp /Fe:mealwallet.exe ws2_32.lib
if errorlevel 1 (
    echo [ERROR] MSVC compilation failed!
    pause
    exit /b 1
)

:run_server
if "%1"=="--test" (
    .\mealwallet.exe --test
    exit /b %ERRORLEVEL%
)
if "%1"=="--console" (
    .\mealwallet.exe --console
    exit /b %ERRORLEVEL%
)
if "%1"=="--seed-demo" (
    .\mealwallet.exe --seed-demo
    exit /b %ERRORLEVEL%
)

echo [2/2] Opening http://localhost:8080 in your default browser...
start "" "http://localhost:8080"
.\mealwallet.exe %*

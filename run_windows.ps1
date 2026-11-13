# Meal Wallet — PowerShell Launcher for VS Code on Windows
$ErrorActionPreference = "Stop"
Set-Location -Path $PSScriptRoot

Write-Host "========================================================================" -ForegroundColor Cyan
Write-Host "  MEAL WALLET — WINDOWS VS CODE C++17 BUILD & SERVER LAUNCHER" -ForegroundColor Cyan
Write-Host "========================================================================" -ForegroundColor Cyan

Write-Host "[1/2] Compiling C++17 source files with g++..." -ForegroundColor Yellow
g++ -std=c++17 -O2 -Wall -Wextra -Iinclude -Ithird_party `
    src/main.cpp src/DomainImpl.cpp src/ConsoleAndTests.cpp `
    src/repository/MealWalletService.cpp src/api/ApiServer.cpp `
    -o mealwallet.exe -lws2_32

if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] Compilation failed!" -ForegroundColor Red
    exit 1
}

if ($args.Count -gt 0) {
    & .\mealwallet.exe @args
    exit $LASTEXITCODE
}

Write-Host "[2/2] Launching http://localhost:8080 and starting C++ server..." -ForegroundColor Green
Start-Process "http://localhost:8080"
& .\mealwallet.exe

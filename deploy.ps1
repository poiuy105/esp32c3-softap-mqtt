# ESP32-C3 GitHub Deployment Script (PowerShell)
Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "ESP32-C3 SoftAP MQTT Config - Deployment" -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan
Write-Host ""

# Change to project directory
Set-Location $PSScriptRoot

Write-Host "Current directory: $PWD" -ForegroundColor Green
Write-Host ""

# Step 1: Initialize Git if needed
if (-not (Test-Path ".git")) {
    Write-Host "[1/6] Initializing Git repository..." -ForegroundColor Yellow
    git init
    git branch -M main
}

# Step 2: Add all files
Write-Host "[2/6] Adding files to Git..." -ForegroundColor Yellow
git add .

# Step 3: Commit files
Write-Host "[3/6] Committing changes..." -ForegroundColor Yellow
git commit -m "Initial commit: ESP32-C3 SoftAP MQTT Config System with GitHub Actions"

Write-Host ""
Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "Git repository is ready!" -ForegroundColor Green
Write-Host ""
Write-Host "Next steps:" -ForegroundColor White
Write-Host "1. Create a GitHub repository at https://github.com/new" -ForegroundColor White
Write-Host "2. After creating, run these commands in Git Bash:" -ForegroundColor White
Write-Host ""
Write-Host "   git remote add origin https://github.com/您的用户名/您的仓库名.git" -ForegroundColor Gray
Write-Host "   git push -u origin main" -ForegroundColor Gray
Write-Host ""
Write-Host "3. Then check the Actions tab, wait for build, and download merged.bin" -ForegroundColor White
Write-Host "==================================================" -ForegroundColor Cyan

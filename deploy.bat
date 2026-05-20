@echo off
REM ESP32-C3 GitHub Deployment Script

echo ==================================================
echo ESP32-C3 SoftAP MQTT Config - Deployment
echo ==================================================
echo.

cd /d "%~dp0"

REM Step 1: Initialize Git
if not exist ".git" (
    echo [1/5] Initializing Git repository...
    git init
    git branch -M main
)

REM Step 2: Add and commit files
echo [2/5] Adding files to Git...
git add .

echo [3/5] Committing changes...
git commit -m "Initial commit: ESP32-C3 SoftAP MQTT Config System with GitHub Actions"

echo.
echo ==================================================
echo Next steps:
echo 1. Go to https://github.com/new and create a repository
echo 2. After creating the repo, run these commands:
echo.
echo    git remote add origin https://github.com/您的用户名/您的仓库名.git
echo    git push -u origin main
echo.
echo 3. Then check the Actions tab on GitHub and download merged.bin
echo ==================================================
echo.

pause

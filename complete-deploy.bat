@echo off
echo ==================================================
echo ESP32-C3 SoftAP MQTT - Complete Deployment
echo ==================================================
echo.

cd /d "%~dp0"

echo [1/7] Checking Git repository...
if not exist ".git" (
    echo Initializing Git...
    git init
)

echo.
echo [2/7] Adding files to Git...
git add .

echo.
echo [3/7] Committing changes...
git commit -m "Initial commit: ESP32-C3 SoftAP MQTT Config with GitHub Actions"
git branch -M main

echo.
echo ==================================================
echo Git repository is ready!
echo ==================================================
echo.
echo Next steps - Please run these commands in GIT BASH:
echo.
echo   1. Create a GitHub repo first (if you haven't):
echo      https://github.com/new
echo.
echo   2. Then run in Git Bash:
echo      git remote add origin https://github.com/您的用户名/您的仓库名.git
echo      git push -u origin main
echo.
echo   3. Check Actions tab, wait for build, download merged.bin!
echo.
echo If you have GitHub CLI, run:
echo      gh repo create esp32c3-softap-mqtt --public --push --source . --remote origin
echo.
pause

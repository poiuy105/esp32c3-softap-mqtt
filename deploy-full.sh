#!/bin/bash
# ESP32-C3 Complete Deployment Script for Git Bash

echo "=================================================="
echo "ESP32-C3 SoftAP MQTT - Complete Deployment"
echo "=================================================="
echo ""

cd "$(dirname "$0")"

echo "Current directory: $(pwd)"
echo ""

# Step 1: Initialize Git repository if needed
if [ ! -d ".git" ]; then
    echo "[1/6] Initializing Git repository..."
    git init
    git branch -M main
fi

# Step 2: Stage and commit all files
echo "[2/6] Staging and committing files..."
git add .
git commit -m "Initial commit: ESP32-C3 SoftAP MQTT Config System with GitHub Actions"

echo ""
echo "=================================================="
echo "Git repository is now ready!"
echo "=================================================="
echo ""
echo "Now you have two options to continue:"
echo ""
echo "Option A: Use GitHub CLI (gh) if you have it installed"
echo "   gh repo create esp32c3-softap-mqtt --public --push --source . --remote origin"
echo ""
echo "Option B: Manually create and push"
echo "   1. Go to https://github.com/new and create a repository"
echo "   2. Then run these commands:"
echo "      git remote add origin https://github.com/您的用户名/您的仓库名.git"
echo "      git push -u origin main"
echo ""
echo "Step 3: After pushing, check Actions tab on GitHub"
echo "Step 4: Wait for build, download esp32c3-firmware artifact"
echo "Step 5: Unzip and get merged.bin!"
echo ""
echo "=================================================="

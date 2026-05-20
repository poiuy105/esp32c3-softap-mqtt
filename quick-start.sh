#!/bin/bash
# Quick Start Script for ESP32-C3 SoftAP MQTT Config

echo "=================================================="
echo " ESP32-C3 SoftAP MQTT Config - Quick Start"
echo "=================================================="
echo ""

# Change to project directory
cd "$(dirname "$0")"

echo "Current directory: $(pwd)"
echo ""

# Check if git is initialized
if [ ! -d ".git" ]; then
    echo "Initializing Git repository..."
    git init
    git branch -M main
fi

# Check if remote origin exists
if ! git remote | grep -q origin; then
    echo ""
    echo "=================================================="
    echo " Please set up your GitHub repository first!"
    echo " 1. Go to https://github.com/new"
    echo " 2. Create a new repository"
    echo " 3. After creating, run the following command:"
    echo ""
    echo "    git remote add origin https://github.com/您的用户名/您的仓库名.git"
    echo "    git push -u origin main"
    echo ""
    echo " Or run:"
    echo "    bash quick-start.sh"
    echo "=================================================="
    echo ""
    exit 1
fi

echo "Adding files to Git..."
git add .

echo "Committing changes..."
git commit -m "Add ESP32-C3 SoftAP MQTT Config project with GitHub Actions"

echo "Pushing to GitHub..."
git push -u origin main

echo ""
echo "=================================================="
echo " ✅ Successfully pushed to GitHub!"
echo "=================================================="
echo ""
echo "Next steps:"
echo " 1. Go to your GitHub repository"
echo " 2. Click on the 'Actions' tab"
echo " 3. Wait for the build to complete"
echo " 4. Download the merged.bin from artifacts"
echo ""

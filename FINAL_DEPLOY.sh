#!/bin/bash
# ESP32-C3 最终部署脚本
# 直接在 Git Bash 中运行这个文件！

echo "=================================================="
echo "  ESP32-C3 SoftAP MQTT - 最终部署"
echo "=================================================="
echo ""

cd "$(dirname "$0")"

echo "当前目录: $(pwd)"
echo ""

# 步骤 1: 初始化 Git
echo "[1/5] 初始化 Git 仓库..."
git init
git branch -M main
echo "    ✓ 完成！"

# 步骤 2: 添加所有文件
echo ""
echo "[2/5] 添加所有文件..."
git add .
echo "    ✓ 完成！"

# 步骤 3: 提交更改
echo ""
echo "[3/5] 提交更改..."
git commit -m "Initial commit: ESP32-C3 SoftAP MQTT Config System with GitHub Actions"
echo "    ✓ 完成！"

# 步骤 4: 添加远程仓库
echo ""
echo "[4/5] 添加远程仓库..."
git remote add origin https://github.com/poiuy105/esp32c3-softap-mqtt.git
echo "    ✓ 完成！"

# 步骤 5: 推送到 GitHub
echo ""
echo "[5/5] 推送到 GitHub..."
git push -u origin main
echo "    ✓ 完成！"

echo ""
echo "=================================================="
echo "  ✅ 部署成功！"
echo "=================================================="
echo ""
echo "下一步："
echo "  1. 访问您的仓库：https://github.com/poiuy105/esp32c3-softap-mqtt"
echo "  2. 点击顶部的 Actions 标签"
echo "  3. 等待工作流运行（约 3-5 分钟）"
echo "  4. 工作流变绿 ✔️ 后，点进去"
echo "  5. 页面底部 Artifacts -> 下载 esp32c3-firmware"
echo "  6. 解压 ZIP 包，里面就有 merged.bin！"
echo ""
echo "=================================================="

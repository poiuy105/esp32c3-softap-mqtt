# 🏃 快速开始指南

本指南将帮助您在10分钟内完成部署并下载 merged.bin！

## 📋 目录
## 步骤总览

1. 在 GitHub 创建仓库
2. 推送代码
3. 等待 GitHub Actions 自动编译
4. 下载 merged.bin

---

## 🚀 第一步：创建 GitHub 仓库

1. 打开浏览器访问：https://github.com/new
2. 仓库名：**esp32c3-softap-mqtt
3. 权限：Public 或 Private 都可以
4. **重要：**不要** 初始化 README、.gitignore 或 LICENSE！
5. 点击 **Create repository**

---

## 🚀 第二步：推送代码

打开 **Git Bash**，执行下面一行一行执行：

```bash
cd e:/Espidf/softap_mqtt

git init
git add .
git commit -m "Initial commit: ESP32-C3 SoftAP MQTT"
git branch -M main

# 下面这行要把 您的用户名/仓库名 替换成您真实的信息
git remote add origin https://github.com/您的用户名/esp32c3-softap-mqtt.git

git push -u origin main
```

---

## 🚀 第三步：等待编译并下载 merged.bin

1. 推送成功后，访问您的 GitHub 仓库
2. 点击顶部的 **Actions** 标签页
3. 您会看到工作流正在运行，名字是 "ESP-IDF Build"
4. 等 3-5 分钟，直到变成绿色 ✔️
5. 点进去，滑到页面底部
6. 在 **Artifacts** 区域，点击 **esp32c3-firmware** 下载
7. 解压 ZIP 包，里面就是您要的 merged.bin！

---

## 💡 提示

如果您有 **GitHub CLI (gh)**，也可以这样创建仓库：

```bash
# 创建仓库并推送
gh repo create esp32c3-softap-mqtt --public --push --source . --remote origin --description "ESP32-C3 SoftAP MQTT Config System"
```

---

## 🔧 工作流说明

- ESP-IDF 版本：v5.2.2
- 目标芯片：ESP32-C3
- 编译后文件：merged.bin

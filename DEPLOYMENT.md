# GitHub CI/CD 部署指南

本指南帮助您在 GitHub 上创建仓库、配置自动编译，并下载 merged.bin 文件。

## 步骤 1：在 GitHub 上创建仓库

1. 访问 https://github.com/new
2. 填写仓库名称，例如：`esp32c3-softap-mqtt`
3. 选择 **Public** 或 **Private**
4. **不要** 初始化 README、.gitignore 或 LICENSE（我们已经有了）
5. 点击 **Create repository**

## 步骤 2：将本地代码推送到 GitHub

打开 **Git Bash**，并执行以下命令：

```bash
# 进入项目目录
cd e:/Espidf/softap_mqtt

# 初始化 Git（如果还没有）
git init

# 添加所有文件
git add .

# 首次提交
git commit -m "Initial commit: ESP32-C3 SoftAP MQTT Config System"

# 重命名分支为 main
git branch -M main

# 添加远程仓库地址（替换为您的真实仓库地址）
git remote add origin https://github.com/您的用户名/您的仓库名.git

# 推送到 GitHub
git push -u origin main
```

## 步骤 3：触发自动编译

代码成功推送后，GitHub Actions 会自动开始编译：

1. 访问您的仓库页面
2. 点击 **Actions** 标签页
3. 您会看到正在运行的工作流，名为 "ESP-IDF Build"
4. 等待编译完成（约 3-5 分钟）

## 步骤 4：下载 merged.bin 文件

1. 在 Actions 页面中，点击已完成的工作流
2. 滚动到页面底部的 **Artifacts** 部分
3. 点击 **esp32c3-firmware** 下载 ZIP 文件
4. 解压 ZIP 文件，里面就有 `merged.bin`

## 手动触发编译

如果需要手动重新编译：

1. 进入仓库的 **Actions** 标签
2. 选择 "ESP-IDF Build" 工作流
3. 点击 **Run workflow** → 选择分支 → 点击绿色的 **Run workflow**

## 烧录 merged.bin 文件

### 方法 1：使用 esptool.py（推荐）

```bash
python -m esptool --chip esp32c3 --port <COM端口> --baud 460800 \
  write_flash 0x0 merged.bin
```

### 方法 2：使用 ESP Flash Download Tool（Windows）

1. 打开 **Flash Download Tool**（乐鑫官方下载工具）
2. 选择芯片类型为 **ESP32-C3**
3. 添加 `merged.bin` 文件，偏移地址设为 **0x0**
4. 选择正确的 COM 端口和波特率
5. 点击 **START**

### 方法 3：使用 ESP-IDF

```bash
idf.py -p <COM端口> flash
```

## 工作流说明

`.github/workflows/build.yml` 包含以下功能：

- 使用 **ESP-IDF v5.2.2**
- 目标芯片：**ESP32-C3**
- 自动合并固件为单个 `merged.bin` 文件
- 保留编译产物 30 天
- 支持手动触发和推送触发

## 常见问题

### Q: 如何修改 ESP-IDF 版本？
A: 修改 `.github/workflows/build.yml` 中的 `esp_idf_version` 字段。

### Q: 编译失败了怎么办？
A: 在 Actions 工作流中查看详细的错误日志进行调试。

### Q: 编译产物保留多久？
A: 默认保留 30 天，可在 `build.yml` 中修改 `retention-days`。

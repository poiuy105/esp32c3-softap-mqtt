# ESP32-C3 SoftAP + MQTT 配置系统

基于 ESP-IDF 构建的 ESP32-C3 应用程序，提供通过 SoftAP 热点进行 WiFi 和 MQTT 配置的功能。

## 快速开始（GitHub Actions 自动编译）

想要最快拿到合并好的 `merged.bin`？只需 3 步：

1. [创建 GitHub 仓库](https://github.com/new)
2. 推送代码（使用 [QUICKSTART.md](QUICKSTART.md) 中的步骤）
3. 下载 Actions 编译产物！

详细的快速开始指南请查看：[QUICKSTART.md](QUICKSTART.md)

## 功能特点

1. **SoftAP 热点自动启动** - 设备启动时自动创建 WiFi 热点供用户连接
2. **Web 配置界面** - 提供友好的 Web 页面用于配置 WiFi 和 MQTT 参数
3. **配置持久化** - 使用 NVS Flash 保存配置信息
4. **状态机管理** - 完整的状态机管理系统，确保流程顺畅
5. **自动连接** - 配置完成后自动连接 WiFi 和 MQTT Broker
6. **GitHub Actions CI/CD** - 支持自动编译和 merged.bin 生成

## 目录结构

```
softap_mqtt/
├── CMakeLists.txt          # 主项目 CMake 配置
├── sdkconfig.defaults      # 默认配置
├── custom_partitions.csv   # 分区表
├── README.md               # 项目文档
├── QUICKSTART.md           # 快速开始指南
├── DEPLOYMENT.md           # 部署文档
├── .github/
│   └── workflows/
│       └── build.yml       # GitHub Actions 自动编译
├── main/
│   ├── CMakeLists.txt      # 主组件 CMake 配置
│   ├── app_main.c          # 主程序入口
│   ├── core/               # 核心模块（状态机、事件处理）
│   ├── wifi/               # WiFi 管理模块
│   ├── mqtt/               # MQTT 客户端模块
│   ├── config/             # NVS 配置管理
│   └── web/                # HTTP 服务器和 Web 界面
└── esp-idf/                # ESP-IDF 框架
```

## 本地编译和烧录

### 硬件要求

- ESP32-C3 开发板
- 电源 USB 线

### 编译和烧录

1. 设置目标芯片：
```bash
idf.py set-target esp32c3
```

2. 配置项目（可选）：
```bash
idf.py menuconfig
```

3. 编译、烧录和监控：
```bash
idf.py build flash monitor
```

### 使用说明

1. **设备启动**：ESP32-C3 上电后，将自动创建名为 "ESP32-SoftAP" 的 WiFi 热点，密码为 "12345678"
2. **连接热点**：使用手机或电脑连接到该热点
3. **访问配置页面**：在浏览器中打开 `http://192.168.4.1`
4. **配置参数**：填写您的 WiFi SSID、密码和 MQTT Broker 信息
5. **保存配置**：点击保存按钮，设备会自动重启并连接到您的网络

## 配置选项

- **WiFi SSID** - 您的 WiFi 网络名称
- **WiFi 密码** - WiFi 网络密码
- **MQTT Broker** - MQTT Broker 地址（如 `test.mosquitto.org`）
- **MQTT 端口** - MQTT 端口（默认 1883）
- **MQTT 用户名/密码** - 可选的认证信息

## 软件架构

### 状态机

系统包含以下状态：
- `STATE_INIT` - 初始化状态
- `STATE_SOFTAP` - SoftAP 模式，等待用户配置
- `STATE_CONFIG` - 配置已接收，准备连接
- `STATE_CONNECTING` - 正在连接 WiFi 和 MQTT
- `STATE_RUNNING` - 正常运行状态
- `STATE_ERROR` - 错误状态

### 模块设计

- **config** - 管理 NVS 配置的读写
- **core** - 状态机和事件处理
- **wifi** - WiFi Station 和 SoftAP 管理
- **mqtt** - MQTT 客户端功能
- **web** - HTTP 服务器和 Web UI

## 技术栈

- ESP-IDF v5.x
- FreeRTOS
- NVS Flash
- WiFi 驱动
- MQTT 库
- HTTP 服务器

## GitHub Actions CI/CD

本项目配置了自动编译工作流，每次推送到 GitHub 后都会自动：
1. 使用 ESP-IDF v5.2.2 编译
2. 合并固件为单个 `merged.bin`
3. 上传为 Artifacts 供下载

详细说明请查看：[DEPLOYMENT.md](DEPLOYMENT.md)

## 许可证

本项目基于 ESP-IDF 示例开发，可自由使用。

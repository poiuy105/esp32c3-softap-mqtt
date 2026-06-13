# ESP32-C3 SoftAP MQTT 配网系统 / Provisioning System

[中文](#中文) | [English](#english)

---

<a name="中文"></a>

## 功能特性

- **SoftAP 配网**: 设备首次上电自动创建 WiFi 热点，通过 Web 页面配置网络
- **Captive Portal**: 连接热点后自动弹出配置页面（支持 Android / iOS / Windows / macOS）
- **统一认证**: 登录页面 + Token 认证，防暴力破解（5次失败锁定30秒）
- **智能页面切换**: SoftAP 模式显示配网页面，STA 模式显示管理页面
- **MQTT 通信**: 连接 MQTT Broker，支持命令控制和状态上报
- **Home Assistant 集成**: MQTT 自动发现协议，自动注册传感器、开关、数值调节和按钮实体
- **OTA 固件升级**: Web 页面在线升级固件，支持固件头校验
- **密码管理**: 支持在配网页面或管理页面修改管理员密码（NVS 持久化）
- **独立 PWM 控制**: 照明和声波两路 PWM 完全独立控制
- **WiFi 扫描**: 配网页面可扫描并选择周围 WiFi 网络
- **工厂重置**: 长按按键 5 秒恢复出厂设置
- **安全模式**: 驱动/NVS 初始化失败时自动进入，LED 快闪告警

## 硬件要求

- **芯片**: ESP32-C3 (RISC-V 架构)
- **Flash**: 4MB
- **GPIO 分配**:

| GPIO | 功能 | 说明 |
|------|------|------|
| GPIO 1 | 声波 PWM 输出 | 声波信号输出 |
| GPIO 3 | 照明 PWM 输出 | 照明信号输出 |
| GPIO 4 | LED 指示灯 | 低电平有效 |
| GPIO 6 | 声波使能 | 声波模块使能 |
| GPIO 9 | 按键输入 | 长按 5 秒恢复出厂设置 |

## Web 管理界面

### 页面架构

设备根据 WiFi 连接状态自动切换页面：

| WiFi 状态 | 访问 `/` 或 `/index.html` | 说明 |
|----------|--------------------------|------|
| **未连接 (SoftAP)** | 配网页面 | WiFi/MQTT 配置 + 可选密码修改 |
| **已连接 (STA)** | 管理页面 | 设备信息 + 密码修改 + OTA + 折叠式网络配置 |

### URI 路由

| URI | Handler | 说明 |
|-----|---------|------|
| `/login.html` | login_page_handler | 登录页面（公开） |
| `/` `/index.html` | root_handler | 根据WiFi状态自动切换 |
| `/config.html` | config_page_handler | 显式配网页面 |
| `/ota.html` | ota_page_handler | OTA 升级页面 |
| `/api/login` | POST | 登录获取 Token |
| `/api/logout` | POST | 登出 |
| `/api/config` | GET/POST | 获取/保存网络配置（需认证） |
| `/api/scan` | GET | WiFi 扫描（需认证） |
| `/api/status` | GET | 设备状态（公开） |
| `/api/ota` | GET/POST | OTA 信息/升级（需认证） |
| `/api/admin_credentials` | POST | 修改管理员密码（需认证） |

### 默认登录凭据

- 用户名: `admin`
- 密码: `synaflow2024`
- 首次启动时打印到串口日志，建议首次登录后立即修改

## 快速开始

### 1. 克隆项目

```bash
git clone <repository-url>
cd softap_mqtt
```

### 2. 设置 ESP-IDF 环境

```bash
cd esp-idf
./install.sh
. ./export.sh
cd ..
```

### 3. 编译项目

```bash
idf.py set-target esp32c3
idf.py build
```

### 4. 烧录固件

```bash
idf.py -p COM_PORT flash
```

### 5. 配网流程

1. **设备上电**: 首次上电自动进入配网模式，LED 慢闪
2. **连接热点**: 手机/电脑连接 `ESP32-SoftAP-XXXX` 热点（密码: `12345678`）
3. **自动弹窗**: 浏览器自动弹出登录页面（或手动访问 `192.168.4.1`）
4. **登录**: 输入默认账号密码 `admin` / `synaflow2024`
5. **配置网络**: 选择 WiFi、输入密码、配置 MQTT Broker
6. **保存**: 点击保存，设备自动连接 WiFi 和 MQTT

## Home Assistant 集成

设备连接 MQTT Broker 后自动注册以下实体（共 15 个）：

### 传感器 (Sensor)

| 实体 | 说明 |
|------|------|
| WiFi 名称 | 当前连接的 WiFi SSID |
| WiFi 信号强度 | RSSI 值 (dBm) |
| IP 地址 | 设备 IP |
| 空闲内存 | 剩余堆内存 (B) |
| 运行时间 | 设备运行时长 (s) |

### 开关 (Switch)

| 实体 | 说明 |
|------|------|
| LED 指示灯 | 控制 LED 开关 |
| 照明控制 | 开关照明输出 |
| 声波控制 | 开关声波输出 |

### 数值调节 (Number)

| 实体 | 范围 | 步进 |
|------|------|------|
| 照明频率 | 0 - 150 kHz | 0.1 Hz |
| 照明亮度 | 0 - 100% | 0.1% |
| 声波频率 | 0 - 150 kHz | 1 Hz |
| 声波音量 | 0 - 100% | 0.1% |

### 按钮 (Button)

| 实体 | 说明 |
|------|------|
| 重启设备 | 远程重启 ESP32 |

### 二进制传感器 (Binary Sensor)

| 实体 | 说明 |
|------|------|
| MQTT 状态 | MQTT 连接状态 |

## MQTT 主题

### 命令主题（设备接收）

```
{node_id}/led/set                    # LED 开关: ON/OFF
{node_id}/light/power/set            # 照明开关: ON/OFF
{node_id}/light/freq/set             # 照明频率: 0-150000
{node_id}/light/duty/set             # 照明亮度: 0-100
{node_id}/sound/power/set            # 声波开关: ON/OFF
{node_id}/sound/freq/set             # 声波频率: 0-150000
{node_id}/sound/vol/set              # 声波音量: 50-100
{node_id}/restart/set                # 重启设备: RESTART
```

### 状态主题（设备发布）

```
{node_id}/led/state                  # LED 状态: ON/OFF
{node_id}/light_power/state          # 照明开关状态
{node_id}/light_freq/state           # 照明频率
{node_id}/light_duty/state           # 照明亮度
{node_id}/sound_power/state          # 声波开关状态
{node_id}/sound_freq/state           # 声波频率
{node_id}/sound_vol/state            # 声波音量
{node_id}/status                     # 在线状态: online/offline
```

其中 `{node_id}` 格式为 `esp32c3_{MAC后缀}`，例如 `esp32c3_5500e8`。

## 项目结构

```
softap_mqtt/
├── main/
│   ├── core/                  # 核心框架（状态机、事件处理、安全模式）
│   ├── wifi/                  # WiFi 管理（STA/AP 模式切换）
│   ├── mqtt/                  # MQTT 客户端、HA 发现、命令处理
│   ├── config/                # NVS 配置管理（WiFi/MQTT/管理员凭据）
│   ├── web/                   # HTTP 服务器、认证中间件、OTA、页面
│   ├── drivers/               # 硬件驱动（PWM、GPIO、按键、RMT）
│   └── utils/                 # 系统监控（堆内存、温度、运行时间）
├── CMakeLists.txt             # 顶层构建配置
├── sdkconfig.defaults         # SDK 默认配置
├── custom_partitions.csv      # 自定义分区表（双 OTA 分区）
└── .github/workflows/         # GitHub Actions CI/CD
```

## 分区表

| 分区 | 大小 | 用途 |
|------|------|------|
| nvs | 24KB | 配置存储（WiFi/MQTT/管理员凭据/设备状态） |
| otadata | 8KB | OTA 分区选择数据 |
| phy_init | 4KB | PHY 初始化数据 |
| ota_0 | 1920KB | 应用分区 A |
| ota_1 | 1920KB | 应用分区 B |

## 默认配置

| 配置项 | 默认值 | 说明 |
|--------|--------|------|
| SoftAP SSID | `ESP32-SoftAP-{MAC}` | 带 MAC 后缀 |
| SoftAP 密码 | `12345678` | WPA2 |
| SoftAP 信道 | 1 | 固定信道 |
| MQTT Broker | `mqtt://test.mosquitto.org` | 默认测试服务器 |
| MQTT 端口 | 1883 | MQTT 3.1.1 |
| 照明频率 | 1 kHz | 默认值 |
| 照明亮度 | 80% | 默认值 |
| 声波频率 | 20 kHz | 默认值 |
| 声波音量 | 75% | 默认值 |

## CI/CD

项目使用 GitHub Actions 自动构建：

- **触发条件**: Push 到 main 分支、Pull Request
- **构建环境**: `espressif/esp-idf-ci-action@v1` (Docker)
- **ESP-IDF 版本**: v5.1.2
- **目标芯片**: ESP32-C3
- **构建产物**: `firmware.bin`（合并固件，可直接烧录）

## 故障排除

### 无法进入配网模式
- 首次上电自动进入配网模式
- 长按按键 5 秒恢复出厂设置

### WiFi 连接失败
- 确认 WiFi 密码正确
- 检查 WiFi 信号强度
- 查看串口日志获取详细错误信息

### MQTT 连接失败
- 确认 MQTT Broker 地址和端口正确
- 检查网络连接
- 确认 Broker 支持匿名连接或配置正确的用户名密码

### Home Assistant 未发现设备
- 确认 MQTT 集成已配置
- 检查设备是否成功连接 MQTT Broker
- 查看 MQTT 主题是否有消息发布

### Web 页面显示 401 Unauthorized
- Token 已过期或设备重启，重新登录即可
- 连续 5 次认证失败将锁定 30 秒

## 许可证

[MIT License](LICENSE)

## 致谢

- [ESP-IDF](https://github.com/espressif/esp-idf) - Espressif IoT Development Framework
- [Home Assistant](https://www.home-assistant.io/) - 开源智能家居平台

---

<a name="english"></a>

## Features

- **SoftAP Provisioning**: Device automatically creates a WiFi hotspot on first boot for web-based network configuration
- **Captive Portal**: Auto-redirect to configuration page after connecting (Android / iOS / Windows / macOS)
- **Unified Authentication**: Login page + Token-based auth with brute-force protection (5 failures = 30s lockout)
- **Smart Page Switching**: Shows config page in SoftAP mode, admin page in STA mode
- **MQTT Communication**: Connect to MQTT Broker with command control and status reporting
- **Home Assistant Integration**: MQTT Discovery protocol with auto-registered sensors, switches, numbers, and buttons
- **OTA Firmware Update**: Web-based firmware upgrade with magic byte verification
- **Password Management**: Change admin credentials from config page or admin page (persisted in NVS)
- **Independent PWM Control**: Two fully independent PWM channels for lighting and ultrasonic
- **WiFi Scanning**: Scan and select nearby WiFi networks from the config page
- **Factory Reset**: Long-press button for 5 seconds to restore factory defaults
- **Safe Mode**: Auto-enter on driver/NVS initialization failure with LED fast-blink warning

## Hardware Requirements

- **Chip**: ESP32-C3 (RISC-V architecture)
- **Flash**: 4MB
- **GPIO Assignment**:

| GPIO | Function | Description |
|------|----------|-------------|
| GPIO 1 | Ultrasonic PWM Output | Ultrasonic signal output |
| GPIO 3 | Lighting PWM Output | Lighting signal output |
| GPIO 4 | LED Indicator | Active low |
| GPIO 6 | Ultrasonic Enable | Ultrasonic module enable |
| GPIO 9 | Button Input | Long-press 5s for factory reset |

## Web Management Interface

### Page Architecture

The device automatically switches pages based on WiFi connection state:

| WiFi State | Access `/` or `/index.html` | Description |
|-------------|----------------------------|-------------|
| **Disconnected (SoftAP)** | Config Page | WiFi/MQTT config + optional password change |
| **Connected (STA)** | Admin Page | Device info + password change + OTA + collapsible network config |

### URI Routing

| URI | Handler | Description |
|-----|---------|-------------|
| `/login.html` | login_page_handler | Login page (public) |
| `/` `/index.html` | root_handler | Auto-switch based on WiFi state |
| `/config.html` | config_page_handler | Explicit config page |
| `/ota.html` | ota_page_handler | OTA upgrade page |
| `/api/login` | POST | Login to get Token |
| `/api/logout` | POST | Logout |
| `/api/config` | GET/POST | Get/save network config (auth required) |
| `/api/scan` | GET | WiFi scan (auth required) |
| `/api/status` | GET | Device status (public) |
| `/api/ota` | GET/POST | OTA info/upgrade (auth required) |
| `/api/admin_credentials` | POST | Change admin password (auth required) |

### Default Login Credentials

- Username: `admin`
- Password: `synaflow2024`
- Printed to serial log on first boot. Recommended to change immediately after first login.

## Quick Start

### 1. Clone the Project

```bash
git clone <repository-url>
cd softap_mqtt
```

### 2. Set Up ESP-IDF Environment

```bash
cd esp-idf
./install.sh
. ./export.sh
cd ..
```

### 3. Build the Project

```bash
idf.py set-target esp32c3
idf.py build
```

### 4. Flash Firmware

```bash
idf.py -p COM_PORT flash
```

### 5. Provisioning Flow

1. **Power On**: Device auto-enters provisioning mode, LED slow-blinks
2. **Connect**: Connect to `ESP32-SoftAP-XXXX` hotspot (password: `12345678`)
3. **Auto Popup**: Browser auto-redirects to login page (or visit `192.168.4.1`)
4. **Login**: Enter default credentials `admin` / `synaflow2024`
5. **Configure**: Select WiFi, enter password, configure MQTT Broker
6. **Save**: Click save, device auto-connects to WiFi and MQTT

## Home Assistant Integration

The device auto-registers the following entities (15 total) after connecting to MQTT Broker:

### Sensors

| Entity | Description |
|--------|-------------|
| WiFi Name | Connected WiFi SSID |
| WiFi Signal Strength | RSSI value (dBm) |
| IP Address | Device IP |
| Free Heap | Remaining heap memory (B) |
| Uptime | Device uptime (s) |

### Switches

| Entity | Description |
|--------|-------------|
| LED Indicator | Control LED on/off |
| Lighting Control | Toggle lighting output |
| Ultrasonic Control | Toggle ultrasonic output |

### Numbers

| Entity | Range | Step |
|--------|-------|------|
| Lighting Frequency | 0 - 150 kHz | 0.1 Hz |
| Lighting Brightness | 0 - 100% | 0.1% |
| Ultrasonic Frequency | 0 - 150 kHz | 1 Hz |
| Ultrasonic Volume | 0 - 100% | 0.1% |

### Buttons

| Entity | Description |
|--------|-------------|
| Restart Device | Remote reboot ESP32 |

### Binary Sensors

| Entity | Description |
|--------|-------------|
| MQTT Status | MQTT connection status |

## MQTT Topics

### Command Topics (Device Receives)

```
{node_id}/led/set                    # LED switch: ON/OFF
{node_id}/light/power/set            # Lighting switch: ON/OFF
{node_id}/light/freq/set             # Lighting frequency: 0-150000
{node_id}/light/duty/set             # Lighting brightness: 0-100
{node_id}/sound/power/set            # Ultrasonic switch: ON/OFF
{node_id}/sound/freq/set             # Ultrasonic frequency: 0-150000
{node_id}/sound/vol/set              # Ultrasonic volume: 50-100
{node_id}/restart/set                # Restart device: RESTART
```

### State Topics (Device Publishes)

```
{node_id}/led/state                  # LED state: ON/OFF
{node_id}/light_power/state          # Lighting switch state
{node_id}/light_freq/state           # Lighting frequency
{node_id}/light_duty/state           # Lighting brightness
{node_id}/sound_power/state          # Ultrasonic switch state
{node_id}/sound_freq/state           # Ultrasonic frequency
{node_id}/sound_vol/state            # Ultrasonic volume
{node_id}/status                     # Availability: online/offline
```

Where `{node_id}` format is `esp32c3_{MAC_suffix}`, e.g. `esp32c3_5500e8`.

## Project Structure

```
softap_mqtt/
├── main/
│   ├── core/                  # Core framework (state machine, events, safe mode)
│   ├── wifi/                  # WiFi management (STA/AP mode switching)
│   ├── mqtt/                  # MQTT client, HA discovery, command handling
│   ├── config/                # NVS config (WiFi/MQTT/admin credentials)
│   ├── web/                   # HTTP server, auth middleware, OTA, pages
│   ├── drivers/               # Hardware drivers (PWM, GPIO, button, RMT)
│   └── utils/                 # System monitoring (heap, temperature, uptime)
├── CMakeLists.txt             # Top-level build config
├── sdkconfig.defaults         # SDK default config
├── custom_partitions.csv      # Custom partition table (dual OTA)
└── .github/workflows/         # GitHub Actions CI/CD
```

## Partition Table

| Partition | Size | Purpose |
|----------|------|---------|
| nvs | 24KB | Config storage (WiFi/MQTT/admin credentials/device state) |
| otadata | 8KB | OTA partition selection data |
| phy_init | 4KB | PHY initialization data |
| ota_0 | 1920KB | Application partition A |
| ota_1 | 1920KB | Application partition B |

## Default Configuration

| Setting | Default | Description |
|---------|---------|-------------|
| SoftAP SSID | `ESP32-SoftAP-{MAC}` | With MAC suffix |
| SoftAP Password | `12345678` | WPA2 |
| SoftAP Channel | 1 | Fixed channel |
| MQTT Broker | `mqtt://test.mosquitto.org` | Default test server |
| MQTT Port | 1883 | MQTT 3.1.1 |
| Lighting Frequency | 1 kHz | Default |
| Lighting Brightness | 80% | Default |
| Ultrasonic Frequency | 20 kHz | Default |
| Ultrasonic Volume | 75% | Default |

## CI/CD

The project uses GitHub Actions for automated builds:

- **Trigger**: Push to main branch, Pull Requests
- **Environment**: `espressif/esp-idf-ci-action@v1` (Docker)
- **ESP-IDF Version**: v5.1.2
- **Target Chip**: ESP32-C3
- **Artifact**: `firmware.bin` (merged firmware, ready to flash)

## Troubleshooting

### Cannot Enter Provisioning Mode
- Device auto-enters provisioning mode on first boot
- Long-press button for 5 seconds to factory reset

### WiFi Connection Failure
- Verify WiFi password is correct
- Check WiFi signal strength
- Check serial log for detailed error messages

### MQTT Connection Failure
- Verify MQTT Broker address and port are correct
- Check network connectivity
- Confirm Broker supports anonymous access or configure correct credentials

### Home Assistant Not Discovering Device
- Confirm MQTT integration is configured
- Check if device successfully connected to MQTT Broker
- Check if MQTT topics have messages published

### Web Page Shows 401 Unauthorized
- Token expired or device rebooted, re-login
- 5 consecutive auth failures will lock out for 30 seconds

## License

[MIT License](LICENSE)

## Acknowledgments

- [ESP-IDF](https://github.com/espressif/esp-idf) - Espressif IoT Development Framework
- [Home Assistant](https://www.home-assistant.io/) - Open-source smart home platform

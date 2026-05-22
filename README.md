# ESP32-C3 SoftAP MQTT 配网系统

基于 ESP32-C3 的物联网设备配网解决方案，支持 SoftAP 配网、MQTT 通信和 Home Assistant 自动发现。

## 功能特性

- **SoftAP 配网**: 设备首次上电自动创建 WiFi 热点，通过手机/电脑 Web 页面配置网络
- **Captive Portal**: 连接热点后自动弹出配网页面
- **MQTT 通信**: 连接 MQTT Broker，支持命令控制和状态上报
- **Home Assistant 集成**: 自动发现协议，自动注册传感器和开关实体
- **独立 PWM 控制**: 照明和声波两路 PWM 完全独立控制
- **WiFi 扫描**: 配网页面可扫描并选择周围 WiFi 网络
- **工厂重置**: 长按按键 5 秒恢复出厂设置

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

## 软件依赖

- ESP-IDF v5.1.2+
- CMake 3.16+
- Python 3.8+

## 快速开始

### 1. 克隆项目

```bash
git clone <repository-url>
cd softap_mqtt
```

### 2. 设置 ESP-IDF 环境

```bash
# 进入 ESP-IDF 目录
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
2. **连接热点**: 手机/电脑连接 `ESP32-SoftAP-XXXX` 热点
3. **自动弹窗**: 自动弹出配网页面（或浏览器访问 `192.168.4.1`）
4. **配置网络**:
   - 选择/输入 WiFi 名称和密码
   - 输入 MQTT Broker 地址（支持 `mqtt://` 和 `mqtts://`）
   - 点击保存
5. **自动连接**: 设备自动连接 WiFi 和 MQTT Broker

## Home Assistant 集成

设备支持 Home Assistant MQTT 自动发现协议，连接 MQTT Broker 后自动注册以下实体：

### 传感器
- **WiFi 名称**: 当前连接的 WiFi SSID
- **WiFi 信号强度**: RSSI 值 (dBm)
- **IP 地址**: 设备 IP
- **空闲内存**: 剩余堆内存
- **运行时间**: 设备运行时长

### 开关
- **LED 指示灯**: 控制 LED 开关
- **照明控制**: 开关照明输出
- **声波控制**: 开关声波输出

### 数值调节
- **照明频率**: 0 - 150 kHz，步进 1 Hz
- **照明亮度**: 0 - 100%，步进 0.1%
- **声波频率**: 0 - 150 kHz，步进 1 Hz
- **声波音量**: 50 - 100%，步进 0.1%

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

其中 `{node_id}` 格式为 `esp32c3_{mac后缀}`，例如 `esp32c3_309bdfc8`。

## REST API

配网页面提供以下 REST API：

### GET /api/config
获取当前配置

```json
{
  "wifi_ssid": "",
  "wifi_password": "",
  "mqtt_uri": "mqtt://test.mosquitto.org:1883"
}
```

### POST /api/config
保存配置

```json
{
  "wifi_ssid": "YourWiFi",
  "wifi_password": "YourPassword",
  "mqtt_uri": "mqtt://192.168.1.100:1883"
}
```

### GET /api/scan
扫描 WiFi 网络

```json
{
  "count": 2,
  "networks": [
    {"ssid": "Network1", "rssi": -45, "channel": 6, "auth": 3},
    {"ssid": "Network2", "rssi": -78, "channel": 11, "auth": 4}
  ]
}
```

### GET /api/status
获取设备状态

```json
{
  "state": "RUNNING",
  "wifi_connected": true,
  "mqtt_connected": true,
  "ip": "192.168.1.100"
}
```

## 项目结构

```
softap_mqtt/
├── main/
│   ├── core/              # 核心模块（状态机、事件处理）
│   ├── wifi/              # WiFi 管理
│   ├── mqtt/              # MQTT 客户端和 HA 发现
│   ├── config/            # NVS 配置管理
│   ├── web/               # HTTP/DNS 服务器
│   ├── drivers/           # 硬件驱动（PWM、GPIO、按键）
│   └── utils/             # 工具模块
├── CMakeLists.txt         # 项目配置
├── sdkconfig.defaults     # 默认配置
├── custom_partitions.csv  # 分区表
└── .github/workflows/     # CI/CD 配置
```

## 构建配置

### 分区表

| 分区 | 大小 | 用途 |
|------|------|------|
| nvs | 24KB | 配置存储 |
| phy_init | 4KB | PHY 数据 |
| factory | 1.5MB | 应用程序 |

### 默认配置

- SoftAP SSID: `ESP32-SoftAP-{MAC后缀}`
- SoftAP 密码: 无（开放网络）
- 默认 MQTT: `mqtt://test.mosquitto.org:1883`
- 照明默认频率: 1 kHz
- 照明默认亮度: 80%
- 声波默认频率: 20 kHz
- 声波默认音量: 75%

## CI/CD

项目使用 GitHub Actions 自动构建：

- **触发条件**: Push 到 main 分支、Pull Request
- **构建环境**: Ubuntu Latest
- **ESP-IDF 版本**: v5.1.2
- **目标芯片**: ESP32-C3
- **构建产物**: `merged.bin`（合并固件，可直接烧录）

构建状态: ![Build Status](https://github.com/{username}/softap_mqtt/workflows/ESP-IDF%20Build/badge.svg)

## 故障排除

### 无法进入配网模式
- 检查设备是否已配置（首次上电自动进入配网模式）
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

## 许可证

[MIT License](LICENSE)

## 贡献

欢迎提交 Issue 和 Pull Request。

## 致谢

- [ESP-IDF](https://github.com/espressif/esp-idf) - Espressif IoT Development Framework
- [Home Assistant](https://www.home-assistant.io/) - 开源智能家居平台


# 78/esp-wifi-connect 库移植指南

## 概述

本文档详细介绍如何将 `78/esp-wifi-connect` 库（版本 ~3.1.3）从 xiaozhi 项目移植到 ESP32 SoftAP MQTT 配置项目中。

## 1. 背景介绍

### 1.1 当前项目架构

当前项目结构：
```
E:\Espidf\softap_mqtt\
├── main\
│   ├── wifi\
│   │   ├── wifi_manager.c/h    # 基础 WiFi 管理
│   │   └── softap.c/h          # SoftAP 功能
│   ├── web\
│   │   ├── http_server.c/h     # HTTP 服务器
│   │   ├── api_handlers.c/h    # API 处理
│   │   └── web_page.h          # 配网页面
│   ├── core\
│   │   └── state_machine.c/h   # 状态机
│   └── app_main.c              # 主程序
└── CMakeLists.txt
```

### 1.2 xiaozhi 项目架构参考

xiaozhi 项目关键组件：
- 使用 `78/esp-wifi-connect` 作为核心配网库
- `WifiBoard` 类封装配网逻辑
- `WifiManager` 提供统一接口
- `SsidManager` 管理 SSID 持久化
- `DeviceStateMachine` 管理设备状态

## 2. 移植步骤

### 2.1 第一步：添加库依赖

在项目根目录创建或编辑 `idf_component.yml`：

```yaml
dependencies:
  78/esp-wifi-connect: ~3.1.3
```

#### 验证安装

运行命令自动下载依赖：
```bash
cd E:\Espidf\softap_mqtt
idf.py fullclean
idf.py reconfigure
```

### 2.2 第二步：修改 Kconfig.projbuild

在 `main/Kconfig.projbuild` 中添加配网配置选项：

```kconfig
menu "WiFi Configuration Method"
    help
        WiFi Configuration Method Selection

    config USE_HOTSPOT_WIFI_PROVISIONING
        bool "Hotspot"
        default y
        help
            Use WiFi Hotspot to transmit WiFi configuration data

    config USE_ACOUSTIC_WIFI_PROVISIONING
        bool "Acoustic"
        depends on 0  # 当前项目不使用
        help
            Use audio signal to transmit WiFi configuration data

    config USE_ESP_BLUFI_WIFI_PROVISIONING
        bool "Esp Blufi"
        depends on 0  # 当前项目不使用
        help
            Use esp blufi protocol to transmit WiFi configuration data
endmenu

menu "WiFi Connect Config"
    config ESP_WIFI_CONNECT_SSID_PREFIX
        string "SoftAP SSID Prefix"
        default "ESP32-Config"
        help
            Prefix for SoftAP SSID (MAC address will be appended)

    config ESP_WIFI_CONNECT_MAX_RETRY
        int "Maximum Connection Retries"
        range 1 10
        default 3
        help
            Maximum number of WiFi connection retries
endmenu
```

### 2.3 第三步：重构 wifi_manager

将现有的 `wifi/wifi_manager.c/h` 重新封装为适配 `esp-wifi-connect` 的接口。

#### 2.3.1 修改 wifi_manager.h

```c
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// WiFi Manager 状态
typedef enum {
    WIFI_STATE_IDLE = 0,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_CONFIG_MODE,
    WIFI_STATE_ERROR
} wifi_manager_state_t;

// 事件回调类型
typedef void (*wifi_manager_event_cb_t)(wifi_manager_state_t state, void* user_data);

// 配置结构体
typedef struct {
    char ssid_prefix[32];          // SSID 前缀
    uint8_t max_retry;             // 最大重试次数
    wifi_manager_event_cb_t event_cb;  // 事件回调
    void* user_data;               // 用户数据
} wifi_manager_config_t;

// 初始化 WiFi Manager
esp_err_t wifi_manager_init(wifi_manager_config_t* config);

// 启动 WiFi Station 模式
esp_err_t wifi_manager_start_station(void);

// 停止 WiFi
esp_err_t wifi_manager_stop(void);

// 启动配网模式
esp_err_t wifi_manager_start_config_mode(void);

// 停止配网模式
esp_err_t wifi_manager_stop_config_mode(void);

// 获取当前状态
wifi_manager_state_t wifi_manager_get_state(void);

// 获取 SoftAP SSID
const char* wifi_manager_get_ap_ssid(void);

// 获取配网页面地址
const char* wifi_manager_get_ap_web_url(void);

#endif // WIFI_MANAGER_H
```

#### 2.3.2 修改 wifi_manager.c

```c
#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 引入 esp-wifi-connect 头文件
#include "wifi_manager.h"  // 库提供的头文件
#include "wifi_station.h"
#include "ssid_manager.h"

static const char* TAG = "wifi_mgr_wrapper";
static wifi_manager_config_t g_config = {0};
static wifi_manager_state_t g_state = WIFI_STATE_IDLE;
static char g_ap_ssid[64] = {0};
static char g_web_url[64] = {0};

// 事件转发函数
static void wifi_event_forwarder(void* arg, esp_event_base_t event_base,
                                 int32_t event_id, void* event_data)
{
    // 处理 WiFi 事件并转发到用户回调
    if (g_config.event_cb) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                g_state = WIFI_STATE_CONNECTING;
                break;
            case WIFI_EVENT_STA_CONNECTED:
                g_state = WIFI_STATE_CONNECTED;
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                g_state = WIFI_STATE_DISCONNECTED;
                break;
            default:
                break;
        }
        g_config.event_cb(g_state, g_config.user_data);
    }
}

esp_err_t wifi_manager_init(wifi_manager_config_t* config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    // 复制配置
    memcpy(&g_config, config, sizeof(wifi_manager_config_t));

    // 初始化 NVS（如果未初始化）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed");
        return ret;
    }

    // 初始化 esp-wifi-connect
    WifiManagerConfig lib_config = {
        .ssid_prefix = config->ssid_prefix,
        .language = "zh-CN"  // 中文界面
    };
    
    // 这里需要根据库的实际 API 进行适配
    // wifi_manager_init(&lib_config);

    // 设置事件处理器
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, 
                                               &wifi_event_forwarder, NULL));

    ESP_LOGI(TAG, "WiFi Manager initialized");
    return ESP_OK;
}

esp_err_t wifi_manager_start_station(void)
{
    g_state = WIFI_STATE_CONNECTING;
    ESP_LOGI(TAG, "Starting WiFi station...");
    
    // 调用库的 API
    // wifi_manager_start_station();
    
    if (g_config.event_cb) {
        g_config.event_cb(g_state, g_config.user_data);
    }
    
    return ESP_OK;
}

esp_err_t wifi_manager_stop(void)
{
    g_state = WIFI_STATE_IDLE;
    ESP_LOGI(TAG, "Stopping WiFi...");
    
    // wifi_manager_stop();
    
    return ESP_OK;
}

esp_err_t wifi_manager_start_config_mode(void)
{
    g_state = WIFI_STATE_CONFIG_MODE;
    ESP_LOGI(TAG, "Starting config mode...");
    
    // 启动配网模式
    // wifi_manager_start_config_ap();
    
    // 获取 SSID 和 URL
    // snprintf(g_ap_ssid, sizeof(g_ap_ssid), "%s", wifi_manager_get_ap_ssid());
    // snprintf(g_web_url, sizeof(g_web_url), "%s", wifi_manager_get_ap_web_url());
    
    // 模拟数据用于编译
    snprintf(g_ap_ssid, sizeof(g_ap_ssid), "%s-XXXX", g_config.ssid_prefix);
    snprintf(g_web_url, sizeof(g_web_url), "http://192.168.4.1");
    
    ESP_LOGI(TAG, "Config AP: %s", g_ap_ssid);
    ESP_LOGI(TAG, "Web URL: %s", g_web_url);
    
    if (g_config.event_cb) {
        g_config.event_cb(g_state, g_config.user_data);
    }
    
    return ESP_OK;
}

esp_err_t wifi_manager_stop_config_mode(void)
{
    g_state = WIFI_STATE_IDLE;
    ESP_LOGI(TAG, "Stopping config mode...");
    
    // wifi_manager_stop_config_ap();
    
    return ESP_OK;
}

wifi_manager_state_t wifi_manager_get_state(void)
{
    return g_state;
}

const char* wifi_manager_get_ap_ssid(void)
{
    return g_ap_ssid;
}

const char* wifi_manager_get_ap_web_url(void)
{
    return g_web_url;
}
```

### 2.4 第四步：修改 main/CMakeLists.txt

更新 `main/CMakeLists.txt` 以正确链接 esp-wifi-connect 组件：

```cmake
idf_component_register(SRCS "app_main.c"
                            "wifi/wifi_manager.c"
                            "wifi/softap.c"
                            "web/http_server.c"
                            "web/api_handlers.c"
                            "core/state_machine.c"
                            "core/event_handlers.c"
                            "config/nvs_config.c"
                            "mqtt/mqtt_client.c"
                            "mqtt/ha_discovery.c"
                    INCLUDE_DIRS "."
                                   "wifi"
                                   "web"
                                   "core"
                                   "config"
                                   "mqtt"
                    REQUIRES esp_wifi
                             esp_event
                             nvs_flash
                             esp_http_server
                             esp_netif
                             esp_wifi_connect)  # 添加这个依赖
```

### 2.5 第五步：修改 app_main.c

重构主程序以使用新的 WiFi Manager：

```c
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "config/nvs_config.h"
#include "core/state_machine.h"
#include "wifi/wifi_manager.h"
#include "mqtt/app_mqtt.h"
#include "mqtt/ha_discovery.h"

static const char* TAG = "app_main";
static EventGroupHandle_t app_event_group;

#define APP_WIFI_CONNECTED_BIT BIT0
#define APP_WIFI_FAIL_BIT      BIT1

// WiFi Manager 事件回调
static void wifi_manager_event_handler(wifi_manager_state_t state, void* user_data)
{
    ESP_LOGI(TAG, "WiFi State: %d", state);
    
    switch (state) {
        case WIFI_STATE_CONNECTED:
            xEventGroupSetBits(app_event_group, APP_WIFI_CONNECTED_BIT);
            break;
        case WIFI_STATE_DISCONNECTED:
        case WIFI_STATE_ERROR:
            xEventGroupSetBits(app_event_group, APP_WIFI_FAIL_BIT);
            break;
        case WIFI_STATE_CONFIG_MODE:
            // 显示配网提示
            ESP_LOGI(TAG, "========================================");
            ESP_LOGI(TAG, "请连接 WiFi: %s", wifi_manager_get_ap_ssid());
            ESP_LOGI(TAG, "浏览器访问: %s", wifi_manager_get_ap_web_url());
            ESP_LOGI(TAG, "========================================");
            break;
        default:
            break;
    }
}

static void app_task(void* arg)
{
    app_config_t app_config = {0};
    app_state_t current_state = STATE_INIT;
    
    // 初始化 NVS 和加载配置
    nvs_init_config();
    nvs_load_all_config(&app_config);
    
    // 初始化 WiFi Manager
    wifi_manager_config_t wm_config = {
        .ssid_prefix = "ESP32-Config",
        .max_retry = 3,
        .event_cb = wifi_manager_event_handler,
        .user_data = NULL
    };
    wifi_manager_init(&wm_config);
    
    while (1) {
        app_state_t new_state = state_machine_get_current_state();
        
        if (new_state != current_state) {
            current_state = new_state;
            ESP_LOGI(TAG, "State changed to: %s", state_machine_get_state_name(current_state));
            
            switch (current_state) {
                case STATE_INIT:
                    ESP_LOGI(TAG, "Initializing...");
                    // 检查是否已有配置
                    if (app_config.is_configured && !app_config.first_boot) {
                        ESP_LOGI(TAG, "Config found, starting WiFi...");
                        state_machine_trigger_event(EVENT_CONFIG_RECEIVED);
                    } else {
                        ESP_LOGI(TAG, "No config, starting config mode...");
                        state_machine_trigger_event(EVENT_INIT_COMPLETE);
                    }
                    break;
                    
                case STATE_SOFTAP:
                    ESP_LOGI(TAG, "Starting config mode...");
                    wifi_manager_start_config_mode();
                    break;
                    
                case STATE_CONFIG:
                    ESP_LOGI(TAG, "Stopping config mode, connecting...");
                    wifi_manager_stop_config_mode();
                    // 等待连接事件
                    EventBits_t bits = xEventGroupWaitBits(app_event_group,
                            APP_WIFI_CONNECTED_BIT | APP_WIFI_FAIL_BIT,
                            pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));
                    
                    if (bits & APP_WIFI_CONNECTED_BIT) {
                        ESP_LOGI(TAG, "WiFi connected!");
                        state_machine_trigger_event(EVENT_WIFI_CONNECTED);
                    } else {
                        ESP_LOGW(TAG, "WiFi connection failed");
                        state_machine_trigger_event(EVENT_WIFI_DISCONNECTED);
                    }
                    break;
                    
                case STATE_CONNECTING:
                    ESP_LOGI(TAG, "Connecting MQTT...");
                    // 初始化 MQTT
                    // app_mqtt_init(app_config.mqtt_uri, app_config.mqtt_port, ...);
                    // app_mqtt_start();
                    break;
                    
                case STATE_RUNNING:
                    ESP_LOGI(TAG, "System running!");
                    // 定期任务
                    break;
                    
                case STATE_ERROR:
                    ESP_LOGE(TAG, "System error!");
                    state_machine_trigger_event(EVENT_RESET_CONFIG);
                    break;
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 SoftAP MQTT Config (with esp-wifi-connect)");
    ESP_LOGI(TAG, "Free heap: %" PRIu32 " bytes", esp_get_free_heap_size());
    
    // 创建事件组
    app_event_group = xEventGroupCreate();
    
    // 初始化状态机
    state_machine_init();
    
    // 启动应用任务
    xTaskCreate(&app_task, "app_task", 4096, NULL, 5, NULL);
}
```

### 2.6 第六步：配置 sdkconfig.defaults

更新 `sdkconfig.defaults` 添加推荐配置：

```ini
# WiFi 配置
CONFIG_ESP_WIFI_SSID="YourWiFi"
CONFIG_ESP_WIFI_PASSWORD=""
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=10
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=32
CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=32
CONFIG_ESP_WIFI_AMPDU_TX_ENABLED=y
CONFIG_ESP_WIFI_AMPDU_RX_ENABLED=y
CONFIG_ESP_WIFI_TX_BA_WIN=6
CONFIG_ESP_WIFI_RX_BA_WIN=6
CONFIG_ESP_WIFI_NVS_ENABLED=y

# LwIP 配置
CONFIG_LWIP_MAX_SOCKETS=10
CONFIG_LWIP_SO_REUSE=y
CONFIG_LWIP_SO_REUSE_RXALL=y

# 日志配置
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
CONFIG_LOG_COLORS=y
```

## 3. 实际集成策略（分阶段）

由于 esp-wifi-connect 是第三方库，建议分阶段进行集成测试：

### 阶段一：功能验证（不替换现有代码）

1. 先添加库依赖
2. 创建测试文件验证库功能
3. 确认 API 调用正确

### 阶段二：混合使用

1. 保留现有配网代码作为后备
2. 添加 Kconfig 选项切换配网方式
3. 逐步迁移功能

### 阶段三：完全迁移

1. 移除旧的配网实现
2. 优化配置和性能
3. 完善错误处理

## 4. 关键集成要点

### 4.1 事件处理流程

```
应用启动
  ↓
检查 NVS 配置
  ├─ 有配置 → 尝试连接 → 成功 → 正常运行
  │                ↓ 失败
  └─ 无配置/失败 → 启动配网模式
                     ↓
              显示配网提示（SSID/URL）
                     ↓
              用户通过网页配置
                     ↓
              保存配置 → 重启/重试连接
```

### 4.2 NVS 配置同步

确保以下数据同步：

- WiFi SSID 和密码
- MQTT 配置（URI、端口、用户名、密码）
- 设备配置信息

### 4.3 状态机集成

修改 `state_machine.c` 以适应新的事件流程：

```c
// 原有的状态保持不变
// 但事件处理需要适配新的 WiFi Manager API
```

## 5. 测试和验证

### 5.1 测试清单

- [ ] 编译通过且无警告
- [ ] 能正常启动进入配网模式
- [ ] SoftAP SSID 正确显示（包含 MAC 后缀）
- [ ] 网页配网界面可访问
- [ ] 可以扫描周围 WiFi 网络
- [ ] 配置后能正确保存到 NVS
- [ ] 重启后能自动连接 WiFi
- [ ] 连接失败后能自动重试
- [ ] 重试超时后能回到配网模式

### 5.2 调试输出

使用以下日志 Tag 进行调试：

- `wifi_mgr_wrapper` - 封装层日志
- `wifi_manager` - 库的日志
- `app_main` - 应用主流程

## 6. 回退方案

如果集成过程中出现问题，可以回退到原有的配网方案：

1. 移除 `idf_component.yml` 中的库依赖
2. 恢复原有的 `wifi_manager.c/h`
3. 恢复原有的 `web/` 模块
4. 恢复 `app_main.c` 中的调用

## 7. 参考资源

- xiaozhi 项目: `E:\Espidf\softap_mqtt\xiaozhi\xiaozhi-esp32-main\`
- esp-wifi-connect 组件文档: 查看 `managed_components/78__esp-wifi-connect/` (下载后)
- ESP-IDF WiFi 配网文档: https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32/api-reference/network/esp_wifi.html

## 8. 常见问题

### Q1: 库下载失败怎么办？

A: 检查网络连接，或手动从组件注册表下载并放到 `managed_components/` 目录。

### Q2: 如何切换回原有配网方式？

A: 通过 Kconfig 选项 `USE_HOTSPOT_WIFI_PROVISIONING` 或条件编译控制。

### Q3: 库的 API 与预期不符怎么办？

A: 参考 xiaozhi 项目的实际调用方式，并阅读库的头文件了解确切 API。

---

文档版本: 1.0
最后更新: 2026-05-21
维护者: ESP32 SoftAP MQTT 项目团队

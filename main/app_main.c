#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_task_wdt.h"
#include "esp_ota_ops.h"

#include "nvs_config.h"
#include "state_machine.h"
#include "wifi_manager.h"
#include "app_mqtt.h"
#include "ha_discovery.h"
#include "http_server.h"
#include "softap.h"
#include "event_handlers.h"
#include "dns_server.h"
#include "pwm_driver.h"
#include "gpio_control.h"
#include "button_driver.h"
#include "safe_mode.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// MQTT connection timeout (15 seconds)
#define MQTT_CONNECT_TIMEOUT_MS  15000

static EventGroupHandle_t wifi_event_group;

static const char *TAG = "app_main";

static app_config_t app_config;
static volatile bool restart_pending = false;

// MQTT connection timeout timer
static TimerHandle_t mqtt_timeout_timer = NULL;
static volatile bool mqtt_timeout_fired = false;

// Factory reset callback (called from button driver)
static void factory_reset_handler(void)
{
    ESP_LOGW(TAG, "Executing factory reset - clearing network config only");
    nvs_reset_network_config();
    restart_pending = true;
}

// MQTT connection timeout callback
static void mqtt_timeout_callback(TimerHandle_t timer)
{
    ESP_LOGW(TAG, "MQTT connection timeout!");
    mqtt_timeout_fired = true;
    state_machine_trigger_event(EVENT_TIMEOUT);
}

static void start_mqtt_timeout_timer(void)
{
    mqtt_timeout_fired = false;
    if (mqtt_timeout_timer == NULL) {
        mqtt_timeout_timer = xTimerCreate("mqtt_timeout", 
                                           pdMS_TO_TICKS(MQTT_CONNECT_TIMEOUT_MS),
                                           pdFALSE, NULL, mqtt_timeout_callback);
    }
    xTimerReset(mqtt_timeout_timer, 0);
}

static void stop_mqtt_timeout_timer(void)
{
    if (mqtt_timeout_timer) {
        xTimerStop(mqtt_timeout_timer, 0);
    }
    mqtt_timeout_fired = false;
}

static void app_init_state_machine(void)
{
    app_config.is_configured = nvs_is_config_valid(&app_config);
    state_machine_init();
    ESP_LOGI(TAG, "State machine initialized, configured: %d, first_boot: %d",
             app_config.is_configured, app_config.first_boot);

    if (app_config.is_configured && !app_config.first_boot) {
        ESP_LOGI(TAG, "Already configured, skipping SOFTAP");
        state_machine_trigger_event(EVENT_CONFIG_RECEIVED);
    } else {
        state_machine_trigger_event(EVENT_INIT_COMPLETE);
    }
}

static void app_task(void *arg)
{
    // Register task with watchdog
    esp_task_wdt_add(NULL);

    app_state_t current_state = STATE_INIT;

    while (1) {
        esp_task_wdt_reset();

        // Check restart pending (factory reset)
        if (restart_pending) {
            ESP_LOGI(TAG, "Restarting system...");
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_restart();
        }

        app_state_t new_state = state_machine_get_current_state();

        if (new_state != current_state) {
            current_state = new_state;
            ESP_LOGI(TAG, "State changed to: %s", state_machine_get_state_name(current_state));

            switch (current_state) {
                case STATE_INIT:
                    ESP_LOGI(TAG, "Initializing...");
                    gpio_set_led_mode(LED_MODE_SLOW_BLINK);
                    state_machine_trigger_event(EVENT_INIT_COMPLETE);
                    break;

                case STATE_SOFTAP: {
                    ESP_LOGI(TAG, "Starting SoftAP mode");
                    gpio_set_led_mode(LED_MODE_SLOW_BLINK);
                    char softap_ssid[32];
                    softap_generate_ssid_with_mac(softap_ssid, sizeof(softap_ssid));
                    event_handlers_set_auto_connect(false);
                    wifi_manager_start_softap(softap_ssid, "");
                    http_server_start();
                    dns_server_start(53, "192.168.4.1");
                    
                    // SOFTAP timeout: 5 minutes
                    TickType_t softap_start_tick = xTaskGetTickCount();
                    const uint32_t SOFTAP_TIMEOUT_MS = 5 * 60 * 1000;  // 5 minutes
                    uint32_t last_log_min = 0;
                    
                    while (state_machine_get_current_state() == STATE_SOFTAP && !restart_pending) {
                        esp_task_wdt_reset();  // Feed watchdog
                        
                        // Check timeout
                        uint32_t elapsed_ms = (xTaskGetTickCount() - softap_start_tick) * portTICK_PERIOD_MS;
                        uint32_t elapsed_min = elapsed_ms / 60000;
                        
                        // Log every minute
                        if (elapsed_min != last_log_min) {
                            last_log_min = elapsed_min;
                            ESP_LOGI(TAG, "SOFTAP running for %lu min, waiting for config...", elapsed_min);
                        }
                        
                        // Timeout check
                        if (elapsed_ms > SOFTAP_TIMEOUT_MS) {
                            ESP_LOGW(TAG, "SOFTAP timeout after 5 minutes, restarting...");
                            vTaskDelay(pdMS_TO_TICKS(1000));
                            esp_restart();
                        }
                        
                        vTaskDelay(pdMS_TO_TICKS(1000));
                    }
                    break;
                }

                case STATE_CONFIG: {
                    ESP_LOGI(TAG, "Connecting to WiFi...");
                    gpio_set_led_mode(LED_MODE_ON);
                    dns_server_stop();
                    http_server_stop();
                    wifi_manager_stop_softap();
                    vTaskDelay(pdMS_TO_TICKS(1000));

                    // Reload config (only once, not in a loop)
                    nvs_load_all_config(&app_config);
                    ESP_LOGI(TAG, "Config: SSID=%s", app_config.wifi_ssid);

                    // Create event group for WiFi connection sync
                    wifi_event_group = xEventGroupCreate();
                    event_handlers_set_wifi_event_group(wifi_event_group);

                    // Non-blocking WiFi connection with exponential backoff
                    bool wifi_connected = false;
                    int retry_count = 0;
                    int retry_delay_ms = 5000;
                    const int max_retry_delay_ms = 60000;

                    while (!wifi_connected && !restart_pending) {
                        esp_task_wdt_reset();  // Feed watchdog during retry loop

                        if (retry_count > 0) {
                            ESP_LOGW(TAG, "WiFi retry %d (delay: %d ms)...", retry_count, retry_delay_ms);
                            vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
                            retry_delay_ms *= 2;
                            if (retry_delay_ms > max_retry_delay_ms) {
                                retry_delay_ms = max_retry_delay_ms;
                            }
                        }

                        event_handlers_set_auto_connect(true);
                        wifi_manager_connect_sta(app_config.wifi_ssid, app_config.wifi_password);

                        ESP_LOGI(TAG, "Waiting for WiFi (attempt %d)...", retry_count + 1);
                        EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                                               WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                                               pdTRUE,
                                                               pdFALSE,
                                                               pdMS_TO_TICKS(20000));

                        if (bits & WIFI_CONNECTED_BIT) {
                            ESP_LOGI(TAG, "WiFi connected");
                            wifi_connected = true;
                        } else {
                            ESP_LOGW(TAG, "WiFi attempt %d failed", retry_count + 1);
                            wifi_manager_stop_sta();
                            vTaskDelay(pdMS_TO_TICKS(1000));
                        }
                        retry_count++;
                    }

                    vEventGroupDelete(wifi_event_group);
                    wifi_event_group = NULL;

                    if (wifi_connected) {
                        state_machine_trigger_event(EVENT_WIFI_CONNECTED);
                    } else if (!restart_pending) {
                        // WiFi failed after all retries, go back to SOFTAP
                        ESP_LOGW(TAG, "WiFi connection failed, returning to SOFTAP");
                        state_machine_trigger_event(EVENT_TIMEOUT);
                    }
                    break;
                }

                case STATE_CONNECTING:
                    ESP_LOGI(TAG, "Connecting to MQTT...");
                    gpio_set_led_mode(LED_MODE_FAST_BLINK);
                    app_mqtt_connect(app_config.mqtt_uri, app_config.mqtt_port,
                                      app_config.mqtt_username, app_config.mqtt_password);
                    // Start timeout timer - if MQTT doesn't connect within timeout,
                    // the timer will fire EVENT_TIMEOUT which keeps us in CONNECTING
                    // (ESP-MQTT has its own reconnect, so this is just a safety net)
                    start_mqtt_timeout_timer();
                    break;

                case STATE_RUNNING: {
                    ESP_LOGI(TAG, "System running!");
                    gpio_set_led_mode(LED_MODE_OFF);
                    stop_mqtt_timeout_timer();
                    http_server_start();  // Start HTTP server for OTA and config in WiFi mode
                    ha_discovery_publish_states();

                    // Periodic state reporting (every 30 seconds)
                    int report_count = 0;
                    while (state_machine_get_current_state() == STATE_RUNNING && !restart_pending) {
                        esp_task_wdt_reset();
                        vTaskDelay(pdMS_TO_TICKS(1000));
                        report_count++;
                        if (report_count >= 30) {
                            report_count = 0;
                            ha_discovery_publish_states();
                        }
                    }
                    break;
                }

                case STATE_ERROR:
                    ESP_LOGE(TAG, "System in error state");
                    gpio_set_led_mode(LED_MODE_WARN_BLINK);
                    // Stop MQTT timeout timer
                    stop_mqtt_timeout_timer();
                    // Don't clear config! Wait for auto-recovery via TIMEOUT event
                    // which transitions back to CONFIG to retry WiFi connection
                    break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 SoftAP MQTT Config");
    ESP_LOGI(TAG, "Free heap: %" PRIu32 " bytes", esp_get_free_heap_size());

    // Check OTA rollback state
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "OTA: New firmware booted, marking as valid");
            esp_err_t ret = esp_ota_mark_app_valid_cancel_rollback();
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "OTA: Firmware marked as valid");
            } else {
                ESP_LOGE(TAG, "OTA: Failed to mark valid: %s", esp_err_to_name(ret));
            }
        } else if (ota_state == ESP_OTA_IMG_ABORTED) {
            ESP_LOGW(TAG, "OTA: Previous update was aborted, rollback occurred");
        } else if (ota_state == ESP_OTA_IMG_VALID) {
            ESP_LOGI(TAG, "OTA: Current firmware is valid");
        }
    }

    esp_err_t ret = nvs_init_config();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed, restarting");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    }

    nvs_load_all_config(&app_config);
    ESP_LOGI(TAG, "Config loaded: SSID=%s, MQTT=%s:%d",
             app_config.wifi_ssid, app_config.mqtt_uri, app_config.mqtt_port);

    // Initialize drivers with error handling
    ret = gpio_control_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO control init failed: %s", esp_err_to_name(ret));
        safe_mode_enter(SAFE_MODE_ERR_DRIVER_INIT, "GPIO control init failed");
        return;
    }
    
    ret = button_init(factory_reset_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Button init failed: %s", esp_err_to_name(ret));
        // Non-critical, continue
    }
    
    ret = pwmDriverInit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PWM driver init failed: %s", esp_err_to_name(ret));
        safe_mode_enter(SAFE_MODE_ERR_DRIVER_INIT, "PWM driver init failed");
        return;
    }
    
    // Load and restore device state using independent entity architecture
    device_state_t device_state;
    nvs_load_device_state(&device_state);
    
    // Stage all parameters first, then apply per entity
    pwmSetLightEnable(device_state.light_enabled);
    pwmSetLightFreq(device_state.light_freq);
    pwmSetLightDuty(device_state.light_duty);
    pwmApplyLight();
    
    pwmSetSoundEnable(device_state.sound_enabled);
    pwmSetSoundFreq(device_state.sound_freq);
    pwmSetSoundDuty(device_state.sound_duty);
    pwmApplySound();

    // Initialize network stack
    ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Netif init failed: %s", esp_err_to_name(ret));
        safe_mode_enter(SAFE_MODE_ERR_NETIF_INIT, "Network interface init failed");
        return;
    }
    
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Event loop init failed: %s", esp_err_to_name(ret));
        safe_mode_enter(SAFE_MODE_ERR_EVENT_LOOP, "Event loop init failed");
        return;
    }

    app_init_state_machine();
    wifi_manager_init();
    event_handlers_register_wifi_handler();

    xTaskCreate(&app_task, "app_task", 4096, NULL, 5, NULL);
}

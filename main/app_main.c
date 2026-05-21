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

#include "nvs_config.h"
#include "state_machine.h"
#include "wifi_manager.h"
#include "app_mqtt.h"
#include "ha_discovery.h"
#include "http_server.h"
#include "softap.h"
#include "event_handlers.h"
#include "dns_server.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t wifi_event_group;

static const char *TAG = "app_main";

static app_config_t app_config;
static bool restart_pending = false;

static void app_init_state_machine(void)
{
    app_config.is_configured = nvs_is_config_valid(&app_config);
    state_machine_init();
    ESP_LOGI(TAG, "State machine initialized, configured: %d, first_boot: %d",
             app_config.is_configured, app_config.first_boot);

    // Check if already configured (not first boot and config is valid)
    if (app_config.is_configured && !app_config.first_boot) {
        // Already configured, skip SOFTAP and go directly to CONFIG state
        // This will trigger WiFi connection in app_task
        ESP_LOGI(TAG, "Already configured, skipping SOFTAP, going to CONFIG state to connect WiFi");
        state_machine_trigger_event(EVENT_CONFIG_RECEIVED);
    } else {
        // First boot or no config, go to SOFTAP for configuration
        state_machine_trigger_event(EVENT_INIT_COMPLETE);
    }
}

static void app_task(void *arg)
{
    app_state_t current_state = STATE_INIT;

    while (1) {
        app_state_t new_state = state_machine_get_current_state();

        if (new_state != current_state) {
            current_state = new_state;
            ESP_LOGI(TAG, "State changed to: %s", state_machine_get_state_name(current_state));

            switch (current_state) {
                case STATE_INIT:
                    ESP_LOGI(TAG, "Initializing...");
                    nvs_load_all_config(&app_config);
                    state_machine_trigger_event(EVENT_INIT_COMPLETE);
                    break;

                case STATE_SOFTAP:
                    ESP_LOGI(TAG, "Starting SoftAP mode");
                    // Generate SSID with MAC suffix for unique identification
                    char softap_ssid[32];
                    softap_generate_ssid_with_mac(softap_ssid, sizeof(softap_ssid));
                    // Start SoftAP with empty password (open network)
                    event_handlers_set_auto_connect(false);
                    wifi_manager_start_softap(softap_ssid, "");
                    http_server_start();
                    dns_server_start(53, "192.168.4.1");
                    break;

                case STATE_CONFIG:
                    ESP_LOGI(TAG, "Config received, switching to STA mode");
                    dns_server_stop();
                    http_server_stop();
                    wifi_manager_stop_softap();
                    vTaskDelay(pdMS_TO_TICKS(1000));

                    // Reload config after saving (to get updated values)
                    nvs_load_all_config(&app_config);
                    ESP_LOGI(TAG, "Reloaded config: SSID=%s, password_len=%d",
                             app_config.wifi_ssid, strlen(app_config.wifi_password));

                    // Create event group for WiFi connection sync
                    wifi_event_group = xEventGroupCreate();
                    event_handlers_set_wifi_event_group(wifi_event_group);

                    // Start WiFi STA connection with retry
                    bool wifi_connected = false;
                    int retry_count = 0;
                    int max_retries = 3;

                    while (retry_count < max_retries && !wifi_connected) {
                        if (retry_count > 0) {
                            ESP_LOGW(TAG, "WiFi retry %d/%d...", retry_count, max_retries);
                            vTaskDelay(pdMS_TO_TICKS(5000));
                        }

                        event_handlers_set_auto_connect(true);
                        wifi_manager_connect_sta(app_config.wifi_ssid, app_config.wifi_password);

                        ESP_LOGI(TAG, "Waiting for WiFi connection (attempt %d/%d)...",
                                 retry_count + 1, max_retries);
                        EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                                               WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                                               pdTRUE,  // Clear bits on exit
                                                               pdFALSE,
                                                               pdMS_TO_TICKS(15000));

                        if (bits & WIFI_CONNECTED_BIT) {
                            ESP_LOGI(TAG, "WiFi connected successfully");
                            wifi_connected = true;
                        } else {
                            ESP_LOGW(TAG, "WiFi connection attempt %d failed", retry_count + 1);
                            wifi_manager_stop_sta();
                            vTaskDelay(pdMS_TO_TICKS(1000));
                        }
                        retry_count++;
                    }

                    if (wifi_connected) {
                        state_machine_trigger_event(EVENT_WIFI_CONNECTED);
                    } else {
                        ESP_LOGE(TAG, "WiFi connection failed after %d retries, entering SOFTAP", max_retries);
                        state_machine_trigger_event(EVENT_WIFI_DISCONNECTED);
                    }

                    vEventGroupDelete(wifi_event_group);
                    wifi_event_group = NULL;
                    break;

                case STATE_CONNECTING:
                    ESP_LOGI(TAG, "Connecting to MQTT...");
                    app_mqtt_connect(app_config.mqtt_uri, app_config.mqtt_port,
                                      app_config.mqtt_username, app_config.mqtt_password);
                    break;

                case STATE_RUNNING: {
                    ESP_LOGI(TAG, "System running!");
                    // Publish initial states
                    ha_discovery_publish_states();

                    // Periodic state reporting (every 30 seconds)
                    int report_count = 0;
                    while (state_machine_get_current_state() == STATE_RUNNING) {
                        vTaskDelay(pdMS_TO_TICKS(1000));
                        report_count++;
                        if (report_count >= 30) {
                            report_count = 0;
                            ha_discovery_publish_states();
                            ESP_LOGI(TAG, "Periodic state published");
                        }
                    }
                    break;
                }

                case STATE_ERROR:
                    ESP_LOGE(TAG, "System in error state, reverting to SoftAP mode");
                    // Stop any active connections
                    wifi_manager_stop_softap();
                    vTaskDelay(pdMS_TO_TICKS(500));
                    // Trigger reset config event to go back to INIT -> SOFTAP
                    state_machine_trigger_event(EVENT_RESET_CONFIG);
                    break;
            }
        }

        if (restart_pending) {
            ESP_LOGI(TAG, "Restarting system...");
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_restart();
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 SoftAP MQTT Config");
    ESP_LOGI(TAG, "Free heap: %" PRIu32 " bytes", esp_get_free_heap_size());

    esp_err_t ret = nvs_init_config();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed, restaring");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    }

    nvs_load_all_config(&app_config);
    ESP_LOGI(TAG, "Config loaded: SSID=%s, MQTT=%s:%d",
             app_config.wifi_ssid, app_config.mqtt_uri, app_config.mqtt_port);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    app_init_state_machine();
    wifi_manager_init();
    event_handlers_register_wifi_handler();

    xTaskCreate(&app_task, "app_task", 4096, NULL, 5, NULL);
}

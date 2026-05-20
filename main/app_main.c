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
#include "mqtt_client.h"
#include "http_server.h"
#include "softap.h"
#include "event_handlers.h"

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
    ESP_LOGI(TAG, "State machine initialized, current state: %s",
             state_machine_get_state_name(state_machine_get_current_state()));
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
                    wifi_manager_start_softap(DEFAULT_SOFTAP_SSID, DEFAULT_SOFTAP_PASSWORD);
                    http_server_start();
                    break;
                    
                case STATE_CONFIG:
                    ESP_LOGI(TAG, "Config received, switching to STA mode");
                    http_server_stop();
                    wifi_manager_stop_softap();
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    
                    // Create event group for WiFi connection sync
                    wifi_event_group = xEventGroupCreate();
                    event_handlers_set_wifi_event_group(wifi_event_group);
                    
                    // Start WiFi STA connection
                    wifi_manager_connect_sta(app_config.wifi_ssid, app_config.wifi_password);
                    
                    // Wait for WiFi connection with timeout (30 seconds)
                    ESP_LOGI(TAG, "Waiting for WiFi connection...");
                    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                                           pdFALSE,
                                                           pdFALSE,
                                                           pdMS_TO_TICKS(30000));
                    
                    if (bits & WIFI_CONNECTED_BIT) {
                        ESP_LOGI(TAG, "WiFi connected successfully");
                        state_machine_trigger_event(EVENT_WIFI_CONNECTED);
                    } else {
                        ESP_LOGE(TAG, "WiFi connection failed or timeout");
                        state_machine_trigger_event(EVENT_WIFI_DISCONNECTED);
                    }
                    
                    vEventGroupDelete(wifi_event_group);
                    wifi_event_group = NULL;
                    break;
                    
                case STATE_CONNECTING:
                    ESP_LOGI(TAG, "Connecting to MQTT...");
                    mqtt_client_connect(app_config.mqtt_uri, app_config.mqtt_port,
                                      app_config.mqtt_username, app_config.mqtt_password);
                    break;
                    
                case STATE_RUNNING:
                    ESP_LOGI(TAG, "System running!");
                    mqtt_client_subscribe("esp32/command", 0);
                    break;
                    
                case STATE_ERROR:
                    ESP_LOGE(TAG, "System in error state, reverting to SoftAP");
                    restart_pending = true;
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
    mqtt_client_init();
    
    xTaskCreate(&app_task, "app_task", 4096, NULL, 5, NULL);
}

#include "event_handlers.h"
#include "esp_log.h"
#include "esp_wifi.h"

static const char *TAG = "event_handlers";

static esp_event_handler_instance_t wifi_event_handler_instance = NULL;
static esp_event_handler_instance_t ip_event_handler_instance = NULL;
static EventGroupHandle_t wifi_event_group = NULL;
static bool auto_connect_enabled = false;

void event_handlers_set_wifi_event_group(EventGroupHandle_t event_group)
{
    wifi_event_group = event_group;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi station started");
                if (auto_connect_enabled) {
                    esp_wifi_connect();
                }
                break;
            case WIFI_EVENT_STA_STOP:
                ESP_LOGI(TAG, "WiFi station stopped");
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "WiFi station connected");
                break;
            case WIFI_EVENT_STA_DISCONNECTED: {
                int reason = 0;
                if (event_data) {
                    wifi_event_sta_disconnected_t *disconn = (wifi_event_sta_disconnected_t *)event_data;
                    reason = disconn->reason;
                }
                ESP_LOGW(TAG, "WiFi station disconnected, reason: %d", reason);
                if (wifi_event_group) {
                    xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
                }
                // Do NOT trigger state machine events here - let app_task handle it
                break;
            }
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP:
                ESP_LOGI(TAG, "WiFi station got IP");
                if (wifi_event_group) {
                    xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
                }
                // Do NOT trigger state machine events here - let app_task handle it
                break;
            default:
                break;
        }
    }
}

esp_err_t event_handlers_init(void)
{
    ESP_LOGI(TAG, "Event handlers init");
    return ESP_OK;
}

void event_handlers_register_wifi_handler(void)
{
    esp_err_t ret;
    
    ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &wifi_event_handler_instance);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WiFi event handler: %s", esp_err_to_name(ret));
        return;
    }
    
    ret = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &ip_event_handler_instance);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP event handler: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "WiFi event handlers registered");
}

void event_handlers_set_auto_connect(bool enabled) {
    auto_connect_enabled = enabled;
}

#include "safe_mode.h"
#include "esp_log.h"
#include "esp_system.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gpio_control.h"
#include "wifi_manager.h"
#include "http_server.h"
#include "dns_server.h"
#include "softap.h"

static const char *TAG = "SAFE_MODE";

static volatile bool safe_mode_active = false;
static safe_mode_error_t safe_mode_error = SAFE_MODE_ERR_NONE;

// Error code to string
static const char *error_to_string(safe_mode_error_t error)
{
    switch (error) {
        case SAFE_MODE_ERR_NONE: return "NONE";
        case SAFE_MODE_ERR_NVS_INIT: return "NVS_INIT_FAILED";
        case SAFE_MODE_ERR_NETIF_INIT: return "NETIF_INIT_FAILED";
        case SAFE_MODE_ERR_EVENT_LOOP: return "EVENT_LOOP_FAILED";
        case SAFE_MODE_ERR_WIFI_INIT: return "WIFI_INIT_FAILED";
        case SAFE_MODE_ERR_MQTT_INIT: return "MQTT_INIT_FAILED";
        case SAFE_MODE_ERR_DRIVER_INIT: return "DRIVER_INIT_FAILED";
        default: return "UNKNOWN";
    }
}

void safe_mode_enter(safe_mode_error_t error, const char *msg)
{
    if (safe_mode_active) {
        return;  // Already in safe mode
    }

    safe_mode_active = true;
    safe_mode_error = error;

    ESP_LOGE(TAG, "============================================");
    ESP_LOGE(TAG, "ENTERING SAFE MODE");
    ESP_LOGE(TAG, "Error: %s", error_to_string(error));
    if (msg) {
        ESP_LOGE(TAG, "Message: %s", msg);
    }
    ESP_LOGE(TAG, "============================================");

    // Stop normal operation
    // Note: We don't restart - we stay in safe mode for reconfiguration

    // Start error indication - rapid LED blink
    gpio_set_led_mode(LED_MODE_WARN_BLINK);

    // Try to start SoftAP for reconfiguration
    ESP_LOGI(TAG, "Starting SoftAP for recovery...");

    char softap_ssid[32];
    softap_generate_ssid_with_mac(softap_ssid, sizeof(softap_ssid));

    esp_err_t ret = wifi_manager_start_softap(softap_ssid, "");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start SoftAP: %s", esp_err_to_name(ret));
        // Even if SoftAP fails, we stay in safe mode with LED blink
    } else {
        http_server_start();
        dns_server_start(53, "192.168.4.1");
        ESP_LOGI(TAG, "Recovery SoftAP started: %s", softap_ssid);
    }

    ESP_LOGI(TAG, "Safe mode active. Connect to SoftAP to reconfigure.");

    // Stay in safe mode indefinitely
    while (safe_mode_active) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

bool safe_mode_is_active(void)
{
    return safe_mode_active;
}

safe_mode_error_t safe_mode_get_error(void)
{
    return safe_mode_error;
}

void safe_mode_exit_and_restart(void)
{
    ESP_LOGI(TAG, "Exiting safe mode and restarting...");
    safe_mode_active = false;
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

esp_err_t safe_mode_handle_error(esp_err_t err, const char *msg, bool enter_safe_mode)
{
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error: %s - %s", msg, esp_err_to_name(err));
        if (enter_safe_mode) {
            // Map ESP error to safe mode error
            safe_mode_error_t sm_error = SAFE_MODE_ERR_NONE;
            if (strstr(msg, "NVS")) sm_error = SAFE_MODE_ERR_NVS_INIT;
            else if (strstr(msg, "netif")) sm_error = SAFE_MODE_ERR_NETIF_INIT;
            else if (strstr(msg, "event")) sm_error = SAFE_MODE_ERR_EVENT_LOOP;
            else if (strstr(msg, "WiFi")) sm_error = SAFE_MODE_ERR_WIFI_INIT;
            else if (strstr(msg, "MQTT")) sm_error = SAFE_MODE_ERR_MQTT_INIT;
            else sm_error = SAFE_MODE_ERR_DRIVER_INIT;

            safe_mode_enter(sm_error, msg);
        }
    }
    return err;
}

#include <string.h>
#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "softap.h"

static const char *TAG = "wifi_manager";
static wifi_manager_config_t current_config = {0};

// Static netif handles for proper lifecycle management
static esp_netif_t *sta_netif = NULL;
static esp_netif_t *ap_netif = NULL;

esp_err_t wifi_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing WiFi manager");
    
    // esp_netif_init() is called in app_main, don't call it here
    
    return ESP_OK;
}

esp_err_t wifi_manager_connect_sta(const char *ssid, const char *password)
{
    ESP_LOGI(TAG, "Connecting to WiFi station: %s", ssid);
    
    // Destroy existing netifs to avoid conflicts
    if (sta_netif != NULL) {
        esp_netif_destroy(sta_netif);
        sta_netif = NULL;
    }
    if (ap_netif != NULL) {
        esp_netif_destroy(ap_netif);
        ap_netif = NULL;
    }
    
    sta_netif = esp_netif_create_default_wifi_sta();
    if (sta_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create STA netif");
        return ESP_FAIL;
    }
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password) - 1);
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    strncpy(current_config.ssid, ssid, sizeof(current_config.ssid) - 1);
    strncpy(current_config.password, password, sizeof(current_config.password) - 1);
    
    return ESP_OK;
}

esp_err_t wifi_manager_stop_sta(void)
{
    ESP_LOGI(TAG, "Stopping WiFi station");
    
    esp_wifi_stop();
    esp_wifi_deinit();
    
    // Destroy STA netif
    if (sta_netif != NULL) {
        esp_netif_destroy(sta_netif);
        sta_netif = NULL;
    }
    
    current_config.sta_connected = false;
    
    return ESP_OK;
}

bool wifi_manager_is_sta_connected(void)
{
    return current_config.sta_connected;
}

int8_t wifi_manager_get_rssi(void)
{
    return current_config.rssi;
}

esp_err_t wifi_manager_start_softap(const char *ssid, const char *password)
{
    ESP_LOGI(TAG, "Starting WiFi SoftAP: %s", ssid);
    return softap_start_with_netif(ssid, password, &sta_netif, &ap_netif);
}

esp_err_t wifi_manager_stop_softap(void)
{
    ESP_LOGI(TAG, "Stopping WiFi SoftAP");
    return softap_stop_with_netif(&sta_netif, &ap_netif);
}

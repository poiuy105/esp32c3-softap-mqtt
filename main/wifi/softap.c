#include <string.h>
#include "softap.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"

static const char *TAG = "softap";
static bool ap_running = false;

esp_err_t softap_start(const char *ssid, const char *password)
{
    ESP_LOGI(TAG, "Starting SoftAP: %s", ssid);
    
    esp_netif_create_default_wifi_ap();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.ap.ssid, ssid, sizeof(wifi_cfg.ap.ssid) - 1);
    strncpy((char *)wifi_cfg.ap.password, password, sizeof(wifi_cfg.ap.password) - 1);
    wifi_cfg.ap.channel = DEFAULT_SOFTAP_CHANNEL;
    wifi_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_cfg.ap.max_connection = 4;
    wifi_cfg.ap.beacon_interval = 100;
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ap_running = true;
    ESP_LOGI(TAG, "SoftAP started successfully");
    ESP_LOGI(TAG, "SoftAP IP: 192.168.4.1");
    
    return ESP_OK;
}

esp_err_t softap_stop(void)
{
    ESP_LOGI(TAG, "Stopping SoftAP");
    
    esp_wifi_stop();
    esp_wifi_deinit();
    ap_running = false;
    
    ESP_LOGI(TAG, "SoftAP stopped");
    return ESP_OK;
}

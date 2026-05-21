#include "device_info.h"
#include "esp_log.h"
#include "esp_mac.h"

static const char *TAG = "device_info";

static char node_id[32] = {0};
static char mac_str[18] = {0};
static char device_name[32] = {0};
static bool initialized = false;

void device_info_init(void)
{
    if (initialized) return;
    
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(node_id, sizeof(node_id), "esp32c3_%02x%02x%02x%02x",
             mac[2], mac[3], mac[4], mac[5]);
    snprintf(device_name, sizeof(device_name), "ESP32-C3 %02X:%02X:%02X",
             mac[3], mac[4], mac[5]);
    
    ESP_LOGI(TAG, "Device ID: %s, MAC: %s", node_id, mac_str);
    initialized = true;
}

const char* device_info_get_node_id(void)
{
    if (!initialized) device_info_init();
    return node_id;
}

const char* device_info_get_mac_string(void)
{
    if (!initialized) device_info_init();
    return mac_str;
}

const char* device_info_get_device_name(void)
{
    if (!initialized) device_info_init();
    return device_name;
}

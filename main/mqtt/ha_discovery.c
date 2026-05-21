#include <string.h>
#include <stdio.h>
#include "ha_discovery.h"
#include "app_mqtt.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "cJSON.h"

static const char *TAG = "ha_discovery";

// Node ID based on MAC address (e.g., "esp32c3_aabbccdd")
static char node_id[32] = {0};
// MAC address string (e.g., "AA:BB:CC:DD:EE:FF")
static char mac_str[18] = {0};
// Device name with MAC suffix (e.g., "ESP32-C3 AA:BB:CC")
static char device_name[32] = {0};

// Topic buffers
static char avail_topic[64] = {0};

// Initialize node_id from MAC address
static void init_node_id(void)
{
    if (node_id[0] != '\0') return;

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(node_id, sizeof(node_id), "esp32c3_%02x%02x%02x%02x",
             mac[2], mac[3], mac[4], mac[5]);
    snprintf(device_name, sizeof(device_name), "ESP32-C3 %02X:%02X:%02X",
             mac[3], mac[4], mac[5]);
    snprintf(avail_topic, sizeof(avail_topic), "%s/status", node_id);

    ESP_LOGI(TAG, "Node ID: %s, Device: %s", node_id, device_name);
}

// Build the common device info JSON object
static cJSON* build_device_info(void)
{
    cJSON *device = cJSON_CreateObject();
    cJSON_AddStringToObject(device, "name", device_name);
    cJSON_AddStringToObject(device, "manufacturer", "Espressif");
    cJSON_AddStringToObject(device, "model", "ESP32-C3");
    cJSON_AddStringToObject(device, "identifiers", node_id);

    cJSON *connections = cJSON_CreateArray();
    cJSON *conn = cJSON_CreateArray();
    cJSON_AddItemToArray(conn, cJSON_CreateString("mac"));
    cJSON_AddItemToArray(conn, cJSON_CreateString(mac_str));
    cJSON_AddItemToArray(connections, conn);
    cJSON_AddItemToObject(device, "connections", connections);

    return device;
}

// Publish a sensor discovery config
static esp_err_t publish_sensor_config(const char *object_id, const char *name,
                                        const char *device_class,
                                        const char *unit, const char *state_class)
{
    char topic[128];
    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/%s/config", node_id, object_id);

    char state_topic[96];
    snprintf(state_topic, sizeof(state_topic), "%s/%s/state", node_id, object_id);

    char unique_id[64];
    snprintf(unique_id, sizeof(unique_id), "%s_%s", node_id, object_id);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", name);
    cJSON_AddStringToObject(root, "state_topic", state_topic);
    cJSON_AddStringToObject(root, "unique_id", unique_id);
    cJSON_AddStringToObject(root, "availability_topic", avail_topic);
    cJSON_AddStringToObject(root, "payload_available", "online");
    cJSON_AddStringToObject(root, "payload_not_available", "offline");

    if (device_class && strlen(device_class) > 0) {
        cJSON_AddStringToObject(root, "device_class", device_class);
    }
    if (unit && strlen(unit) > 0) {
        cJSON_AddStringToObject(root, "unit_of_measurement", unit);
    }
    if (state_class && strlen(state_class) > 0) {
        cJSON_AddStringToObject(root, "state_class", state_class);
    }

    cJSON_AddItemToObject(root, "device", build_device_info());

    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);

    if (json_str == NULL) {
        ESP_LOGE(TAG, "Failed to build JSON for sensor %s", object_id);
        return ESP_FAIL;
    }

    esp_err_t err = app_mqtt_publish(topic, json_str, 1, 1);  // QoS 1, retain
    free(json_str);
    return err;
}

// Publish a binary_sensor discovery config
static esp_err_t publish_binary_sensor_config(const char *object_id, const char *name,
                                               const char *device_class)
{
    char topic[128];
    snprintf(topic, sizeof(topic), "homeassistant/binary_sensor/%s/%s/config", node_id, object_id);

    char state_topic[96];
    snprintf(state_topic, sizeof(state_topic), "%s/%s/state", node_id, object_id);

    char unique_id[64];
    snprintf(unique_id, sizeof(unique_id), "%s_%s", node_id, object_id);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", name);
    cJSON_AddStringToObject(root, "state_topic", state_topic);
    cJSON_AddStringToObject(root, "unique_id", unique_id);
    cJSON_AddStringToObject(root, "availability_topic", avail_topic);
    cJSON_AddStringToObject(root, "payload_available", "online");
    cJSON_AddStringToObject(root, "payload_not_available", "offline");

    if (device_class && strlen(device_class) > 0) {
        cJSON_AddStringToObject(root, "device_class", device_class);
    }

    cJSON_AddItemToObject(root, "device", build_device_info());

    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);

    if (json_str == NULL) {
        ESP_LOGE(TAG, "Failed to build JSON for binary_sensor %s", object_id);
        return ESP_FAIL;
    }

    esp_err_t err = app_mqtt_publish(topic, json_str, 1, 1);  // QoS 1, retain
    free(json_str);
    return err;
}

// Helper: publish state to topic
static esp_err_t publish_state(const char *object_id, const char *value)
{
    char topic[96];
    snprintf(topic, sizeof(topic), "%s/%s/state", node_id, object_id);
    return app_mqtt_publish(topic, value, 1, 1);  // QoS 1, retain
}

esp_err_t ha_discovery_publish_configs(void)
{
    init_node_id();
    ESP_LOGI(TAG, "Publishing HA discovery configs for device: %s", device_name);

    // WiFi SSID sensor
    publish_sensor_config("wifi_ssid", "WiFi 名称", NULL, NULL, NULL);

    // WiFi RSSI sensor
    publish_sensor_config("wifi_rssi", "WiFi 信号强度", "signal_strength", "dBm", "measurement");

    // WiFi IP sensor
    publish_sensor_config("wifi_ip", "IP 地址", NULL, NULL, NULL);

    // Heap free sensor
    publish_sensor_config("heap_free", "空闲内存", NULL, "B", "measurement");

    // Uptime sensor
    publish_sensor_config("uptime", "运行时间", "duration", "s", "total_increasing");

    // MQTT status binary sensor
    publish_binary_sensor_config("mqtt_status", "MQTT 状态", "connectivity");

    ESP_LOGI(TAG, "HA discovery configs published");
    return ESP_OK;
}

esp_err_t ha_discovery_publish_states(void)
{
    init_node_id();

    // WiFi SSID
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        publish_state("wifi_ssid", (char *)ap_info.ssid);

        // WiFi RSSI
        char rssi_str[16];
        snprintf(rssi_str, sizeof(rssi_str), "%d", ap_info.rssi);
        publish_state("wifi_rssi", rssi_str);
    } else {
        publish_state("wifi_ssid", "N/A");
        publish_state("wifi_rssi", "0");
    }

    // WiFi IP
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            char ip_str[16];
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
            publish_state("wifi_ip", ip_str);
        } else {
            publish_state("wifi_ip", "0.0.0.0");
        }
    } else {
        publish_state("wifi_ip", "0.0.0.0");
    }

    // Heap free
    char heap_str[16];
    snprintf(heap_str, sizeof(heap_str), "%lu", (unsigned long)esp_get_free_heap_size());
    publish_state("heap_free", heap_str);

    // Uptime (seconds since boot)
    char uptime_str[16];
    snprintf(uptime_str, sizeof(uptime_str), "%lld",
             (long long)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000));
    publish_state("uptime", uptime_str);

    // MQTT status
    publish_state("mqtt_status", app_mqtt_is_connected() ? "ON" : "OFF");

    return ESP_OK;
}

esp_err_t ha_discovery_publish_online(void)
{
    init_node_id();
    return app_mqtt_publish(avail_topic, "online", 1, 1);  // QoS 1, retain
}

esp_err_t ha_discovery_publish_offline(void)
{
    init_node_id();
    return app_mqtt_publish(avail_topic, "offline", 1, 1);  // QoS 1, retain
}

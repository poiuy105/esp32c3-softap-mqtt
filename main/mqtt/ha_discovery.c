#include <string.h>
#include <stdio.h>
#include "ha_discovery.h"
#include "app_mqtt.h"
#include "device_info.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "cJSON.h"
#include "gpio_control.h"
#include "pwm_driver.h"

static const char *TAG = "ha_discovery";

// Topic buffers
static char avail_topic[64] = {0};
static bool initialized = false;

// Initialize topics from device_info
static void init_topics(void)
{
    if (initialized) return;
    snprintf(avail_topic, sizeof(avail_topic), "%s/status", device_info_get_node_id());
    initialized = true;
}

// Build the common device info JSON object
static cJSON* build_device_info(void)
{
    cJSON *device = cJSON_CreateObject();
    cJSON_AddStringToObject(device, "name", device_info_get_device_name());
    cJSON_AddStringToObject(device, "manufacturer", "synaflow");
    cJSON_AddStringToObject(device, "model", "ESP32-C3");
    cJSON_AddStringToObject(device, "identifiers", device_info_get_node_id());

    cJSON *connections = cJSON_CreateArray();
    cJSON *conn = cJSON_CreateArray();
    cJSON_AddItemToArray(conn, cJSON_CreateString("mac"));
    cJSON_AddItemToArray(conn, cJSON_CreateString(device_info_get_mac_string()));
    cJSON_AddItemToArray(connections, conn);
    cJSON_AddItemToObject(device, "connections", connections);

    return device;
}

// Publish a sensor discovery config
static esp_err_t publish_sensor_config(const char *object_id, const char *name,
                                        const char *device_class,
                                        const char *unit, const char *state_class)
{
    const char *node_id = device_info_get_node_id();
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

    esp_err_t err = app_mqtt_publish(topic, json_str, 1, 1);
    free(json_str);
    return err;
}

// Publish a switch discovery config
static esp_err_t publish_switch_config(const char *object_id, const char *name,
                                        const char *cmd_topic_suffix)
{
    const char *node_id = device_info_get_node_id();
    char topic[128];
    snprintf(topic, sizeof(topic), "homeassistant/switch/%s/%s/config", node_id, object_id);

    char state_topic[96];
    snprintf(state_topic, sizeof(state_topic), "%s/%s/state", node_id, object_id);

    char cmd_topic[96];
    snprintf(cmd_topic, sizeof(cmd_topic), "%s/%s/set", node_id, cmd_topic_suffix);

    char unique_id[64];
    snprintf(unique_id, sizeof(unique_id), "%s_%s", node_id, object_id);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", name);
    cJSON_AddStringToObject(root, "state_topic", state_topic);
    cJSON_AddStringToObject(root, "command_topic", cmd_topic);
    cJSON_AddStringToObject(root, "unique_id", unique_id);
    cJSON_AddStringToObject(root, "availability_topic", avail_topic);
    cJSON_AddStringToObject(root, "payload_available", "online");
    cJSON_AddStringToObject(root, "payload_not_available", "offline");
    cJSON_AddStringToObject(root, "payload_on", "ON");
    cJSON_AddStringToObject(root, "payload_off", "OFF");
    cJSON_AddStringToObject(root, "state_on", "ON");
    cJSON_AddStringToObject(root, "state_off", "OFF");

    cJSON_AddItemToObject(root, "device", build_device_info());

    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);

    if (json_str == NULL) {
        ESP_LOGE(TAG, "Failed to build JSON for switch %s", object_id);
        return ESP_FAIL;
    }

    esp_err_t err = app_mqtt_publish(topic, json_str, 1, 1);
    free(json_str);
    return err;
}

// Publish a number discovery config
static esp_err_t publish_number_config(const char *object_id, const char *name,
                                        const char *cmd_topic_suffix,
                                        double min_val, double max_val, double step,
                                        const char *unit, const char *mode)
{
    const char *node_id = device_info_get_node_id();
    char topic[128];
    snprintf(topic, sizeof(topic), "homeassistant/number/%s/%s/config", node_id, object_id);

    char state_topic[96];
    snprintf(state_topic, sizeof(state_topic), "%s/%s/state", node_id, object_id);

    char cmd_topic[96];
    snprintf(cmd_topic, sizeof(cmd_topic), "%s/%s/set", node_id, cmd_topic_suffix);

    char unique_id[64];
    snprintf(unique_id, sizeof(unique_id), "%s_%s", node_id, object_id);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", name);
    cJSON_AddStringToObject(root, "state_topic", state_topic);
    cJSON_AddStringToObject(root, "command_topic", cmd_topic);
    cJSON_AddStringToObject(root, "unique_id", unique_id);
    cJSON_AddStringToObject(root, "availability_topic", avail_topic);
    cJSON_AddStringToObject(root, "payload_available", "online");
    cJSON_AddStringToObject(root, "payload_not_available", "offline");
    cJSON_AddNumberToObject(root, "min", min_val);
    cJSON_AddNumberToObject(root, "max", max_val);
    cJSON_AddNumberToObject(root, "step", step);
    if (unit && strlen(unit) > 0) {
        cJSON_AddStringToObject(root, "unit_of_measurement", unit);
    }
    if (mode && strlen(mode) > 0) {
        cJSON_AddStringToObject(root, "mode", mode);
    }

    cJSON_AddItemToObject(root, "device", build_device_info());

    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);

    if (json_str == NULL) {
        ESP_LOGE(TAG, "Failed to build JSON for number %s", object_id);
        return ESP_FAIL;
    }

    esp_err_t err = app_mqtt_publish(topic, json_str, 1, 1);
    free(json_str);
    return err;
}

// Publish a button discovery config
static esp_err_t publish_button_config(const char *object_id, const char *name,
                                        const char *cmd_topic_suffix,
                                        const char *payload_press)
{
    const char *node_id = device_info_get_node_id();
    char topic[128];
    snprintf(topic, sizeof(topic), "homeassistant/button/%s/%s/config", node_id, object_id);

    char cmd_topic[96];
    snprintf(cmd_topic, sizeof(cmd_topic), "%s/%s/set", node_id, cmd_topic_suffix);

    char unique_id[64];
    snprintf(unique_id, sizeof(unique_id), "%s_%s", node_id, object_id);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", name);
    cJSON_AddStringToObject(root, "command_topic", cmd_topic);
    cJSON_AddStringToObject(root, "unique_id", unique_id);
    cJSON_AddStringToObject(root, "availability_topic", avail_topic);
    cJSON_AddStringToObject(root, "payload_available", "online");
    cJSON_AddStringToObject(root, "payload_not_available", "offline");
    if (payload_press && strlen(payload_press) > 0) {
        cJSON_AddStringToObject(root, "payload_press", payload_press);
    }
    cJSON_AddStringToObject(root, "device_class", "restart");

    cJSON_AddItemToObject(root, "device", build_device_info());

    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);

    if (json_str == NULL) {
        ESP_LOGE(TAG, "Failed to build JSON for button %s", object_id);
        return ESP_FAIL;
    }

    esp_err_t err = app_mqtt_publish(topic, json_str, 1, 1);
    free(json_str);
    return err;
}

// Publish a binary_sensor discovery config
static esp_err_t publish_binary_sensor_config(const char *object_id, const char *name,
                                               const char *device_class)
{
    const char *node_id = device_info_get_node_id();
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

    esp_err_t err = app_mqtt_publish(topic, json_str, 1, 1);
    free(json_str);
    return err;
}

// Helper: publish state to topic
static esp_err_t publish_state(const char *object_id, const char *value)
{
    const char *node_id = device_info_get_node_id();
    char topic[96];
    snprintf(topic, sizeof(topic), "%s/%s/state", node_id, object_id);
    return app_mqtt_publish(topic, value, 1, 1);
}

esp_err_t ha_discovery_publish_configs(void)
{
    init_topics();
    ESP_LOGI(TAG, "Publishing HA discovery configs for device: %s", device_info_get_device_name());

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

    // LED switch
    publish_switch_config("led", "LED指示灯", "led");

    // Light control switch
    publish_switch_config("light_power", "照明控制", "light/power");

    // Light frequency number (0-150kHz, step=1Hz, mode=box)
    publish_number_config("light_freq", "照明频率", "light/freq", 0, 150000, 0.1, "Hz", "box");

    // Light duty number (0-100.0%, step=0.1, mode=box)
    publish_number_config("light_duty", "照明亮度", "light/duty", 0, 100, 0.1, "%", "box");

    // Sound control switch
    publish_switch_config("sound_power", "声波控制", "sound/power");

    // Sound frequency number (0-150kHz, step=1Hz, mode=box)
    publish_number_config("sound_freq", "声波频率", "sound/freq", 0, 150000, 1, "Hz", "box");

    // Sound volume number (50.0-100.0%, step=0.1, mode=box)
    publish_number_config("sound_vol", "声波音量", "sound/vol", 0, 100, 0.1, "%", "box");

    // Restart device button
    publish_button_config("restart", "重启设备", "restart", "RESTART");

    ESP_LOGI(TAG, "HA discovery configs published");
    return ESP_OK;
}

esp_err_t ha_discovery_publish_states(void)
{
    init_topics();
    const char *node_id = device_info_get_node_id();

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

    // LED state
    publish_state("led", gpio_get_led() ? "ON" : "OFF");

    // Light state
    publish_state("light_power", pwmGetLightEnable() ? "ON" : "OFF");
    char light_freq_str[16];
    snprintf(light_freq_str, sizeof(light_freq_str), "%lu", (unsigned long)pwmGetLightFreq());
    char light_duty_str[16];
    snprintf(light_duty_str, sizeof(light_duty_str), "%.1f", pwmGetLightDuty() / 1000.0);
    publish_state("light_freq", light_freq_str);
    publish_state("light_duty", light_duty_str);

    // Sound state
    publish_state("sound_power", pwmGetSoundEnable() ? "ON" : "OFF");
    char sound_freq_str[16];
    snprintf(sound_freq_str, sizeof(sound_freq_str), "%lu", (unsigned long)pwmGetSoundFreq());
    char sound_vol_str[16];
    snprintf(sound_vol_str, sizeof(sound_vol_str), "%.1f", pwmGetSoundDuty() / 1000.0);
    publish_state("sound_freq", sound_freq_str);
    publish_state("sound_vol", sound_vol_str);

    return ESP_OK;
}

esp_err_t ha_discovery_publish_online(void)
{
    init_topics();
    return app_mqtt_publish(avail_topic, "online", 1, 1);
}

esp_err_t ha_discovery_publish_offline(void)
{
    init_topics();
    return app_mqtt_publish(avail_topic, "offline", 1, 1);
}

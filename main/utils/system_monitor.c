#include "system_monitor.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_mqtt.h"

static const char *TAG = "SYS_MON";

static uint32_t boot_time = 0;

esp_err_t system_monitor_init(void)
{
    boot_time = (uint32_t)(esp_timer_get_time() / 1000000);
    ESP_LOGI(TAG, "System monitor initialized, boot time: %lu", boot_time);
    return ESP_OK;
}

esp_err_t system_monitor_get_stats(system_stats_t *stats)
{
    if (stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Heap info
    stats->free_heap = esp_get_free_heap_size();
    stats->min_free_heap = esp_get_minimum_free_heap_size();
    
    // WiFi RSSI
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        stats->wifi_rssi = ap_info.rssi;
    } else {
        stats->wifi_rssi = 0;
    }
    
    // Uptime
    uint32_t current_time = (uint32_t)(esp_timer_get_time() / 1000000);
    stats->uptime_seconds = current_time - boot_time;
    
    // MQTT stats
    mqtt_conn_stats_t mqtt_stats;
    app_mqtt_get_stats(&mqtt_stats);
    stats->mqtt_connect_count = mqtt_stats.connect_successes;
    stats->mqtt_disconnect_count = mqtt_stats.disconnect_count;
    
    return ESP_OK;
}

void system_monitor_log_stats(void)
{
    system_stats_t stats;
    system_monitor_get_stats(&stats);
    
    ESP_LOGI(TAG, "=== System Statistics ===");
    ESP_LOGI(TAG, "Free heap: %lu bytes (min: %lu)", 
             stats.free_heap, stats.min_free_heap);
    ESP_LOGI(TAG, "WiFi RSSI: %d dBm", stats.wifi_rssi);
    ESP_LOGI(TAG, "Uptime: %lu seconds", stats.uptime_seconds);
    ESP_LOGI(TAG, "MQTT: %lu connects, %lu disconnects",
             stats.mqtt_connect_count, stats.mqtt_disconnect_count);
    ESP_LOGI(TAG, "=========================");
}

uint32_t system_monitor_get_free_heap(void)
{
    return esp_get_free_heap_size();
}

uint32_t system_monitor_get_min_free_heap(void)
{
    return esp_get_minimum_free_heap_size();
}

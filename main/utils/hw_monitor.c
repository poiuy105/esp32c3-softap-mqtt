#include "hw_monitor.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "HW_MON";
static const char *NVS_NAMESPACE = "hw_monitor";
static const char *NVS_KEY_BOOT_COUNT = "boot_count";

static uint32_t boot_count = 0;

esp_err_t hw_monitor_init(void)
{
    ESP_LOGI(TAG, "Initializing hardware monitor");
    
    // Record boot
    esp_err_t ret = hw_monitor_record_boot();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to record boot: %s", esp_err_to_name(ret));
    }
    
    // Log initial status
    hw_monitor_log_status();
    
    return ESP_OK;
}

float hw_monitor_get_temperature(void)
{
    // ESP32-C3 has a built-in temperature sensor
    // Note: This is a placeholder - actual implementation depends on ESP-IDF version
    // For ESP-IDF 5.x, use adc_oneshot or dedicated temp sensor API
    
    // Return a dummy value for now (can be implemented with proper ADC reading)
    return 0.0f;
}

esp_err_t hw_monitor_get_status(hw_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    status->temperature_c = hw_monitor_get_temperature();
    status->temp_warning = status->temperature_c >= HW_MONITOR_TEMP_WARNING_C;
    status->temp_critical = status->temperature_c >= HW_MONITOR_TEMP_CRITICAL_C;
    status->boot_count = boot_count;
    status->last_reset_reason = esp_reset_reason();
    
    return ESP_OK;
}

void hw_monitor_log_status(void)
{
    hw_status_t status;
    hw_monitor_get_status(&status);
    
    ESP_LOGI(TAG, "=== Hardware Status ===");
    ESP_LOGI(TAG, "Boot count: %lu", status.boot_count);
    ESP_LOGI(TAG, "Reset reason: %lu", status.last_reset_reason);
    ESP_LOGI(TAG, "Temperature: %.1f C", status.temperature_c);
    if (status.temp_critical) {
        ESP_LOGE(TAG, "CRITICAL: High temperature!");
    } else if (status.temp_warning) {
        ESP_LOGW(TAG, "WARNING: Elevated temperature");
    }
    ESP_LOGI(TAG, "=======================");
}

bool hw_monitor_is_temp_safe(void)
{
    float temp = hw_monitor_get_temperature();
    return temp < HW_MONITOR_TEMP_WARNING_C;
}

esp_err_t hw_monitor_record_boot(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Read current count
    ret = nvs_get_u32(handle, NVS_KEY_BOOT_COUNT, &boot_count);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return ret;
    }
    
    // Increment and save
    boot_count++;
    ret = nvs_set_u32(handle, NVS_KEY_BOOT_COUNT, boot_count);
    if (ret != ESP_OK) {
        nvs_close(handle);
        return ret;
    }
    
    ret = nvs_commit(handle);
    nvs_close(handle);
    
    return ret;
}

uint32_t hw_monitor_get_boot_count(void)
{
    return boot_count;
}

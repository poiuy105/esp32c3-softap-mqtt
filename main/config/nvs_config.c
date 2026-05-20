#include <string.h>
#include "nvs_config.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "nvs_config";

esp_err_t nvs_init_config(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition was truncated and needs to be erased");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t nvs_save_wifi_config(const char *ssid, const char *password)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(handle, NVS_KEY_WIFI_SSID, ssid);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }
    
    err = nvs_set_str(handle, NVS_KEY_WIFI_PASSWORD, password);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    
    ESP_LOGI(TAG, "WiFi config saved successfully (SSID: %s)", ssid);
    return err;
}

esp_err_t nvs_read_wifi_config(char *ssid, size_t ssid_size, char *password, size_t pass_size)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;

    memset(ssid, 0, ssid_size);
    memset(password, 0, pass_size);
    
    err = nvs_get_str(handle, NVS_KEY_WIFI_SSID, ssid, &ssid_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return err;
    }
    
    err = nvs_get_str(handle, NVS_KEY_WIFI_PASSWORD, password, &pass_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return err;
    }
    
    nvs_close(handle);
    
    ESP_LOGI(TAG, "WiFi config read: SSID=%s", ssid);
    return ESP_OK;
}

esp_err_t nvs_save_mqtt_config(const char *uri, uint16_t port, const char *user, const char *pass)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(handle, NVS_KEY_MQTT_URI, uri);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }
    
    err = nvs_set_u16(handle, NVS_KEY_MQTT_PORT, port);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }
    
    err = nvs_set_str(handle, NVS_KEY_MQTT_USER, user);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }
    
    err = nvs_set_str(handle, NVS_KEY_MQTT_PASS, pass);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    
    ESP_LOGI(TAG, "MQTT config saved successfully");
    return err;
}

esp_err_t nvs_read_mqtt_config(char *uri, size_t uri_size, uint16_t *port, char *user, size_t user_size, char *pass, size_t pass_size)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;

    memset(uri, 0, uri_size);
    memset(user, 0, user_size);
    memset(pass, 0, pass_size);
    
    err = nvs_get_str(handle, NVS_KEY_MQTT_URI, uri, &uri_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return err;
    }
    
    uint16_t temp_port = DEFAULT_MQTT_PORT;
    err = nvs_get_u16(handle, NVS_KEY_MQTT_PORT, &temp_port);
    if (err == ESP_OK) *port = temp_port;
    
    err = nvs_get_str(handle, NVS_KEY_MQTT_USER, user, &user_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return err;
    }
    
    err = nvs_get_str(handle, NVS_KEY_MQTT_PASS, pass, &pass_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return err;
    }
    
    nvs_close(handle);
    return ESP_OK;
}

esp_err_t nvs_reset_config(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    
    err = nvs_erase_all(handle);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    
    ESP_LOGI(TAG, "Config reset successfully");
    return err;
}

esp_err_t nvs_set_first_boot(bool first_boot)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    
    err = nvs_set_u8(handle, NVS_KEY_FIRST_BOOT, first_boot ? 1 : 0);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    
    ESP_LOGI(TAG, "First boot flag set: %d", first_boot);
    return err;
}

bool nvs_get_first_boot(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return true;
    
    uint8_t first_boot = 1;
    err = nvs_get_u8(handle, NVS_KEY_FIRST_BOOT, &first_boot);
    
    nvs_close(handle);
    
    return (first_boot != 0);
}

esp_err_t nvs_load_all_config(app_config_t *config)
{
    memset(config, 0, sizeof(app_config_t));
    
    esp_err_t err = nvs_read_wifi_config(config->wifi_ssid, sizeof(config->wifi_ssid), 
                                         config->wifi_password, sizeof(config->wifi_password));
    if (err != ESP_OK) {
        strncpy(config->wifi_ssid, DEFAULT_WIFI_SSID, sizeof(config->wifi_ssid) - 1);
        strncpy(config->wifi_password, DEFAULT_WIFI_PASSWORD, sizeof(config->wifi_password) - 1);
    }
    
    err = nvs_read_mqtt_config(config->mqtt_uri, sizeof(config->mqtt_uri), 
                              &config->mqtt_port, 
                              config->mqtt_username, sizeof(config->mqtt_username), 
                              config->mqtt_password, sizeof(config->mqtt_password));
    if (err != ESP_OK) {
        strncpy(config->mqtt_uri, DEFAULT_MQTT_URI, sizeof(config->mqtt_uri) - 1);
        config->mqtt_port = DEFAULT_MQTT_PORT;
    }
    
    config->first_boot = nvs_get_first_boot();
    config->is_configured = nvs_is_config_valid(config);
    
    return ESP_OK;
}

bool nvs_is_config_valid(app_config_t *config)
{
    if (strlen(config->wifi_ssid) == 0) return false;
    if (strlen(config->mqtt_uri) == 0) return false;
    if (config->mqtt_port == 0) return false;
    
    return true;
}

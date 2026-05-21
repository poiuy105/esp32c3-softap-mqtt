#ifndef NVS_CONFIG_H
#define NVS_CONFIG_H

#include "esp_err.h"
#include "default_config.h"

#define NVS_NAMESPACE "app_config"
#define NVS_KEY_WIFI_SSID "wifi_ssid"
#define NVS_KEY_WIFI_PASSWORD "wifi_pass"
#define NVS_KEY_MQTT_URI "mqtt_uri"
#define NVS_KEY_MQTT_PORT "mqtt_port"
#define NVS_KEY_MQTT_USER "mqtt_user"
#define NVS_KEY_MQTT_PASS "mqtt_pass"
#define NVS_KEY_FIRST_BOOT "first_boot"

// Device control keys
#define NVS_KEY_LED_STATE      "led_state"
#define NVS_KEY_LIGHT_ENABLE   "light_en"
#define NVS_KEY_LIGHT_FREQ     "light_freq"
#define NVS_KEY_LIGHT_DUTY     "light_duty"
#define NVS_KEY_SOUND_ENABLE   "sound_en"
#define NVS_KEY_SOUND_FREQ     "sound_freq"
#define NVS_KEY_SOUND_DUTY     "sound_duty"

typedef struct {
    bool is_configured;
    char wifi_ssid[32];
    char wifi_password[64];
    char mqtt_uri[128];
    uint16_t mqtt_port;
    char mqtt_username[64];
    char mqtt_password[64];
    bool first_boot;
} app_config_t;

// Device control state
typedef struct {
    bool led_state;
    bool light_enabled;
    uint32_t light_freq;
    uint8_t light_duty;
    bool sound_enabled;
    uint32_t sound_freq;
    uint8_t sound_duty;
} device_state_t;

esp_err_t nvs_init_config(void);
esp_err_t nvs_save_wifi_config(const char *ssid, const char *password);
esp_err_t nvs_read_wifi_config(char *ssid, size_t ssid_size, char *password, size_t pass_size);
esp_err_t nvs_save_mqtt_config(const char *uri, uint16_t port, const char *user, const char *pass);
esp_err_t nvs_read_mqtt_config(char *uri, size_t uri_size, uint16_t *port, char *user, size_t user_size, char *pass, size_t pass_size);
esp_err_t nvs_reset_config(void);
esp_err_t nvs_set_first_boot(bool first_boot);
bool nvs_get_first_boot(void);
esp_err_t nvs_load_all_config(app_config_t *config);
bool nvs_is_config_valid(app_config_t *config);

// Device state storage
esp_err_t nvs_save_device_state(const device_state_t *state);
esp_err_t nvs_load_device_state(device_state_t *state);

#endif

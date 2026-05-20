#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char ssid[32];
    char password[64];
    uint8_t channel;
    bool sta_connected;
    int8_t rssi;
} wifi_manager_config_t;

esp_err_t wifi_manager_init(void);
esp_err_t wifi_manager_connect_sta(const char *ssid, const char *password);
esp_err_t wifi_manager_stop_sta(void);
bool wifi_manager_is_sta_connected(void);
int8_t wifi_manager_get_rssi(void);
esp_err_t wifi_manager_start_softap(const char *ssid, const char *password);
esp_err_t wifi_manager_stop_softap(void);

#endif

#ifndef SOFTAP_H
#define SOFTAP_H

#include "esp_err.h"

#define DEFAULT_SOFTAP_SSID "ESP32-SoftAP"
#define DEFAULT_SOFTAP_PASSWORD "12345678"
#define DEFAULT_SOFTAP_CHANNEL 1

esp_err_t softap_start(const char *ssid, const char *password);
esp_err_t softap_stop(void);

#endif

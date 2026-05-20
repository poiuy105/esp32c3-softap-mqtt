#ifndef SOFTAP_H
#define SOFTAP_H

#include "esp_err.h"
#include "esp_netif.h"

#define DEFAULT_SOFTAP_SSID "ESP32-SoftAP"
#define DEFAULT_SOFTAP_PASSWORD ""  // Empty password for open network
#define DEFAULT_SOFTAP_CHANNEL 1

// Function to generate SSID with MAC suffix
esp_err_t softap_generate_ssid_with_mac(char *ssid_out, size_t ssid_size);

#endif

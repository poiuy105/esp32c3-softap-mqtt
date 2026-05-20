#ifndef SOFTAP_H
#define SOFTAP_H

#include "esp_err.h"
#include "esp_netif.h"

#define DEFAULT_SOFTAP_SSID "ESP32-SoftAP"
#define DEFAULT_SOFTAP_PASSWORD ""  // Empty password for open network
#define DEFAULT_SOFTAP_CHANNEL 1

esp_err_t softap_start(const char *ssid, const char *password);
esp_err_t softap_stop(void);
esp_err_t softap_generate_ssid_with_mac(char *ssid_out, size_t ssid_size);

// Functions with netif handle management for proper lifecycle
esp_err_t softap_start_with_netif(const char *ssid, const char *password, 
                                  esp_netif_t **sta_netif_out, esp_netif_t **ap_netif_out);
esp_err_t softap_stop_with_netif(esp_netif_t **sta_netif, esp_netif_t **ap_netif);

#endif

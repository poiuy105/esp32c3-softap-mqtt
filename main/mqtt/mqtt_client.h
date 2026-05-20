#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include "esp_err.h"

typedef struct {
    char broker_uri[128];
    uint16_t port;
    char username[64];
    char password[64];
    char client_id[32];
    bool connected;
} mqtt_config_t;

esp_err_t mqtt_client_init(void);
esp_err_t mqtt_client_connect(const char *broker_uri, uint16_t port, 
                                const char *username, const char *password);
esp_err_t mqtt_client_disconnect(void);
esp_err_t mqtt_client_subscribe(const char *topic, int qos);
esp_err_t mqtt_client_publish(const char *topic, const char *payload, int qos, int retain);
bool mqtt_client_is_connected(void);

#endif

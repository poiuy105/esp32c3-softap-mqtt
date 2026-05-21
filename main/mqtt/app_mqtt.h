#ifndef APP_MQTT_H
#define APP_MQTT_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Connection statistics structure
typedef struct {
    uint32_t connect_attempts;
    uint32_t connect_successes;
    uint32_t disconnect_count;
    uint32_t last_reconnect_delay_ms;
} mqtt_conn_stats_t;

esp_err_t app_mqtt_connect(const char *broker_uri, uint16_t port, 
                                const char *username, const char *password);
esp_err_t app_mqtt_disconnect(void);
esp_err_t app_mqtt_subscribe(const char *topic, int qos);
esp_err_t app_mqtt_publish(const char *topic, const char *payload, int qos, int retain);
bool app_mqtt_is_connected(void);

// Get connection statistics
void app_mqtt_get_stats(mqtt_conn_stats_t *stats);

#endif

#ifndef HA_DISCOVERY_H
#define HA_DISCOVERY_H

#include "esp_err.h"

// Publish all HA discovery config messages (retain=true)
esp_err_t ha_discovery_publish_configs(void);

// Publish all entity state values
esp_err_t ha_discovery_publish_states(void);

// Publish online status to availability topic
esp_err_t ha_discovery_publish_online(void);

// Publish offline status to availability topic
esp_err_t ha_discovery_publish_offline(void);

#endif

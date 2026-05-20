#ifndef EVENT_HANDLERS_H
#define EVENT_HANDLERS_H

#include "state_machine.h"
#include "esp_event.h"
#include "freertos/event_groups.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// Set the event group handle for WiFi connection synchronization
void event_handlers_set_wifi_event_group(EventGroupHandle_t event_group);

esp_err_t event_handlers_init(void);
void event_handlers_register_wifi_handler(void);
void event_handlers_register_mqtt_handler(void);

#endif

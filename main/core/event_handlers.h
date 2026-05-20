#ifndef EVENT_HANDLERS_H
#define EVENT_HANDLERS_H

#include "state_machine.h"
#include "esp_event.h"

esp_err_t event_handlers_init(void);
void event_handlers_register_wifi_handler(void);
void event_handlers_register_mqtt_handler(void);

#endif

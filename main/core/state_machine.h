#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include "esp_err.h"

typedef enum {
    STATE_INIT = 0,
    STATE_SOFTAP,
    STATE_CONFIG,
    STATE_CONNECTING,
    STATE_RUNNING,
    STATE_ERROR
} app_state_t;

typedef enum {
    EVENT_INIT_COMPLETE = 0,
    EVENT_CONFIG_RECEIVED,
    EVENT_WIFI_CONNECTED,
    EVENT_WIFI_DISCONNECTED,
    EVENT_MQTT_CONNECTED,
    EVENT_MQTT_DISCONNECTED,
    EVENT_RESET_CONFIG,
    EVENT_TIMEOUT
} app_event_t;

typedef void (*state_enter_callback_t)(app_state_t prev_state);
typedef void (*state_exit_callback_t)(app_state_t next_state);
typedef void (*event_handler_t)(app_event_t event);

void state_machine_init(void);
app_state_t state_machine_get_current_state(void);
const char *state_machine_get_state_name(app_state_t state);
const char *state_machine_get_event_name(app_event_t event);
esp_err_t state_machine_trigger_event(app_event_t event);
bool state_machine_is_running(void);

#endif

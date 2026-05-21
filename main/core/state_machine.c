#include <string.h>
#include "state_machine.h"
#include "esp_log.h"

static const char *TAG = "state_machine";
static app_state_t current_state = STATE_INIT;
static bool initialized = false;

static const char *state_names[] = {
    "INIT",
    "SOFTAP",
    "CONFIG",
    "CONNECTING",
    "RUNNING",
    "ERROR"
};

static const char *event_names[] = {
    "INIT_COMPLETE",
    "CONFIG_RECEIVED",
    "WIFI_CONNECTED",
    "WIFI_DISCONNECTED",
    "MQTT_CONNECTED",
    "MQTT_DISCONNECTED",
    "RESET_CONFIG",
    "TIMEOUT"
};

static void state_machine_transition(app_state_t new_state)
{
    if (new_state == current_state) {
        return;
    }
    
    ESP_LOGI(TAG, "State transition: %s -> %s", 
             state_machine_get_state_name(current_state),
             state_machine_get_state_name(new_state));
    
    current_state = new_state;
}

static void handle_state_init(app_event_t event)
{
    switch (event) {
        case EVENT_INIT_COMPLETE:
            state_machine_transition(STATE_SOFTAP);
            break;
        case EVENT_CONFIG_RECEIVED:
            // Already configured, skip SOFTAP and go directly to CONFIG
            state_machine_transition(STATE_CONFIG);
            break;
        default:
            ESP_LOGW(TAG, "Unsupported event %s in INIT state", 
                     state_machine_get_event_name(event));
            break;
    }
}

static void handle_state_softap(app_event_t event)
{
    switch (event) {
        case EVENT_CONFIG_RECEIVED:
            state_machine_transition(STATE_CONFIG);
            break;
        case EVENT_RESET_CONFIG:
            state_machine_transition(STATE_INIT);
            break;
        default:
            ESP_LOGW(TAG, "Unsupported event %s in SOFTAP state", 
                     state_machine_get_event_name(event));
            break;
    }
}

static void handle_state_config(app_event_t event)
{
    switch (event) {
        case EVENT_WIFI_CONNECTED:
            state_machine_transition(STATE_CONNECTING);
            break;
        case EVENT_RESET_CONFIG:
            state_machine_transition(STATE_INIT);
            break;
        case EVENT_TIMEOUT:
        case EVENT_WIFI_DISCONNECTED:
            state_machine_transition(STATE_SOFTAP);
            break;
        default:
            ESP_LOGW(TAG, "Unsupported event %s in CONFIG state", 
                     state_machine_get_event_name(event));
            break;
    }
}

static void handle_state_connecting(app_event_t event)
{
    switch (event) {
        case EVENT_MQTT_CONNECTED:
            state_machine_transition(STATE_RUNNING);
            break;
        case EVENT_WIFI_DISCONNECTED:
            // WiFi lost during MQTT connect - go back to reconnect WiFi
            state_machine_transition(STATE_CONFIG);
            break;
        case EVENT_TIMEOUT:
            // MQTT connection timed out - retry
            ESP_LOGW(TAG, "MQTT connection timeout, retrying...");
            // Stay in CONNECTING state, app_task will retry
            break;
        case EVENT_RESET_CONFIG:
            state_machine_transition(STATE_INIT);
            break;
        default:
            ESP_LOGW(TAG, "Unsupported event %s in CONNECTING state", 
                     state_machine_get_event_name(event));
            break;
    }
}

static void handle_state_running(app_event_t event)
{
    switch (event) {
        case EVENT_WIFI_DISCONNECTED:
            // WiFi lost - go back to reconnect WiFi first
            state_machine_transition(STATE_CONFIG);
            break;
        case EVENT_MQTT_DISCONNECTED:
            // MQTT disconnected but WiFi still up - ESP-MQTT handles auto-reconnect
            // Don't change state, let the library reconnect
            ESP_LOGI(TAG, "MQTT disconnected, auto-reconnect in progress...");
            break;
        case EVENT_RESET_CONFIG:
            state_machine_transition(STATE_INIT);
            break;
        default:
            ESP_LOGW(TAG, "Unsupported event %s in RUNNING state", 
                     state_machine_get_event_name(event));
            break;
    }
}

static void handle_state_error(app_event_t event)
{
    switch (event) {
        case EVENT_RESET_CONFIG:
            state_machine_transition(STATE_INIT);
            break;
        case EVENT_WIFI_CONNECTED:
            state_machine_transition(STATE_CONNECTING);
            break;
        case EVENT_TIMEOUT:
            // Retry after timeout in error state
            state_machine_transition(STATE_CONFIG);
            break;
        default:
            ESP_LOGW(TAG, "Unsupported event %s in ERROR state", 
                     state_machine_get_event_name(event));
            break;
    }
}

void state_machine_init(void)
{
    if (initialized) {
        ESP_LOGW(TAG, "State machine already initialized");
        return;
    }
    
    current_state = STATE_INIT;
    initialized = true;
    ESP_LOGI(TAG, "State machine initialized");
}

app_state_t state_machine_get_current_state(void)
{
    return current_state;
}

const char *state_machine_get_state_name(app_state_t state)
{
    if (state < 0 || state >= sizeof(state_names) / sizeof(state_names[0])) {
        return "UNKNOWN";
    }
    return state_names[state];
}

const char *state_machine_get_event_name(app_event_t event)
{
    if (event < 0 || event >= sizeof(event_names) / sizeof(event_names[0])) {
        return "UNKNOWN";
    }
    return event_names[event];
}

esp_err_t state_machine_trigger_event(app_event_t event)
{
    if (!initialized) {
        ESP_LOGE(TAG, "State machine not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Triggering event: %s in state: %s", 
             state_machine_get_event_name(event),
             state_machine_get_state_name(current_state));
    
    switch (current_state) {
        case STATE_INIT:
            handle_state_init(event);
            break;
        case STATE_SOFTAP:
            handle_state_softap(event);
            break;
        case STATE_CONFIG:
            handle_state_config(event);
            break;
        case STATE_CONNECTING:
            handle_state_connecting(event);
            break;
        case STATE_RUNNING:
            handle_state_running(event);
            break;
        case STATE_ERROR:
            handle_state_error(event);
            break;
        default:
            ESP_LOGE(TAG, "Unknown state: %d", current_state);
            return ESP_ERR_INVALID_STATE;
    }
    
    return ESP_OK;
}

bool state_machine_is_running(void)
{
    return initialized && current_state == STATE_RUNNING;
}

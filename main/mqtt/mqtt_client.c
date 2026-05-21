#include <string.h>
#include "app_mqtt.h"
#include "ha_discovery.h"
#include "state_machine.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "mqtt_client.h"
#include "gpio_control.h"
#include "rmt_driver.h"
#include "nvs_config.h"

static const char *TAG = "mqtt_client";

static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool is_connected = false;

// Buffer for formatted URI
static char formatted_uri[256];

// LWT topic buffer
static char lwt_topic[64] = {0};

// Node ID for topic prefix
static char node_id[32] = {0};

// Helper to save and publish state
static void save_and_publish_state(void)
{
    device_state_t state = {
        .led_state = gpio_get_led(),
        .light_enabled = rmt_get_light_enabled(),
        .light_freq = rmt_get_light_freq(),
        .light_duty = rmt_get_light_duty(),
        .sound_enabled = rmt_get_sound_enabled(),
        .sound_freq = rmt_get_sound_freq(),
        .sound_duty = rmt_get_sound_duty(),
    };
    nvs_save_device_state(&state);
    ha_discovery_publish_states();
}

// Handle incoming MQTT commands
static void handle_mqtt_command(const char *topic, int topic_len, const char *data, int data_len)
{
    // Make null-terminated strings
    char topic_buf[128] = {0};
    char data_buf[64] = {0};
    strncpy(topic_buf, topic, topic_len < 127 ? topic_len : 127);
    strncpy(data_buf, data, data_len < 63 ? data_len : 63);
    
    ESP_LOGI(TAG, "Command: topic=%s, data=%s", topic_buf, data_buf);
    
    // Check which command topic
    char expected_topic[64];
    
    // LED control
    snprintf(expected_topic, sizeof(expected_topic), "%s/led/set", node_id);
    if (strcmp(topic_buf, expected_topic) == 0) {
        bool on = (strcmp(data_buf, "ON") == 0);
        gpio_set_led(on);
        save_and_publish_state();
        return;
    }
    
    // Light power
    snprintf(expected_topic, sizeof(expected_topic), "%s/light/power/set", node_id);
    if (strcmp(topic_buf, expected_topic) == 0) {
        bool on = (strcmp(data_buf, "ON") == 0);
        rmt_set_light(on, rmt_get_light_freq(), rmt_get_light_duty());
        save_and_publish_state();
        return;
    }
    
    // Light frequency
    snprintf(expected_topic, sizeof(expected_topic), "%s/light/freq/set", node_id);
    if (strcmp(topic_buf, expected_topic) == 0) {
        uint32_t freq = (uint32_t)atoi(data_buf);
        rmt_set_light(rmt_get_light_enabled(), freq, rmt_get_light_duty());
        save_and_publish_state();
        return;
    }
    
    // Light duty
    snprintf(expected_topic, sizeof(expected_topic), "%s/light/duty/set", node_id);
    if (strcmp(topic_buf, expected_topic) == 0) {
        uint8_t duty = (uint8_t)atoi(data_buf);
        rmt_set_light(rmt_get_light_enabled(), rmt_get_light_freq(), duty);
        save_and_publish_state();
        return;
    }
    
    // Sound power
    snprintf(expected_topic, sizeof(expected_topic), "%s/sound/power/set", node_id);
    if (strcmp(topic_buf, expected_topic) == 0) {
        bool on = (strcmp(data_buf, "ON") == 0);
        rmt_set_sound(on, rmt_get_sound_freq(), rmt_get_sound_duty());
        save_and_publish_state();
        return;
    }
    
    // Sound frequency
    snprintf(expected_topic, sizeof(expected_topic), "%s/sound/freq/set", node_id);
    if (strcmp(topic_buf, expected_topic) == 0) {
        uint32_t freq = (uint32_t)atoi(data_buf);
        rmt_set_sound(rmt_get_sound_enabled(), freq, rmt_get_sound_duty());
        save_and_publish_state();
        return;
    }
    
    // Sound volume
    snprintf(expected_topic, sizeof(expected_topic), "%s/sound/vol/set", node_id);
    if (strcmp(topic_buf, expected_topic) == 0) {
        uint8_t vol = (uint8_t)atoi(data_buf);
        rmt_set_sound(rmt_get_sound_enabled(), rmt_get_sound_freq(), vol);
        save_and_publish_state();
        return;
    }
    
    ESP_LOGW(TAG, "Unknown command topic: %s", topic_buf);
}

// Helper function to format MQTT URI
static const char* format_mqtt_uri(const char *uri, uint16_t port)
{
    if (strncmp(uri, "mqtt://", 7) == 0 || 
        strncmp(uri, "mqtts://", 8) == 0 ||
        strncmp(uri, "ws://", 5) == 0 ||
        strncmp(uri, "wss://", 6) == 0) {
        return uri;
    }
    
    if (strstr(uri, "://") != NULL) {
        return uri;
    }
    
    if (port != 0 && port != 1883) {
        snprintf(formatted_uri, sizeof(formatted_uri), "mqtt://%s:%d", uri, port);
    } else {
        snprintf(formatted_uri, sizeof(formatted_uri), "mqtt://%s", uri);
    }
    
    ESP_LOGI(TAG, "Formatted MQTT URI: %s", formatted_uri);
    return formatted_uri;
}

// Build LWT topic from MAC address
static void init_lwt_topic(void)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(lwt_topic, sizeof(lwt_topic), "esp32c3_%02x%02x%02x%02x/status",
             mac[2], mac[3], mac[4], mac[5]);
    snprintf(node_id, sizeof(node_id), "esp32c3_%02x%02x%02x%02x",
             mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "LWT topic: %s, Node ID: %s", lwt_topic, node_id);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            is_connected = true;
            
            // Subscribe to command topics
            char cmd_topic[64];
            snprintf(cmd_topic, sizeof(cmd_topic), "%s/led/set", node_id);
            esp_mqtt_client_subscribe(mqtt_client, cmd_topic, 1);
            snprintf(cmd_topic, sizeof(cmd_topic), "%s/light/power/set", node_id);
            esp_mqtt_client_subscribe(mqtt_client, cmd_topic, 1);
            snprintf(cmd_topic, sizeof(cmd_topic), "%s/light/freq/set", node_id);
            esp_mqtt_client_subscribe(mqtt_client, cmd_topic, 1);
            snprintf(cmd_topic, sizeof(cmd_topic), "%s/light/duty/set", node_id);
            esp_mqtt_client_subscribe(mqtt_client, cmd_topic, 1);
            snprintf(cmd_topic, sizeof(cmd_topic), "%s/sound/power/set", node_id);
            esp_mqtt_client_subscribe(mqtt_client, cmd_topic, 1);
            snprintf(cmd_topic, sizeof(cmd_topic), "%s/sound/freq/set", node_id);
            esp_mqtt_client_subscribe(mqtt_client, cmd_topic, 1);
            snprintf(cmd_topic, sizeof(cmd_topic), "%s/sound/vol/set", node_id);
            esp_mqtt_client_subscribe(mqtt_client, cmd_topic, 1);
            
            // Publish online status
            ha_discovery_publish_online();
            
            // Publish HA discovery configs (retain)
            ha_discovery_publish_configs();
            
            // Publish first state data
            ha_discovery_publish_states();
            
            // Trigger state machine event
            state_machine_trigger_event(EVENT_MQTT_CONNECTED);
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT disconnected");
            is_connected = false;
            
            // Trigger state machine event
            state_machine_trigger_event(EVENT_MQTT_DISCONNECTED);
            break;
            
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT subscribed, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT unsubscribed, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT published, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT data received on topic: %.*s", 
                     event->topic_len, event->topic);
            // Handle command
            handle_mqtt_command(event->topic, event->topic_len, 
                               event->data, event->data_len);
            break;
            
        default:
            break;
    }
}

esp_err_t app_mqtt_connect(const char *broker_uri, uint16_t port, 
                                const char *username, const char *password)
{
    ESP_LOGI(TAG, "Connecting to MQTT broker: %s (port: %d)", broker_uri, port);
    
    // Initialize LWT topic
    init_lwt_topic();
    
    // Format URI into static buffer for MQTT config
    const char *fmt_uri = format_mqtt_uri(broker_uri, port);
    ESP_LOGI(TAG, "Using MQTT URI: %s", fmt_uri);
    
    // Parse hostname, port and transport from URI for v5.1 compatibility
    char hostname[128] = {0};
    int mqtt_port = 1883;
    esp_mqtt_transport_t transport = MQTT_TRANSPORT_OVER_TCP;
    const char *uri_start = fmt_uri;
    
    // Skip protocol prefix and determine transport
    if (strncmp(fmt_uri, "mqtt://", 7) == 0) {
        uri_start = fmt_uri + 7;
        transport = MQTT_TRANSPORT_OVER_TCP;
    } else if (strncmp(fmt_uri, "mqtts://", 8) == 0) {
        uri_start = fmt_uri + 8;
        mqtt_port = 8883;
        transport = MQTT_TRANSPORT_OVER_SSL;
    } else if (strncmp(fmt_uri, "ws://", 5) == 0) {
        uri_start = fmt_uri + 5;
        transport = MQTT_TRANSPORT_OVER_WS;
    } else if (strncmp(fmt_uri, "wss://", 6) == 0) {
        uri_start = fmt_uri + 6;
        mqtt_port = 443;
        transport = MQTT_TRANSPORT_OVER_WSS;
    }
    
    // Extract hostname (before ':' or '/')
    strncpy(hostname, uri_start, sizeof(hostname) - 1);
    char *colon = strchr(hostname, ':');
    if (colon) {
        *colon = '\0';
        mqtt_port = atoi(colon + 1);
    }
    char *slash = strchr(hostname, '/');
    if (slash) *slash = '\0';
    
    // Copy credentials to non-const buffers for v5.1 _Generic compatibility
    char user_buf[64] = {0};
    char pass_buf[64] = {0};
    if (username) strncpy(user_buf, username, sizeof(user_buf) - 1);
    if (password) strncpy(pass_buf, password, sizeof(pass_buf) - 1);
    
    ESP_LOGI(TAG, "MQTT hostname: %s, port: %d", hostname, mqtt_port);
    
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address.hostname = hostname,
            .address.port = mqtt_port,
            .address.transport = transport,
        },
        .credentials = {
            .username = user_buf,
            .authentication.password = pass_buf,
        },
        .session = {
            .last_will = {
                .topic = lwt_topic,
                .msg = "offline",
                .qos = 1,
                .retain = true,
            }
        },
    };
    
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }
    
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    
    return esp_mqtt_client_start(mqtt_client);
}

esp_err_t app_mqtt_disconnect(void)
{
    if (mqtt_client) {
        esp_mqtt_client_stop(mqtt_client);
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
        is_connected = false;
    }
    return ESP_OK;
}

esp_err_t app_mqtt_subscribe(const char *topic, int qos)
{
    if (mqtt_client && is_connected) {
        char topic_buf[128] = {0};
        strncpy(topic_buf, topic, sizeof(topic_buf) - 1);
        int msg_id = esp_mqtt_client_subscribe(mqtt_client, topic_buf, qos);
        ESP_LOGI(TAG, "Subscribed to topic '%s', msg_id=%d", topic, msg_id);
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t app_mqtt_publish(const char *topic, const char *payload, int qos, int retain)
{
    if (mqtt_client && is_connected) {
        char topic_buf[128] = {0};
        strncpy(topic_buf, topic, sizeof(topic_buf) - 1);
        int msg_id = esp_mqtt_client_publish(mqtt_client, topic_buf, payload, 0, qos, retain);
        ESP_LOGI(TAG, "Published to topic '%s', msg_id=%d", topic, msg_id);
        return ESP_OK;
    }
    return ESP_FAIL;
}

bool app_mqtt_is_connected(void)
{
    return is_connected;
}

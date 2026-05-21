#include <string.h>
#include "app_mqtt.h"
#include "ha_discovery.h"
#include "state_machine.h"
#include "esp_log.h"
#include "esp_event.h"
#include "mqtt_client.h"

static const char *TAG = "mqtt_client";

static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool is_connected = false;

// Buffer for formatted URI
static char formatted_uri[256];

// LWT topic buffer
static char lwt_topic[64] = {0};

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
    ESP_LOGI(TAG, "LWT topic: %s", lwt_topic);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            is_connected = true;
            
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
            break;
            
        default:
            break;
    }
}

esp_err_t app_mqtt_connect(const char *broker_uri, uint16_t port, 
                                const char *username, const char *password)
{
    ESP_LOGI(TAG, "Connecting to MQTT broker: %s (port: %d)", broker_uri, port);
    
    const char *formatted_broker_uri = format_mqtt_uri(broker_uri, port);
    ESP_LOGI(TAG, "Using MQTT URI: %s", formatted_broker_uri);
    
    // Initialize LWT topic
    init_lwt_topic();
    
    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = formatted_broker_uri,
        .username = username,
        .password = password,
        .lwt_topic = lwt_topic,
        .lwt_msg = "offline",
        .lwt_qos = 1,
        .lwt_retain = true,
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
        int msg_id = esp_mqtt_client_subscribe(mqtt_client, topic, qos);
        ESP_LOGI(TAG, "Subscribed to topic '%s', msg_id=%d", topic, msg_id);
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t app_mqtt_publish(const char *topic, const char *payload, int qos, int retain)
{
    if (mqtt_client && is_connected) {
        int msg_id = esp_mqtt_client_publish(mqtt_client, topic, payload, 0, qos, retain);
        ESP_LOGI(TAG, "Published to topic '%s', msg_id=%d", topic, msg_id);
        return ESP_OK;
    }
    return ESP_FAIL;
}

bool app_mqtt_is_connected(void)
{
    return is_connected;
}

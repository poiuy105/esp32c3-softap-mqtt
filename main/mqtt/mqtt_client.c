#include <string.h>
#include "mqtt_client.h"
#include "esp_log.h"
#include "esp_event.h"
#include "mqtt_client.h"

static const char *TAG = "mqtt_client";

static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool is_connected = false;
static uint32_t reconnect_attempts = 0;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            is_connected = true;
            reconnect_attempts = 0;
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT disconnected");
            is_connected = false;
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
            ESP_LOGI(TAG, "MQTT data received: topic=%.*s, data=%.*s", 
                    event->topic_len, event->topic, 
                    event->data_len, event->data);
            break;
            
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error");
            break;
            
        default:
            break;
    }
}

esp_err_t mqtt_client_connect(const char *broker_uri, uint16_t port, 
                                const char *username, const char *password)
{
    char full_uri[256];
    
    // Check if broker_uri already contains protocol prefix
    if (strncmp(broker_uri, "mqtt://", 7) == 0 || strncmp(broker_uri, "mqtts://", 8) == 0) {
        strncpy(full_uri, broker_uri, sizeof(full_uri) - 1);
        full_uri[sizeof(full_uri) - 1] = '\0';
    } else {
        snprintf(full_uri, sizeof(full_uri), "mqtt://%s:%d", broker_uri, port);
    }
    
    ESP_LOGI(TAG, "Connecting to MQTT broker: %s", full_uri);
    
    // ESP-IDF v5.1 MQTT config structure
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = full_uri,
    };
    
    if (username != NULL) {
        mqtt_cfg.credentials.username = username;
    }
    
    if (password != NULL) {
        mqtt_cfg.credentials.authentication.password = password;
    }
    
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }
    
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    
    return esp_mqtt_client_start(mqtt_client);
}

esp_err_t mqtt_client_disconnect(void)
{
    if (mqtt_client) {
        esp_mqtt_client_stop(mqtt_client);
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
        is_connected = false;
    }
    return ESP_OK;
}

esp_err_t mqtt_client_subscribe(const char *topic, int qos)
{
    if (mqtt_client && is_connected) {
        int msg_id = esp_mqtt_client_subscribe(mqtt_client, topic, qos);
        ESP_LOGI(TAG, "Subscribed to topic '%s', msg_id=%d", topic, msg_id);
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t mqtt_client_publish(const char *topic, const char *payload, int qos, bool retain)
{
    if (mqtt_client && is_connected) {
        int msg_id = esp_mqtt_client_publish(mqtt_client, topic, payload, 0, qos, retain);
        ESP_LOGI(TAG, "Published to topic '%s', msg_id=%d", topic, msg_id);
        return ESP_OK;
    }
    return ESP_FAIL;
}

bool mqtt_client_is_connected(void)
{
    return is_connected;
}

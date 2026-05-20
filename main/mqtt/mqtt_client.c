#include <string.h>
#include "app_mqtt.h"
#include "esp_log.h"
#include "esp_event.h"
#include "mqtt_client.h"

static const char *TAG = "mqtt_client";

static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool is_connected = false;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            is_connected = true;
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
            ESP_LOGI(TAG, "MQTT data received");
            break;
            
        default:
            break;
    }
}

esp_err_t app_mqtt_connect(const char *broker_uri, uint16_t port, 
                                const char *username, const char *password)
{
    ESP_LOGI(TAG, "Connecting to MQTT broker: %s", broker_uri);
    
    // ESP-IDF v5.0 MQTT config
    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = broker_uri,
        .username = username,
        .password = password,
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

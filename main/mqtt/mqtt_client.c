#include <string.h>
#include "mqtt_client.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "state_machine.h"

static const char *TAG = "mqtt_client";
static esp_mqtt_client_handle_t mqtt_client = NULL;
static mqtt_config_t mqtt_config = {0};

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            mqtt_config.connected = true;
            state_machine_trigger_event(EVENT_MQTT_CONNECTED);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT disconnected");
            mqtt_config.connected = false;
            state_machine_trigger_event(EVENT_MQTT_DISCONNECTED);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT data received from topic %.*s", event->topic_len, event->topic);
            break;
        default:
            break;
    }
}

esp_err_t mqtt_client_init(void)
{
    ESP_LOGI(TAG, "Initializing MQTT client");
    return ESP_OK;
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
    
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = full_uri,
    };
    
    if (strlen(username) > 0) {
        mqtt_cfg.credentials.username = username;
        mqtt_cfg.credentials.authentication.password = password;
    }
    
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
    
    strncpy(mqtt_config.broker_uri, broker_uri, sizeof(mqtt_config.broker_uri) - 1);
    mqtt_config.port = port;
    strncpy(mqtt_config.username, username, sizeof(mqtt_config.username) - 1);
    strncpy(mqtt_config.password, password, sizeof(mqtt_config.password) - 1);
    
    return ESP_OK;
}

esp_err_t mqtt_client_disconnect(void)
{
    ESP_LOGI(TAG, "Disconnecting from MQTT broker");
    
    if (mqtt_client) {
        esp_mqtt_client_stop(mqtt_client);
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
    }
    
    mqtt_config.connected = false;
    return ESP_OK;
}

esp_err_t mqtt_client_subscribe(const char *topic, int qos)
{
    if (!mqtt_client || !mqtt_config.connected) {
        ESP_LOGE(TAG, "MQTT client not connected");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Subscribing to topic: %s", topic);
    int msg_id = esp_mqtt_client_subscribe(mqtt_client, topic, qos);
    return (msg_id < 0) ? ESP_FAIL : ESP_OK;
}

esp_err_t mqtt_client_publish(const char *topic, const char *payload, int qos, int retain)
{
    if (!mqtt_client || !mqtt_config.connected) {
        ESP_LOGE(TAG, "MQTT client not connected");
        return ESP_ERR_INVALID_STATE;
    }
    
    int msg_id = esp_mqtt_client_publish(mqtt_client, topic, payload, 0, qos, retain);
    return (msg_id < 0) ? ESP_FAIL : ESP_OK;
}

bool mqtt_client_is_connected(void)
{
    return mqtt_config.connected;
}

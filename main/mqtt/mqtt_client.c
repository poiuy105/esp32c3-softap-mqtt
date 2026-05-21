#include <string.h>
#include "app_mqtt.h"
#include "mqtt_command.h"
#include "ha_discovery.h"
#include "device_info.h"
#include "state_machine.h"
#include "esp_log.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "mqtt_client";

static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool is_connected = false;

// Mutex for thread-safe access
static SemaphoreHandle_t mqtt_mutex = NULL;

// Connection statistics
static struct {
    uint32_t connect_successes;
    uint32_t disconnect_count;
} conn_stats = {0};

// Buffer for formatted URI
static char formatted_uri[256];

// LWT topic buffer
static char lwt_topic[64] = {0};

// Helper macros for mutex lock/unlock
#define MQTT_LOCK()   do { if (mqtt_mutex) xSemaphoreTake(mqtt_mutex, portMAX_DELAY); } while(0)
#define MQTT_UNLOCK() do { if (mqtt_mutex) xSemaphoreGive(mqtt_mutex); } while(0)

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

// Build LWT topic from node_id
static void init_lwt_topic(void)
{
    snprintf(lwt_topic, sizeof(lwt_topic), "%s/status", device_info_get_node_id());
    ESP_LOGI(TAG, "LWT topic: %s", lwt_topic);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            MQTT_LOCK();
            is_connected = true;
            conn_stats.connect_successes++;
            MQTT_UNLOCK();
            
            // Subscribe to command topics
            mqtt_command_subscribe_all();
            
            // Publish online status
            ha_discovery_publish_online();
            
            // Publish HA discovery configs
            ha_discovery_publish_configs();
            
            // Publish initial states
            ha_discovery_publish_states();
            
            // Trigger state machine event
            state_machine_trigger_event(EVENT_MQTT_CONNECTED);
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT disconnected");
            MQTT_LOCK();
            is_connected = false;
            conn_stats.disconnect_count++;
            MQTT_UNLOCK();
            // ESP-MQTT handles auto-reconnect
            break;
            
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGD(TAG, "MQTT subscribed, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "MQTT published, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_DATA:
            // Handle command via mqtt_command module
            mqtt_command_handle(event->topic, event->topic_len, 
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
    
    // Format URI
    const char *fmt_uri = format_mqtt_uri(broker_uri, port);
    
    // Parse hostname, port and transport
    char hostname[128] = {0};
    int mqtt_port = 1883;
    esp_mqtt_transport_t transport = MQTT_TRANSPORT_OVER_TCP;
    const char *uri_start = fmt_uri;
    
    if (strncmp(fmt_uri, "mqtt://", 7) == 0) {
        uri_start = fmt_uri + 7;
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
    
    strncpy(hostname, uri_start, sizeof(hostname) - 1);
    char *colon = strchr(hostname, ':');
    if (colon) {
        *colon = '\0';
        mqtt_port = atoi(colon + 1);
    }
    char *slash = strchr(hostname, '/');
    if (slash) *slash = '\0';
    
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
        .network = {
            .reconnect_timeout_ms = 1000,
            .disable_auto_reconnect = false,
        },
    };
    
    if (mqtt_mutex == NULL) {
        mqtt_mutex = xSemaphoreCreateMutex();
    }
    
    MQTT_LOCK();
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (mqtt_client == NULL) {
        MQTT_UNLOCK();
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }
    
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_err_t ret = esp_mqtt_client_start(mqtt_client);
    MQTT_UNLOCK();
    
    return ret;
}

esp_err_t app_mqtt_disconnect(void)
{
    MQTT_LOCK();
    if (mqtt_client) {
        esp_mqtt_client_stop(mqtt_client);
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
        is_connected = false;
    }
    MQTT_UNLOCK();
    return ESP_OK;
}

esp_err_t app_mqtt_subscribe(const char *topic, int qos)
{
    MQTT_LOCK();
    if (mqtt_client && is_connected) {
        char topic_buf[128] = {0};
        strncpy(topic_buf, topic, sizeof(topic_buf) - 1);
        int msg_id = esp_mqtt_client_subscribe(mqtt_client, topic_buf, qos);
        MQTT_UNLOCK();
        ESP_LOGD(TAG, "Subscribed to '%s', msg_id=%d", topic, msg_id);
        return ESP_OK;
    }
    MQTT_UNLOCK();
    return ESP_FAIL;
}

esp_err_t app_mqtt_publish(const char *topic, const char *payload, int qos, int retain)
{
    MQTT_LOCK();
    if (mqtt_client && is_connected) {
        char topic_buf[128] = {0};
        strncpy(topic_buf, topic, sizeof(topic_buf) - 1);
        int msg_id = esp_mqtt_client_publish(mqtt_client, topic_buf, payload, 0, qos, retain);
        MQTT_UNLOCK();
        ESP_LOGD(TAG, "Published to '%s', msg_id=%d", topic, msg_id);
        return ESP_OK;
    }
    MQTT_UNLOCK();
    return ESP_FAIL;
}

bool app_mqtt_is_connected(void)
{
    MQTT_LOCK();
    bool connected = is_connected;
    MQTT_UNLOCK();
    return connected;
}

void app_mqtt_get_stats(mqtt_conn_stats_t *stats)
{
    if (stats) {
        MQTT_LOCK();
        stats->connect_successes = conn_stats.connect_successes;
        stats->disconnect_count = conn_stats.disconnect_count;
        MQTT_UNLOCK();
    }
}

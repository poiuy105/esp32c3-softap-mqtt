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
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "mqtt_client";

static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool is_connected = false;

// Mutex for thread-safe access
static SemaphoreHandle_t mqtt_mutex = NULL;

// Reconnection task
static TaskHandle_t reconnect_task_handle = NULL;
static volatile bool reconnect_enabled = false;

// Connection configuration (saved for reconnection)
static char saved_broker_uri[128] = {0};
static uint16_t saved_port = 0;
static char saved_username[64] = {0};
static char saved_password[64] = {0};

// Reconnection statistics
static struct {
    uint32_t connect_attempts;
    uint32_t connect_successes;
    uint32_t disconnect_count;
    uint32_t last_reconnect_delay_ms;
} conn_stats = {0};

// Buffer for formatted URI
static char formatted_uri[256];

// LWT topic buffer
static char lwt_topic[64] = {0};

// Node ID for topic prefix
static char node_id[32] = {0};

// Helper macros for mutex lock/unlock
#define MQTT_LOCK()   do { if (mqtt_mutex) xSemaphoreTake(mqtt_mutex, portMAX_DELAY); } while(0)
#define MQTT_UNLOCK() do { if (mqtt_mutex) xSemaphoreGive(mqtt_mutex); } while(0)

// Reconnection configuration
#define RECONNECT_MIN_DELAY_MS     1000    // 1 second
#define RECONNECT_MAX_DELAY_MS     60000   // 60 seconds
#define RECONNECT_BACKOFF_FACTOR   2       // Exponential backoff factor

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
            MQTT_LOCK();
            is_connected = true;
            conn_stats.connect_successes++;
            conn_stats.last_reconnect_delay_ms = RECONNECT_MIN_DELAY_MS;  // Reset backoff
            MQTT_UNLOCK();
            
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
            MQTT_LOCK();
            is_connected = false;
            conn_stats.disconnect_count++;
            MQTT_UNLOCK();
            
            // Trigger state machine event
            state_machine_trigger_event(EVENT_MQTT_DISCONNECTED);
            
            // Notify reconnection task
            if (reconnect_enabled && reconnect_task_handle) {
                xTaskNotifyGive(reconnect_task_handle);
            }
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

// Reconnection task with exponential backoff
static void mqtt_reconnect_task(void *pvParameters)
{
    uint32_t delay_ms = RECONNECT_MIN_DELAY_MS;
    
    ESP_LOGI(TAG, "Reconnection task started");
    
    while (reconnect_enabled) {
        // Wait for disconnect notification or timeout
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(delay_ms));
        
        if (!reconnect_enabled) {
            break;
        }
        
        // Check if already connected
        if (app_mqtt_is_connected()) {
            delay_ms = RECONNECT_MIN_DELAY_MS;  // Reset delay
            continue;
        }
        
        ESP_LOGI(TAG, "Attempting reconnection (delay: %lu ms, attempts: %lu)", 
                 delay_ms, conn_stats.connect_attempts);
        
        // Attempt reconnection
        conn_stats.connect_attempts++;
        esp_err_t ret = app_mqtt_connect(saved_broker_uri, saved_port, 
                                          saved_username[0] ? saved_username : NULL,
                                          saved_password[0] ? saved_password : NULL);
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Reconnection initiated");
        } else {
            ESP_LOGE(TAG, "Reconnection failed: %s", esp_err_to_name(ret));
        }
        
        // Exponential backoff
        delay_ms *= RECONNECT_BACKOFF_FACTOR;
        if (delay_ms > RECONNECT_MAX_DELAY_MS) {
            delay_ms = RECONNECT_MAX_DELAY_MS;
        }
        conn_stats.last_reconnect_delay_ms = delay_ms;
    }
    
    ESP_LOGI(TAG, "Reconnection task stopped");
    reconnect_task_handle = NULL;
    vTaskDelete(NULL);
}

// Start reconnection task
static void start_reconnect_task(void)
{
    if (reconnect_task_handle == NULL) {
        reconnect_enabled = true;
        xTaskCreate(mqtt_reconnect_task, "mqtt_reconnect", 4096, NULL, 5, &reconnect_task_handle);
        ESP_LOGI(TAG, "Reconnection task started");
    }
}

// Stop reconnection task
static void stop_reconnect_task(void)
{
    reconnect_enabled = false;
    if (reconnect_task_handle) {
        xTaskNotifyGive(reconnect_task_handle);
        // Task will delete itself
    }
}

esp_err_t app_mqtt_connect(const char *broker_uri, uint16_t port, 
                                const char *username, const char *password)
{
    ESP_LOGI(TAG, "Connecting to MQTT broker: %s (port: %d)", broker_uri, port);
    
    // Save configuration for reconnection
    strncpy(saved_broker_uri, broker_uri, sizeof(saved_broker_uri) - 1);
    saved_port = port;
    if (username) {
        strncpy(saved_username, username, sizeof(saved_username) - 1);
    } else {
        saved_username[0] = '\0';
    }
    if (password) {
        strncpy(saved_password, password, sizeof(saved_password) - 1);
    } else {
        saved_password[0] = '\0';
    }
    
    // Initialize LWT topic
    init_lwt_topic();
    
    // Start reconnection task
    start_reconnect_task();
    
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
    
    // Initialize mutex if not already done
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
    // Stop reconnection task first
    stop_reconnect_task();
    
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
        ESP_LOGI(TAG, "Subscribed to topic '%s', msg_id=%d", topic, msg_id);
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
        ESP_LOGI(TAG, "Published to topic '%s', msg_id=%d", topic, msg_id);
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
        stats->connect_attempts = conn_stats.connect_attempts;
        stats->connect_successes = conn_stats.connect_successes;
        stats->disconnect_count = conn_stats.disconnect_count;
        stats->last_reconnect_delay_ms = conn_stats.last_reconnect_delay_ms;
        MQTT_UNLOCK();
    }
}

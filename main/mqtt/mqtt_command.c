#include "mqtt_command.h"
#include "device_info.h"
#include "gpio_control.h"
#include "rmt_driver.h"
#include "nvs_config.h"
#include "ha_discovery.h"
#include "app_mqtt.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "mqtt_cmd";

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

void mqtt_command_subscribe_all(void)
{
    const char *node_id = device_info_get_node_id();
    char topic[64];
    
    snprintf(topic, sizeof(topic), "%s/led/set", node_id);
    app_mqtt_subscribe(topic, 1);
    
    snprintf(topic, sizeof(topic), "%s/light/power/set", node_id);
    app_mqtt_subscribe(topic, 1);
    
    snprintf(topic, sizeof(topic), "%s/light/freq/set", node_id);
    app_mqtt_subscribe(topic, 1);
    
    snprintf(topic, sizeof(topic), "%s/light/duty/set", node_id);
    app_mqtt_subscribe(topic, 1);
    
    snprintf(topic, sizeof(topic), "%s/sound/power/set", node_id);
    app_mqtt_subscribe(topic, 1);
    
    snprintf(topic, sizeof(topic), "%s/sound/freq/set", node_id);
    app_mqtt_subscribe(topic, 1);
    
    snprintf(topic, sizeof(topic), "%s/sound/vol/set", node_id);
    app_mqtt_subscribe(topic, 1);
    
    ESP_LOGI(TAG, "Subscribed to all command topics");
}

void mqtt_command_handle(const char *topic, int topic_len, 
                         const char *data, int data_len)
{
    char topic_buf[128] = {0};
    char data_buf[64] = {0};
    strncpy(topic_buf, topic, topic_len < 127 ? topic_len : 127);
    strncpy(data_buf, data, data_len < 63 ? data_len : 63);
    
    ESP_LOGI(TAG, "Command: topic=%s, data=%s", topic_buf, data_buf);
    
    const char *node_id = device_info_get_node_id();
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

#include "mqtt_command.h"
#include "device_info.h"
#include "gpio_control.h"
#include "pwm_driver.h"
#include "nvs_config.h"
#include "ha_discovery.h"
#include "app_mqtt.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static const char *TAG = "mqtt_cmd";

// Helper to save and publish state
static void save_and_publish_state(void)
{
    device_state_t state = {
        .led_state = gpio_get_led(),
        .light_enabled = pwm_get_light_enabled(),
        .light_freq = pwm_get_light_freq(),
        .light_duty = pwm_get_light_duty_x1000(),
        .sound_enabled = pwm_get_sound_enabled(),
        .sound_freq = pwm_get_sound_freq(),
        .sound_duty = pwm_get_sound_duty_x1000(),
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
        pwm_set_light(on, pwm_get_light_freq(), pwm_get_light_duty_x1000());
        save_and_publish_state();
        return;
    }
    
    // Light frequency
    snprintf(expected_topic, sizeof(expected_topic), "%s/light/freq/set", node_id);
    if (strcmp(topic_buf, expected_topic) == 0) {
        uint32_t freq = (uint32_t)atoi(data_buf);
        pwm_set_light(pwm_get_light_enabled(), freq, pwm_get_light_duty_x1000());
        save_and_publish_state();
        return;
    }
    
    // Light duty (e.g. "80.5" -> 80500 x1000)
    snprintf(expected_topic, sizeof(expected_topic), "%s/light/duty/set", node_id);
    if (strcmp(topic_buf, expected_topic) == 0) {
        double duty_percent = atof(data_buf);
        uint32_t duty_x1000 = (uint32_t)(duty_percent * 1000.0 + 0.5);  // Round
        pwm_set_light(pwm_get_light_enabled(), pwm_get_light_freq(), duty_x1000);
        save_and_publish_state();
        return;
    }
    
    // Sound power
    snprintf(expected_topic, sizeof(expected_topic), "%s/sound/power/set", node_id);
    if (strcmp(topic_buf, expected_topic) == 0) {
        bool on = (strcmp(data_buf, "ON") == 0);
        pwm_set_sound(on, pwm_get_sound_freq(), pwm_get_sound_duty_x1000());
        save_and_publish_state();
        return;
    }
    
    // Sound frequency
    snprintf(expected_topic, sizeof(expected_topic), "%s/sound/freq/set", node_id);
    if (strcmp(topic_buf, expected_topic) == 0) {
        uint32_t freq = (uint32_t)atoi(data_buf);
        pwm_set_sound(pwm_get_sound_enabled(), freq, pwm_get_sound_duty_x1000());
        save_and_publish_state();
        return;
    }
    
    // Sound volume (e.g. "55.5" -> 55500 x1000)
    snprintf(expected_topic, sizeof(expected_topic), "%s/sound/vol/set", node_id);
    if (strcmp(topic_buf, expected_topic) == 0) {
        double vol_percent = atof(data_buf);
        uint32_t vol_x1000 = (uint32_t)(vol_percent * 1000.0 + 0.5);  // Round
        pwm_set_sound(pwm_get_sound_enabled(), pwm_get_sound_freq(), vol_x1000);
        save_and_publish_state();
        return;
    }
    
    ESP_LOGW(TAG, "Unknown command topic: %s", topic_buf);
}

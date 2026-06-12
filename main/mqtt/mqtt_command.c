#include "mqtt_command.h"
#include "device_info.h"
#include "gpio_control.h"
#include "pwm_driver.h"
#include "nvs_config.h"
#include "ha_discovery.h"
#include "app_mqtt.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "mqtt_cmd";

static void save_and_publish_state(void)
{
    device_state_t state = {
        .led_state = gpio_get_led(),
        .light_enabled = pwmGetLightEnable(),
        .light_freq = pwmGetLightFreq(),
        .light_duty = pwmGetLightDuty(),
        .sound_enabled = pwmGetSoundEnable(),
        .sound_freq = pwmGetSoundFreq(),
        .sound_duty = pwmGetSoundDuty(),
    };
    nvs_save_device_state(&state);
    
    // Only publish to MQTT if connected to avoid blocking
    if (app_mqtt_is_connected()) {
        ha_discovery_publish_states();
    } else {
        ESP_LOGW(TAG, "MQTT not connected, skipping state publish");
    }
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
    
    snprintf(topic, sizeof(topic), "%s/restart/set", node_id);
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
    
    // LED control (independent, not part of PWM entities)
    snprintf(expected_topic, sizeof(expected_topic), "%s/led/set", node_id);
    if (strcmp(topic_buf, expected_topic) == 0) {
        bool on = (strcmp(data_buf, "ON") == 0);
        gpio_set_led(on);
        save_and_publish_state();
        return;
    }
    
    // Light power - only change enable, preserve freq and duty
    snprintf(expected_topic, sizeof(expected_topic), "%s/light/power/set", node_id);
    if (strcmp(topic_buf, expected_topic) == 0) {
        bool on = (strcmp(data_buf, "ON") == 0);
        pwmSetLightEnable(on);
        pwmApplyLight();
        save_and_publish_state();
        return;
    }
    
    // Light frequency - only change freq, preserve enable and duty
    snprintf(expected_topic, sizeof(expected_topic), "%s/light/freq/set", node_id);
    if (strcmp(topic_buf, expected_topic) == 0) {
        uint32_t freq = (uint32_t)atoi(data_buf);
        pwmSetLightFreq(freq);
        pwmApplyLight();
        save_and_publish_state();
        return;
    }
    
    // Light duty - only change duty, preserve enable and freq
    snprintf(expected_topic, sizeof(expected_topic), "%s/light/duty/set", node_id);
    if (strcmp(topic_buf, expected_topic) == 0) {
        double duty_percent = atof(data_buf);
        uint32_t duty_x1000 = (uint32_t)(duty_percent * 1000.0 + 0.5);
        pwmSetLightDuty(duty_x1000);
        pwmApplyLight();
        save_and_publish_state();
        return;
    }
    
    // Sound power - only change enable, preserve freq and duty
    snprintf(expected_topic, sizeof(expected_topic), "%s/sound/power/set", node_id);
    if (strcmp(topic_buf, expected_topic) == 0) {
        bool on = (strcmp(data_buf, "ON") == 0);
        pwmSetSoundEnable(on);
        pwmApplySound();
        save_and_publish_state();
        return;
    }
    
    // Sound frequency - only change freq, preserve enable and duty
    snprintf(expected_topic, sizeof(expected_topic), "%s/sound/freq/set", node_id);
    if (strcmp(topic_buf, expected_topic) == 0) {
        uint32_t freq = (uint32_t)atoi(data_buf);
        pwmSetSoundFreq(freq);
        pwmApplySound();
        save_and_publish_state();
        return;
    }
    
    // Sound volume - only change duty, preserve enable and freq
    snprintf(expected_topic, sizeof(expected_topic), "%s/sound/vol/set", node_id);
    if (strcmp(topic_buf, expected_topic) == 0) {
        double vol_percent = atof(data_buf);
        uint32_t vol_x1000 = (uint32_t)(vol_percent * 1000.0 + 0.5);
        pwmSetSoundDuty(vol_x1000);
        pwmApplySound();
        save_and_publish_state();
        return;
    }
    
    // Restart device
    snprintf(expected_topic, sizeof(expected_topic), "%s/restart/set", node_id);
    if (strcmp(topic_buf, expected_topic) == 0) {
        ESP_LOGI(TAG, "Restart command received, rebooting...");
        // Publish offline status before reboot so HA knows device is going away
        ha_discovery_publish_offline();
        // Small delay to allow MQTT message to be sent
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
        return;
    }
    
    ESP_LOGW(TAG, "Unknown command topic: %s", topic_buf);
}

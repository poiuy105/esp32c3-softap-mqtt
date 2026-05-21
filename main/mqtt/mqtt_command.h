#ifndef MQTT_COMMAND_H
#define MQTT_COMMAND_H

#include "esp_err.h"

/**
 * @brief Handle incoming MQTT command
 * 
 * @param topic Topic string
 * @param topic_len Topic length
 * @param data Data payload
 * @param data_len Data length
 */
void mqtt_command_handle(const char *topic, int topic_len, 
                         const char *data, int data_len);

/**
 * @brief Subscribe to all command topics
 * Called after MQTT connection is established
 */
void mqtt_command_subscribe_all(void);

#endif // MQTT_COMMAND_H

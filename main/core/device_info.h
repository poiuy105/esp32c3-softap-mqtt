#ifndef DEVICE_INFO_H
#define DEVICE_INFO_H

#include <stdint.h>

/**
 * @brief Get device node ID (e.g., "esp32c3_aabbccdd")
 * @return Node ID string (static buffer, do not free)
 */
const char* device_info_get_node_id(void);

/**
 * @brief Get device MAC address string (e.g., "AA:BB:CC:DD:EE:FF")
 * @return MAC string (static buffer, do not free)
 */
const char* device_info_get_mac_string(void);

/**
 * @brief Get device name (e.g., "PWM Esp32c3 AA:BB:CC")
 * @return Device name string (static buffer, do not free)
 */
const char* device_info_get_device_name(void);

/**
 * @brief Initialize device info (called automatically on first use)
 */
void device_info_init(void);

#endif // DEVICE_INFO_H

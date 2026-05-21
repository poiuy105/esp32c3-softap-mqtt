#ifndef SYSTEM_MONITOR_H
#define SYSTEM_MONITOR_H

#include "esp_err.h"
#include <stdint.h>

// System statistics structure
typedef struct {
    uint32_t free_heap;
    uint32_t min_free_heap;
    uint8_t wifi_rssi;
    uint32_t uptime_seconds;
    uint32_t mqtt_connect_count;
    uint32_t mqtt_disconnect_count;
} system_stats_t;

/**
 * @brief Initialize system monitor
 */
esp_err_t system_monitor_init(void);

/**
 * @brief Get current system statistics
 */
esp_err_t system_monitor_get_stats(system_stats_t *stats);

/**
 * @brief Log system statistics
 */
void system_monitor_log_stats(void);

/**
 * @brief Get free heap size
 */
uint32_t system_monitor_get_free_heap(void);

/**
 * @brief Get minimum free heap ever seen
 */
uint32_t system_monitor_get_min_free_heap(void);

#endif // SYSTEM_MONITOR_H

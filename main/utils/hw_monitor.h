#ifndef HW_MONITOR_H
#define HW_MONITOR_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// Hardware monitoring configuration
#define HW_MONITOR_TEMP_WARNING_C    80   // Temperature warning threshold (°C)
#define HW_MONITOR_TEMP_CRITICAL_C   90   // Temperature critical threshold (°C)

/**
 * @brief Hardware status structure
 */
typedef struct {
    float temperature_c;        // Internal temperature in Celsius
    bool temp_warning;          // Temperature warning active
    bool temp_critical;         // Temperature critical active
    uint32_t boot_count;        // Number of boots (from NVS)
    uint32_t last_reset_reason; // Last reset reason
} hw_status_t;

/**
 * @brief Initialize hardware monitor
 */
esp_err_t hw_monitor_init(void);

/**
 * @brief Get current hardware status
 */
esp_err_t hw_monitor_get_status(hw_status_t *status);

/**
 * @brief Log hardware status
 */
void hw_monitor_log_status(void);

/**
 * @brief Check if temperature is within safe range
 */
bool hw_monitor_is_temp_safe(void);

/**
 * @brief Get internal temperature (Celsius)
 */
float hw_monitor_get_temperature(void);

/**
 * @brief Increment and save boot counter
 */
esp_err_t hw_monitor_record_boot(void);

/**
 * @brief Get boot count
 */
uint32_t hw_monitor_get_boot_count(void);

#endif // HW_MONITOR_H

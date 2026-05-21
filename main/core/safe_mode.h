#ifndef SAFE_MODE_H
#define SAFE_MODE_H

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Safe mode error codes
 */
typedef enum {
    SAFE_MODE_ERR_NONE = 0,
    SAFE_MODE_ERR_NVS_INIT,
    SAFE_MODE_ERR_NETIF_INIT,
    SAFE_MODE_ERR_EVENT_LOOP,
    SAFE_MODE_ERR_WIFI_INIT,
    SAFE_MODE_ERR_MQTT_INIT,
    SAFE_MODE_ERR_DRIVER_INIT,
} safe_mode_error_t;

/**
 * @brief Enter safe mode due to critical error
 * 
 * Safe mode will:
 * - Stop normal operation
 * - Start SoftAP for reconfiguration
 * - Blink LED rapidly to indicate error
 * - Log error details
 * 
 * @param error Error code indicating failure reason
 * @param msg Additional error message
 */
void safe_mode_enter(safe_mode_error_t error, const char *msg);

/**
 * @brief Check if system is in safe mode
 * 
 * @return true if in safe mode
 * @return false if normal operation
 */
bool safe_mode_is_active(void);

/**
 * @brief Get safe mode error code
 * 
 * @return safe_mode_error_t Last error that caused safe mode
 */
safe_mode_error_t safe_mode_get_error(void);

/**
 * @brief Exit safe mode and restart system
 */
void safe_mode_exit_and_restart(void);

/**
 * @brief Handle critical error with optional safe mode fallback
 * 
 * @param err ESP error code
 * @param msg Error message
 * @param enter_safe_mode If true, enter safe mode on error
 * @return esp_err_t Original error code
 */
esp_err_t safe_mode_handle_error(esp_err_t err, const char *msg, bool enter_safe_mode);

#endif // SAFE_MODE_H

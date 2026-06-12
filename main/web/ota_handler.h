#ifndef OTA_HANDLER_H
#define OTA_HANDLER_H

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Register OTA HTTP upload handler
 * Registers POST /api/ota endpoint for firmware upload
 */
esp_err_t ota_handler_register(void);

/**
 * @brief Check if OTA update is in progress
 */
bool ota_is_updating(void);

#endif // OTA_HANDLER_H

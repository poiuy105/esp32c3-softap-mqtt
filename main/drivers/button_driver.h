#ifndef BUTTON_DRIVER_H
#define BUTTON_DRIVER_H

#include "esp_err.h"

// Button GPIO (active low, pulled high)
#define GPIO_BTN    9

/**
 * @brief Callback type for factory reset
 */
typedef void (*button_factory_reset_callback_t)(void);

/**
 * @brief Initialize button driver with long-press detection
 *        Long press >= 3s: LED warning blink
 *        Long press >= 5s: factory reset (calls callback)
 * 
 * @param callback Function to call when factory reset is triggered
 * @return esp_err_t ESP_OK on success
 */
esp_err_t button_init(button_factory_reset_callback_t callback);

#endif // BUTTON_DRIVER_H

#ifndef GPIO_CONTROL_H
#define GPIO_CONTROL_H

#include "esp_err.h"
#include <stdbool.h>

// LED GPIO (active low)
#define GPIO_LED    4

/**
 * @brief Initialize GPIO control for LED
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gpio_control_init(void);

/**
 * @brief Set LED state
 * 
 * @param on true = LED on (GPIO low), false = LED off (GPIO high)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gpio_set_led(bool on);

/**
 * @brief Get current LED state
 * 
 * @return true LED is on
 * @return false LED is off
 */
bool gpio_get_led(void);

#endif // GPIO_CONTROL_H

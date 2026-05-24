#ifndef GPIO_CONTROL_H
#define GPIO_CONTROL_H

#include "esp_err.h"
#include <stdbool.h>

// LED GPIO (active low)
#define GPIO_LED    4

// GPIO0 - always low output
#define GPIO_LOW_OUT 0

// LED blink modes
typedef enum {
    LED_MODE_OFF = 0,       // LED off
    LED_MODE_ON,            // LED solid on
    LED_MODE_SLOW_BLINK,    // 1s on / 1s off (waiting for config)
    LED_MODE_FAST_BLINK,    // 200ms on / 200ms off (connecting)
    LED_MODE_WARN_BLINK,    // 100ms on / 100ms off (factory reset warning)
} led_mode_t;

/**
 * @brief Initialize GPIO control for LED
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gpio_control_init(void);

/**
 * @brief Set LED on/off (manual control, overrides blink mode)
 */
esp_err_t gpio_set_led(bool on);

/**
 * @brief Get current LED state
 */
bool gpio_get_led(void);

/**
 * @brief Set LED blink mode (starts a timer task)
 */
esp_err_t gpio_set_led_mode(led_mode_t mode);

/**
 * @brief Get current LED mode
 */
led_mode_t gpio_get_led_mode(void);

#endif // GPIO_CONTROL_H

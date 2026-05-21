#ifndef RMT_DRIVER_H
#define RMT_DRIVER_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// RMT channel assignments
#define RMT_CHAN_LIGHT  0   // 照明PWM - IO3
#define RMT_CHAN_SOUND  1   // 声波PWM - IO1

// GPIO assignments
#define GPIO_LIGHT_PWM  3   // 照明PWM输出
#define GPIO_SOUND_PWM  1   // 声波PWM输出
#define GPIO_SOUND_EN   6   // 声波使能

// Frequency limits
#define FREQ_MIN        0
#define FREQ_MAX        150000  // 150kHz

// Duty cycle limits
#define DUTY_LIGHT_MIN  0
#define DUTY_LIGHT_MAX  100
#define DUTY_SOUND_MIN  50
#define DUTY_SOUND_MAX  100

/**
 * @brief Initialize RMT driver for PWM output
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t rmt_driver_init(void);

/**
 * @brief Set light PWM parameters
 * 
 * @param enable Enable/disable PWM output
 * @param freq_hz Frequency in Hz (0-150000)
 * @param duty_percent Duty cycle in percent (0-100)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t rmt_set_light(bool enable, uint32_t freq_hz, uint8_t duty_percent);

/**
 * @brief Set sound PWM parameters
 * 
 * @param enable Enable/disable PWM output (also controls GPIO6)
 * @param freq_hz Frequency in Hz (0-150000)
 * @param duty_percent Duty cycle in percent (50-100)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t rmt_set_sound(bool enable, uint32_t freq_hz, uint8_t duty_percent);

/**
 * @brief Get current light state
 */
bool rmt_get_light_enabled(void);
uint32_t rmt_get_light_freq(void);
uint8_t rmt_get_light_duty(void);

/**
 * @brief Get current sound state
 */
bool rmt_get_sound_enabled(void);
uint32_t rmt_get_sound_freq(void);
uint8_t rmt_get_sound_duty(void);

#endif // RMT_DRIVER_H

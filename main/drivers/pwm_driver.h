#ifndef PWM_DRIVER_H
#define PWM_DRIVER_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// GPIO assignments
#define GPIO_LIGHT_PWM  3   // 照明PWM输出
#define GPIO_SOUND_PWM  1   // 声波PWM输出
#define GPIO_SOUND_EN   6   // 声波使能

// Frequency limits
#define FREQ_MIN        0
#define FREQ_MAX        150000  // 150kHz

// Duty cycle limits (x10 precision: 0~1000 = 0.0%~100.0%)
#define DUTY_X10_MIN            0
#define DUTY_X10_MAX            1000
#define DUTY_X10_LIGHT_MIN      0
#define DUTY_X10_LIGHT_MAX      1000
#define DUTY_X10_SOUND_MIN      500    // 50.0%
#define DUTY_X10_SOUND_MAX      1000   // 100.0%

/**
 * @brief Initialize PWM driver (LEDC with dynamic resolution)
 */
esp_err_t pwm_driver_init(void);

/**
 * @brief Set light PWM parameters
 * @param enable Enable/disable PWM output
 * @param freq_hz Frequency in Hz (0-150000)
 * @param duty_x10 Duty cycle ×10 (0-1000, e.g. 453 = 45.3%)
 */
esp_err_t pwm_set_light(bool enable, uint32_t freq_hz, uint16_t duty_x10);

/**
 * @brief Set sound PWM parameters
 * @param enable Enable/disable PWM output (also controls GPIO6)
 * @param freq_hz Frequency in Hz (0-150000)
 * @param duty_x10 Duty cycle ×10 (500-1000, e.g. 750 = 75.0%)
 */
esp_err_t pwm_set_sound(bool enable, uint32_t freq_hz, uint16_t duty_x10);

/**
 * @brief Get current light state
 */
bool pwm_get_light_enabled(void);
uint32_t pwm_get_light_freq(void);
uint16_t pwm_get_light_duty_x10(void);  // Returns actual quantized value

/**
 * @brief Get current sound state
 */
bool pwm_get_sound_enabled(void);
uint32_t pwm_get_sound_freq(void);
uint16_t pwm_get_sound_duty_x10(void);  // Returns actual quantized value

#endif // PWM_DRIVER_H

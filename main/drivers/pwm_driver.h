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

// Duty cycle limits (x1000 precision: 0~100000 = 0.000%~100.000%)
// Step=0.1% = 100, 照明默认=80000(80.0%), 声波默认=75000(75.0%)
#define DUTY_X1000_MIN            0
#define DUTY_X1000_MAX            100000
#define DUTY_X1000_LIGHT_MIN      0
#define DUTY_X1000_LIGHT_MAX      100000
#define DUTY_X1000_SOUND_MIN      50000    // 50.0%
#define DUTY_X1000_SOUND_MAX      100000   // 100.0%

/**
 * @brief Initialize PWM driver (LEDC with dynamic resolution)
 */
esp_err_t pwm_driver_init(void);

/**
 * @brief Set light PWM parameters
 * @param enable Enable/disable PWM output
 * @param freq_hz Frequency in Hz (0-150000)
 * @param duty_x1000 Duty cycle ×1000 (0-100000, e.g. 80500 = 80.500%)
 */
esp_err_t pwm_set_light(bool enable, uint32_t freq_hz, uint32_t duty_x1000);

/**
 * @brief Set sound PWM parameters
 * @param enable Enable/disable PWM output (also controls GPIO6)
 * @param freq_hz Frequency in Hz (0-150000)
 * @param duty_x1000 Duty cycle ×1000 (50000-100000, e.g. 75000 = 75.000%)
 */
esp_err_t pwm_set_sound(bool enable, uint32_t freq_hz, uint32_t duty_x1000);

/**
 * @brief Get current light state
 */
bool pwm_get_light_enabled(void);
uint32_t pwm_get_light_freq(void);
uint32_t pwm_get_light_duty_x1000(void);  // Returns actual quantized value (0-100000)

/**
 * @brief Get current sound state
 */
bool pwm_get_sound_enabled(void);
uint32_t pwm_get_sound_freq(void);
uint32_t pwm_get_sound_duty_x1000(void);  // Returns actual quantized value (0-100000)

#endif // PWM_DRIVER_H

#ifndef PWM_DRIVER_H
#define PWM_DRIVER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// GPIO assignments
#define GPIO_LIGHT_PWM  3   // 照明PWM输出
#define GPIO_SOUND_PWM  1   // 声波PWM输出
#define GPIO_SOUND_EN   6   // 声波使能

// Limits
#define PWM_FREQ_MIN        0
#define PWM_FREQ_MAX        150000  // 150kHz
#define PWM_DUTY_MIN        0
#define PWM_DUTY_MAX        100000  // 100.000% (x1000)
#define PWM_SOUND_DUTY_MIN  50000   // 50.000% (x1000)

// Entity enum
typedef enum {
    PWM_ENTITY_LIGHT = 0,
    PWM_ENTITY_SOUND,
    PWM_ENTITY_MAX
} PwmEntity;

// ========== Init ==========
esp_err_t pwmDriverInit(void);

// ========== Light Setters (modify cache only) ==========
esp_err_t pwmSetLightEnable(bool enable);
esp_err_t pwmSetLightFreq(uint32_t freqHz);
esp_err_t pwmSetLightDuty(uint32_t dutyX1000);

// ========== Sound Setters (modify cache only) ==========
esp_err_t pwmSetSoundEnable(bool enable);
esp_err_t pwmSetSoundFreq(uint32_t freqHz);
esp_err_t pwmSetSoundDuty(uint32_t dutyX1000);

// ========== Apply (write cache to hardware) ==========
esp_err_t pwmApplyLight(void);
esp_err_t pwmApplySound(void);

// ========== Generic Interface ==========
esp_err_t pwmSetEnable(PwmEntity entity, bool enable);
esp_err_t pwmSetFreq(PwmEntity entity, uint32_t freqHz);
esp_err_t pwmSetDuty(PwmEntity entity, uint32_t dutyX1000);
esp_err_t pwmApply(PwmEntity entity);

// ========== Light Getters ==========
bool pwmGetLightEnable(void);
uint32_t pwmGetLightFreq(void);
uint32_t pwmGetLightDuty(void);

// ========== Sound Getters ==========
bool pwmGetSoundEnable(void);
uint32_t pwmGetSoundFreq(void);
uint32_t pwmGetSoundDuty(void);

// ========== Generic Getters ==========
bool pwmGetEnable(PwmEntity entity);
uint32_t pwmGetFreq(PwmEntity entity);
uint32_t pwmGetDuty(PwmEntity entity);

#endif // PWM_DRIVER_H

#include "pwm_driver.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "PWM";

#define LEDC_SPEED_MODE    LEDC_LOW_SPEED_MODE
#define APB_CLK_FREQ_HZ    80000000

// ========== Internal State ==========

typedef struct {
    bool enable;
    uint32_t freqHz;
    uint32_t dutyX1000;
    uint8_t resolution;
    bool dirty;
} PwmEntityCache;

typedef struct {
    PwmEntityCache cache[PWM_ENTITY_MAX];
    SemaphoreHandle_t mutex;
} PwmDriverContext;

static PwmDriverContext pwmCtx;

// LEDC hardware mapping
static const int gpioMap[PWM_ENTITY_MAX] = {
    [PWM_ENTITY_LIGHT] = GPIO_LIGHT_PWM,
    [PWM_ENTITY_SOUND] = GPIO_SOUND_PWM,
};

static const ledc_channel_t channelMap[PWM_ENTITY_MAX] = {
    [PWM_ENTITY_LIGHT] = LEDC_CHANNEL_0,
    [PWM_ENTITY_SOUND] = LEDC_CHANNEL_1,
};

static const ledc_timer_t timerMap[PWM_ENTITY_MAX] = {
    [PWM_ENTITY_LIGHT] = LEDC_TIMER_0,
    [PWM_ENTITY_SOUND] = LEDC_TIMER_1,
};

// ========== Internal Helpers ==========

#define PWM_LOCK()   xSemaphoreTake(pwmCtx.mutex, portMAX_DELAY)
#define PWM_UNLOCK() xSemaphoreGive(pwmCtx.mutex)

static uint8_t calcResolution(uint32_t freqHz)
{
    if (freqHz == 0) return 10;
    for (uint8_t res = 13; res >= 8; res--) {
        if ((APB_CLK_FREQ_HZ >> res) >= freqHz) return res;
    }
    return 8;
}

// ========== Init ==========

esp_err_t pwmDriverInit(void)
{
    ESP_LOGI(TAG, "Initializing PWM driver (independent entity architecture)");

    pwmCtx.mutex = xSemaphoreCreateMutex();

    // Configure LEDC channels
    for (int i = 0; i < PWM_ENTITY_MAX; i++) {
        ledc_channel_config_t chCfg = {
            .gpio_num = gpioMap[i],
            .speed_mode = LEDC_SPEED_MODE,
            .channel = channelMap[i],
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = timerMap[i],
            .duty = 0,
            .hpoint = 0,
        };
        esp_err_t ret = ledc_channel_config(&chCfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure channel %d: %s", i, esp_err_to_name(ret));
            return ret;
        }

        pwmCtx.cache[i].enable = false;
        pwmCtx.cache[i].freqHz = 0;
        pwmCtx.cache[i].dutyX1000 = 0;
        pwmCtx.cache[i].resolution = 10;
        pwmCtx.cache[i].dirty = false;
    }

    // Sound enable GPIO
    gpio_config_t ioConf = {
        .pin_bit_mask = (1ULL << GPIO_SOUND_EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&ioConf);
    gpio_set_level(GPIO_SOUND_EN, 0);

    ESP_LOGI(TAG, "PWM driver initialized (2 entities: light, sound)");
    return ESP_OK;
}

// ========== Light Setters ==========

esp_err_t pwmSetLightEnable(bool enable)
{
    PWM_LOCK();
    pwmCtx.cache[PWM_ENTITY_LIGHT].enable = enable;
    pwmCtx.cache[PWM_ENTITY_LIGHT].dirty = true;
    ESP_LOGI(TAG, "Light enable staged: %d", enable);
    PWM_UNLOCK();
    return ESP_OK;
}

esp_err_t pwmSetLightFreq(uint32_t freqHz)
{
    if (freqHz > PWM_FREQ_MAX) freqHz = PWM_FREQ_MAX;
    PWM_LOCK();
    pwmCtx.cache[PWM_ENTITY_LIGHT].freqHz = freqHz;
    pwmCtx.cache[PWM_ENTITY_LIGHT].dirty = true;
    ESP_LOGI(TAG, "Light freq staged: %lu Hz", (unsigned long)freqHz);
    PWM_UNLOCK();
    return ESP_OK;
}

esp_err_t pwmSetLightDuty(uint32_t dutyX1000)
{
    if (dutyX1000 > PWM_DUTY_MAX) dutyX1000 = PWM_DUTY_MAX;
    PWM_LOCK();
    pwmCtx.cache[PWM_ENTITY_LIGHT].dutyX1000 = dutyX1000;
    pwmCtx.cache[PWM_ENTITY_LIGHT].dirty = true;
    ESP_LOGI(TAG, "Light duty staged: %lu.%lu%%",
             (unsigned long)(dutyX1000 / 1000), (unsigned long)((dutyX1000 % 1000) / 100));
    PWM_UNLOCK();
    return ESP_OK;
}

// ========== Sound Setters ==========

esp_err_t pwmSetSoundEnable(bool enable)
{
    PWM_LOCK();
    pwmCtx.cache[PWM_ENTITY_SOUND].enable = enable;
    pwmCtx.cache[PWM_ENTITY_SOUND].dirty = true;
    ESP_LOGI(TAG, "Sound enable staged: %d", enable);
    PWM_UNLOCK();
    return ESP_OK;
}

esp_err_t pwmSetSoundFreq(uint32_t freqHz)
{
    if (freqHz > PWM_FREQ_MAX) freqHz = PWM_FREQ_MAX;
    PWM_LOCK();
    pwmCtx.cache[PWM_ENTITY_SOUND].freqHz = freqHz;
    pwmCtx.cache[PWM_ENTITY_SOUND].dirty = true;
    ESP_LOGI(TAG, "Sound freq staged: %lu Hz", (unsigned long)freqHz);
    PWM_UNLOCK();
    return ESP_OK;
}

esp_err_t pwmSetSoundDuty(uint32_t dutyX1000)
{
    if (dutyX1000 < PWM_SOUND_DUTY_MIN) dutyX1000 = PWM_SOUND_DUTY_MIN;
    if (dutyX1000 > PWM_DUTY_MAX) dutyX1000 = PWM_DUTY_MAX;
    PWM_LOCK();
    pwmCtx.cache[PWM_ENTITY_SOUND].dutyX1000 = dutyX1000;
    pwmCtx.cache[PWM_ENTITY_SOUND].dirty = true;
    ESP_LOGI(TAG, "Sound duty staged: %lu.%lu%%",
             (unsigned long)(dutyX1000 / 1000), (unsigned long)((dutyX1000 % 1000) / 100));
    PWM_UNLOCK();
    return ESP_OK;
}

// ========== Generic Setters ==========

esp_err_t pwmSetEnable(PwmEntity entity, bool enable)
{
    if (entity >= PWM_ENTITY_MAX) return ESP_ERR_INVALID_ARG;
    PWM_LOCK();
    pwmCtx.cache[entity].enable = enable;
    pwmCtx.cache[entity].dirty = true;
    PWM_UNLOCK();
    return ESP_OK;
}

esp_err_t pwmSetFreq(PwmEntity entity, uint32_t freqHz)
{
    if (entity >= PWM_ENTITY_MAX) return ESP_ERR_INVALID_ARG;
    if (freqHz > PWM_FREQ_MAX) freqHz = PWM_FREQ_MAX;
    PWM_LOCK();
    pwmCtx.cache[entity].freqHz = freqHz;
    pwmCtx.cache[entity].dirty = true;
    PWM_UNLOCK();
    return ESP_OK;
}

esp_err_t pwmSetDuty(PwmEntity entity, uint32_t dutyX1000)
{
    if (entity >= PWM_ENTITY_MAX) return ESP_ERR_INVALID_ARG;
    if (dutyX1000 > PWM_DUTY_MAX) dutyX1000 = PWM_DUTY_MAX;
    PWM_LOCK();
    pwmCtx.cache[entity].dutyX1000 = dutyX1000;
    pwmCtx.cache[entity].dirty = true;
    PWM_UNLOCK();
    return ESP_OK;
}

// ========== Apply ==========

esp_err_t pwmApply(PwmEntity entity)
{
    if (entity >= PWM_ENTITY_MAX) return ESP_ERR_INVALID_ARG;

    PWM_LOCK();

    PwmEntityCache *c = &pwmCtx.cache[entity];

    if (!c->dirty) {
        PWM_UNLOCK();
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Apply entity %d: en=%d, freq=%luHz, duty=%lu.%lu%%",
             entity, c->enable, (unsigned long)c->freqHz,
             (unsigned long)(c->dutyX1000 / 1000), (unsigned long)((c->dutyX1000 % 1000) / 100));

    // Sound: control enable GPIO
    if (entity == PWM_ENTITY_SOUND) {
        gpio_set_level(GPIO_SOUND_EN, c->enable ? 1 : 0);
    }

    bool shouldOutput = c->enable && c->freqHz > 0 && c->dutyX1000 > 0;

    if (shouldOutput) {
        // Calculate dynamic resolution
        uint8_t resolution = calcResolution(c->freqHz);
        c->resolution = resolution;

        // Configure timer
        ledc_timer_config_t timerCfg = {
            .speed_mode = LEDC_SPEED_MODE,
            .timer_num = timerMap[entity],
            .duty_resolution = resolution,
            .freq_hz = c->freqHz,
            .clk_cfg = LEDC_AUTO_CLK,
        };
        ledc_timer_config(&timerCfg);

        // Calculate and set duty
        uint32_t maxDuty = (1 << resolution) - 1;
        uint32_t ledcDuty = (maxDuty * c->dutyX1000) / PWM_DUTY_MAX;
        ledc_set_duty(LEDC_SPEED_MODE, channelMap[entity], ledcDuty);
        ledc_update_duty(LEDC_SPEED_MODE, channelMap[entity]);

        ESP_LOGI(TAG, "Entity %d output: ledc_duty=%lu/%lu (res=%d-bit)",
                 entity, (unsigned long)ledcDuty, (unsigned long)maxDuty, resolution);
    } else {
        // Stop output, preserve cached values
        ledc_set_duty(LEDC_SPEED_MODE, channelMap[entity], 0);
        ledc_update_duty(LEDC_SPEED_MODE, channelMap[entity]);

        ESP_LOGI(TAG, "Entity %d output stopped (en=%d, freq=%lu, duty=%lu)",
                 entity, c->enable, (unsigned long)c->freqHz, (unsigned long)c->dutyX1000);
    }

    c->dirty = false;

    PWM_UNLOCK();
    return ESP_OK;
}

esp_err_t pwmApplyLight(void)
{
    return pwmApply(PWM_ENTITY_LIGHT);
}

esp_err_t pwmApplySound(void)
{
    return pwmApply(PWM_ENTITY_SOUND);
}

// ========== Light Getters ==========

bool pwmGetLightEnable(void) { return pwmGetEnable(PWM_ENTITY_LIGHT); }
uint32_t pwmGetLightFreq(void) { return pwmGetFreq(PWM_ENTITY_LIGHT); }
uint32_t pwmGetLightDuty(void) { return pwmGetDuty(PWM_ENTITY_LIGHT); }

// ========== Sound Getters ==========

bool pwmGetSoundEnable(void) { return pwmGetEnable(PWM_ENTITY_SOUND); }
uint32_t pwmGetSoundFreq(void) { return pwmGetFreq(PWM_ENTITY_SOUND); }
uint32_t pwmGetSoundDuty(void) { return pwmGetDuty(PWM_ENTITY_SOUND); }

// ========== Generic Getters ==========

bool pwmGetEnable(PwmEntity entity)
{
    if (entity >= PWM_ENTITY_MAX) return false;
    PWM_LOCK();
    bool val = pwmCtx.cache[entity].enable;
    PWM_UNLOCK();
    return val;
}

uint32_t pwmGetFreq(PwmEntity entity)
{
    if (entity >= PWM_ENTITY_MAX) return 0;
    PWM_LOCK();
    uint32_t val = pwmCtx.cache[entity].freqHz;
    PWM_UNLOCK();
    return val;
}

uint32_t pwmGetDuty(PwmEntity entity)
{
    if (entity >= PWM_ENTITY_MAX) return 0;
    PWM_LOCK();
    uint32_t val = pwmCtx.cache[entity].dutyX1000;
    PWM_UNLOCK();
    return val;
}

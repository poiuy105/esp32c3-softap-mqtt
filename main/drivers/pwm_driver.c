#include "pwm_driver.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "PWM";

// Mutex for thread-safe access
static SemaphoreHandle_t pwm_mutex = NULL;

#define PWM_LOCK()   do { if (pwm_mutex) xSemaphoreTake(pwm_mutex, portMAX_DELAY); } while(0)
#define PWM_UNLOCK() do { if (pwm_mutex) xSemaphoreGive(pwm_mutex); } while(0)

// LEDC channel assignments
#define LEDC_TIMER_LIGHT   LEDC_TIMER_0
#define LEDC_TIMER_SOUND   LEDC_TIMER_1
#define LEDC_CHANNEL_LIGHT LEDC_CHANNEL_0
#define LEDC_CHANNEL_SOUND LEDC_CHANNEL_1
#define LEDC_SPEED_MODE    LEDC_LOW_SPEED_MODE

// APB clock frequency (80MHz on ESP32-C3)
#define APB_CLK_FREQ_HZ    80000000

// Current state (duty stored as x1000: 0~100000 = 0.000%~100.000%)
static struct {
    bool light_enabled;
    uint32_t light_freq;
    uint32_t light_duty_x1000;   // actual quantized value
    bool sound_enabled;
    uint32_t sound_freq;
    uint32_t sound_duty_x1000;   // actual quantized value
} state = {0};

// Current resolution per timer
static uint8_t light_resolution = 10;
static uint8_t sound_resolution = 10;

/**
 * @brief Calculate best resolution for a given frequency
 * Uses highest bit depth that supports the frequency
 */
static uint8_t calc_best_resolution(uint32_t freq_hz)
{
    if (freq_hz == 0) return 10;
    
    for (uint8_t res = 13; res >= 8; res--) {
        if ((APB_CLK_FREQ_HZ >> res) >= freq_hz) {
            return res;
        }
    }
    return 8;
}

/**
 * @brief Configure LEDC timer with dynamic resolution
 */
static esp_err_t configure_timer(ledc_timer_t timer_num, uint32_t freq_hz)
{
    uint8_t resolution = calc_best_resolution(freq_hz);
    
    if (timer_num == LEDC_TIMER_LIGHT) {
        light_resolution = resolution;
    } else {
        sound_resolution = resolution;
    }
    
    ESP_LOGD(TAG, "Timer %d: freq=%luHz, resolution=%d-bit",
              timer_num, freq_hz, resolution);
    
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_SPEED_MODE,
        .timer_num = timer_num,
        .duty_resolution = resolution,
        .freq_hz = freq_hz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    
    return ledc_timer_config(&timer_cfg);
}

/**
 * @brief Set duty and quantize back to x1000
 * 
 * Forward:  ledc_duty = (max_duty * duty_x1000) / 100000
 * Reverse:  actual_x1000 = (ledc_duty * 100000 + max_duty/2) / max_duty  (rounded)
 */
static uint32_t set_and_quantize(ledc_channel_t channel, uint32_t duty_x1000, uint8_t resolution)
{
    uint32_t max_duty = (1 << resolution) - 1;
    
    // Clamp
    if (duty_x1000 > 100000) duty_x1000 = 100000;
    
    // Forward: convert to LEDC duty
    uint32_t ledc_duty = (max_duty * duty_x1000) / 100000;
    
    // Set LEDC duty
    ledc_set_duty(LEDC_SPEED_MODE, channel, ledc_duty);
    ledc_update_duty(LEDC_SPEED_MODE, channel);
    
    // Reverse: quantize back to x1000 (rounded)
    uint32_t actual_x1000 = (uint32_t)(((uint64_t)ledc_duty * 100000 + max_duty / 2) / max_duty);
    
    // Clamp result
    if (actual_x1000 > 100000) actual_x1000 = 100000;
    
    ESP_LOGD(TAG, "duty_x1000=%lu -> ledc_duty=%lu/%u -> actual_x1000=%lu (res=%d-bit)",
             duty_x1000, ledc_duty, max_duty, actual_x1000, resolution);
    
    return actual_x1000;
}

esp_err_t pwm_driver_init(void)
{
    ESP_LOGI(TAG, "Initializing PWM driver (LEDC, x1000 precision)");
    
    if (pwm_mutex == NULL) {
        pwm_mutex = xSemaphoreCreateMutex();
    }
    
    esp_err_t ret;
    
    // Configure light channel (IO3)
    ledc_channel_config_t light_cfg = {
        .gpio_num = GPIO_LIGHT_PWM,
        .speed_mode = LEDC_SPEED_MODE,
        .channel = LEDC_CHANNEL_LIGHT,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_LIGHT,
        .duty = 0,
        .hpoint = 0,
    };
    ret = ledc_channel_config(&light_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure light channel: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Configure sound channel (IO1)
    ledc_channel_config_t sound_cfg = {
        .gpio_num = GPIO_SOUND_PWM,
        .speed_mode = LEDC_SPEED_MODE,
        .channel = LEDC_CHANNEL_SOUND,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_SOUND,
        .duty = 0,
        .hpoint = 0,
    };
    ret = ledc_channel_config(&sound_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure sound channel: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize timers with default frequency
    configure_timer(LEDC_TIMER_LIGHT, 1000);
    configure_timer(LEDC_TIMER_SOUND, 1000);
    
    // Configure sound enable GPIO (IO6)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_SOUND_EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(GPIO_SOUND_EN, 0);
    
    ESP_LOGI(TAG, "PWM driver initialized");
    return ESP_OK;
}

esp_err_t pwm_set_light(bool enable, uint32_t freq_hz, uint32_t duty_x1000)
{
    uint32_t duty_percent = duty_x1000 / 1000;
    uint32_t duty_tenth = (duty_x1000 % 1000) / 100;
    ESP_LOGI(TAG, "Light: en=%d, freq=%luHz, duty=%lu.%lu%%",
             enable, freq_hz, duty_percent, duty_tenth);
    
    // Clamp values
    if (freq_hz > FREQ_MAX) freq_hz = FREQ_MAX;
    if (duty_x1000 > DUTY_X1000_LIGHT_MAX) duty_x1000 = DUTY_X1000_LIGHT_MAX;
    
    PWM_LOCK();
    state.light_enabled = enable;
    state.light_freq = freq_hz;
    
    esp_err_t ret = ESP_OK;
    
    if (enable && freq_hz > 0 && duty_x1000 > 0) {
        // Reconfigure timer for new frequency
        ret = configure_timer(LEDC_TIMER_LIGHT, freq_hz);
        if (ret != ESP_OK) {
            PWM_UNLOCK();
            return ret;
        }
        
        // Set duty and quantize back to actual value
        state.light_duty_x1000 = set_and_quantize(LEDC_CHANNEL_LIGHT, duty_x1000, light_resolution);
    } else {
        ledc_set_duty(LEDC_SPEED_MODE, LEDC_CHANNEL_LIGHT, 0);
        ledc_update_duty(LEDC_SPEED_MODE, LEDC_CHANNEL_LIGHT);
        state.light_duty_x1000 = 0;
    }
    
    PWM_UNLOCK();
    return ret;
}

esp_err_t pwm_set_sound(bool enable, uint32_t freq_hz, uint32_t duty_x1000)
{
    uint32_t duty_percent = duty_x1000 / 1000;
    uint32_t duty_tenth = (duty_x1000 % 1000) / 100;
    ESP_LOGI(TAG, "Sound: en=%d, freq=%luHz, duty=%lu.%lu%%",
             enable, freq_hz, duty_percent, duty_tenth);
    
    // Clamp values
    if (freq_hz > FREQ_MAX) freq_hz = FREQ_MAX;
    if (duty_x1000 < DUTY_X1000_SOUND_MIN) duty_x1000 = DUTY_X1000_SOUND_MIN;
    if (duty_x1000 > DUTY_X1000_SOUND_MAX) duty_x1000 = DUTY_X1000_SOUND_MAX;
    
    PWM_LOCK();
    state.sound_enabled = enable;
    state.sound_freq = freq_hz;
    
    // Control sound enable GPIO
    gpio_set_level(GPIO_SOUND_EN, enable ? 1 : 0);
    
    esp_err_t ret = ESP_OK;
    
    if (enable && freq_hz > 0) {
        ret = configure_timer(LEDC_TIMER_SOUND, freq_hz);
        if (ret != ESP_OK) {
            PWM_UNLOCK();
            return ret;
        }
        
        state.sound_duty_x1000 = set_and_quantize(LEDC_CHANNEL_SOUND, duty_x1000, sound_resolution);
    } else {
        ledc_set_duty(LEDC_SPEED_MODE, LEDC_CHANNEL_SOUND, 0);
        ledc_update_duty(LEDC_SPEED_MODE, LEDC_CHANNEL_SOUND);
        state.sound_duty_x1000 = 0;
    }
    
    PWM_UNLOCK();
    return ret;
}

// Getters - thread-safe
bool pwm_get_light_enabled(void) {
    PWM_LOCK();
    bool val = state.light_enabled;
    PWM_UNLOCK();
    return val;
}

uint32_t pwm_get_light_freq(void) {
    PWM_LOCK();
    uint32_t val = state.light_freq;
    PWM_UNLOCK();
    return val;
}

uint32_t pwm_get_light_duty_x1000(void) {
    PWM_LOCK();
    uint32_t val = state.light_duty_x1000;
    PWM_UNLOCK();
    return val;
}

bool pwm_get_sound_enabled(void) {
    PWM_LOCK();
    bool val = state.sound_enabled;
    PWM_UNLOCK();
    return val;
}

uint32_t pwm_get_sound_freq(void) {
    PWM_LOCK();
    uint32_t val = state.sound_freq;
    PWM_UNLOCK();
    return val;
}

uint32_t pwm_get_sound_duty_x1000(void) {
    PWM_LOCK();
    uint32_t val = state.sound_duty_x1000;
    PWM_UNLOCK();
    return val;
}

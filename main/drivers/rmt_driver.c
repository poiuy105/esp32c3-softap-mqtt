#include "rmt_driver.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "PWM";

// Mutex for thread-safe access
static SemaphoreHandle_t pwm_mutex = NULL;

// Helper macros for mutex lock/unlock
#define PWM_LOCK()   do { if (pwm_mutex) xSemaphoreTake(pwm_mutex, portMAX_DELAY); } while(0)
#define PWM_UNLOCK() do { if (pwm_mutex) xSemaphoreGive(pwm_mutex); } while(0)

// RMT channel handles
static rmt_channel_handle_t light_chan = NULL;
static rmt_channel_handle_t sound_chan = NULL;

// Copy encoder for simple symbol transmission
static rmt_encoder_handle_t copy_encoder = NULL;

// Current state
static struct {
    bool light_enabled;
    uint32_t light_freq;
    uint8_t light_duty;
    bool sound_enabled;
    uint32_t sound_freq;
    uint8_t sound_duty;
} state = {0};

// PWM symbol buffer (2 symbols for one PWM period)
static rmt_symbol_word_t pwm_symbols[2];

esp_err_t rmt_driver_init(void)
{
    ESP_LOGI(TAG, "Initializing PWM driver (RMT)");
    
    // Create mutex
    if (pwm_mutex == NULL) {
        pwm_mutex = xSemaphoreCreateMutex();
    }
    
    esp_err_t ret;
    
    // Configure light PWM channel (IO3)
    rmt_tx_channel_config_t light_cfg = {
        .gpio_num = GPIO_LIGHT_PWM,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000,  // 1MHz = 1us resolution
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .flags.invert_out = false,
        .flags.with_dma = false,
    };
    ret = rmt_new_tx_channel(&light_cfg, &light_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create light channel: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Configure sound PWM channel (IO1)
    rmt_tx_channel_config_t sound_cfg = {
        .gpio_num = GPIO_SOUND_PWM,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000,  // 1MHz = 1us resolution
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .flags.invert_out = false,
        .flags.with_dma = false,
    };
    ret = rmt_new_tx_channel(&sound_cfg, &sound_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create sound channel: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Create copy encoder for symbol transmission
    rmt_copy_encoder_config_t enc_cfg = {};
    ret = rmt_new_copy_encoder(&enc_cfg, &copy_encoder);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create encoder: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Enable channels
    rmt_enable(light_chan);
    rmt_enable(sound_chan);
    
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

// Generate and transmit PWM signal
static esp_err_t transmit_pwm(rmt_channel_handle_t chan, uint32_t freq_hz, uint8_t duty_percent)
{
    if (freq_hz == 0 || duty_percent == 0) {
        // Stop output
        rmt_disable(chan);
        rmt_enable(chan);
        return ESP_OK;
    }
    
    // Calculate timing: resolution = 1MHz = 1us per tick
    // Period = 1/freq seconds = 1000000/freq microseconds
    uint32_t period_us = 1000000 / freq_hz;
    uint32_t high_us = (period_us * duty_percent) / 100;
    uint32_t low_us = period_us - high_us;
    
    // Build PWM symbol: high phase + low phase
    // RMT symbol format: [level0, duration0, level1, duration1]
    pwm_symbols[0] = (rmt_symbol_word_t) {
        .level0 = 1,
        .duration0 = high_us,
        .level1 = 0,
        .duration1 = low_us,
    };
    // End marker (will loop back to symbol[0])
    pwm_symbols[1] = (rmt_symbol_word_t) {
        .level0 = 0,
        .duration0 = 0,
        .level1 = 0,
        .duration1 = 0,
    };
    
    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,  // Infinite loop
        .flags.eot_level = 0,
    };
    
    return rmt_transmit(chan, copy_encoder, pwm_symbols, sizeof(pwm_symbols), &tx_cfg);
}

esp_err_t rmt_set_light(bool enable, uint32_t freq_hz, uint8_t duty_percent)
{
    ESP_LOGI(TAG, "Light: en=%d, freq=%luHz, duty=%d%%", enable, freq_hz, duty_percent);
    
    // Clamp values
    if (freq_hz > FREQ_MAX) freq_hz = FREQ_MAX;
    if (duty_percent > DUTY_LIGHT_MAX) duty_percent = DUTY_LIGHT_MAX;
    
    PWM_LOCK();
    state.light_enabled = enable;
    state.light_freq = freq_hz;
    state.light_duty = duty_percent;
    
    esp_err_t ret;
    if (enable && freq_hz > 0 && duty_percent > 0) {
        ret = transmit_pwm(light_chan, freq_hz, duty_percent);
    } else {
        rmt_disable(light_chan);
        rmt_enable(light_chan);
        ret = ESP_OK;
    }
    PWM_UNLOCK();
    
    return ret;
}

esp_err_t rmt_set_sound(bool enable, uint32_t freq_hz, uint8_t duty_percent)
{
    ESP_LOGI(TAG, "Sound: en=%d, freq=%luHz, duty=%d%%", enable, freq_hz, duty_percent);
    
    // Clamp values
    if (freq_hz > FREQ_MAX) freq_hz = FREQ_MAX;
    if (duty_percent < DUTY_SOUND_MIN) duty_percent = DUTY_SOUND_MIN;
    if (duty_percent > DUTY_SOUND_MAX) duty_percent = DUTY_SOUND_MAX;
    
    PWM_LOCK();
    state.sound_enabled = enable;
    state.sound_freq = freq_hz;
    state.sound_duty = duty_percent;
    
    // Control sound enable GPIO
    gpio_set_level(GPIO_SOUND_EN, enable ? 1 : 0);
    
    esp_err_t ret;
    if (enable && freq_hz > 0) {
        ret = transmit_pwm(sound_chan, freq_hz, duty_percent);
    } else {
        rmt_disable(sound_chan);
        rmt_enable(sound_chan);
        ret = ESP_OK;
    }
    PWM_UNLOCK();
    
    return ret;
}

// Getters - thread-safe
bool rmt_get_light_enabled(void) { 
    PWM_LOCK();
    bool val = state.light_enabled;
    PWM_UNLOCK();
    return val;
}

uint32_t rmt_get_light_freq(void) { 
    PWM_LOCK();
    uint32_t val = state.light_freq;
    PWM_UNLOCK();
    return val;
}

uint8_t rmt_get_light_duty(void) { 
    PWM_LOCK();
    uint8_t val = state.light_duty;
    PWM_UNLOCK();
    return val;
}

bool rmt_get_sound_enabled(void) { 
    PWM_LOCK();
    bool val = state.sound_enabled;
    PWM_UNLOCK();
    return val;
}

uint32_t rmt_get_sound_freq(void) { 
    PWM_LOCK();
    uint32_t val = state.sound_freq;
    PWM_UNLOCK();
    return val;
}

uint8_t rmt_get_sound_duty(void) { 
    PWM_LOCK();
    uint8_t val = state.sound_duty;
    PWM_UNLOCK();
    return val;
}

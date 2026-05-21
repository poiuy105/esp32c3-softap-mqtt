#include "rmt_driver.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "driver/gpio.h"
#include "soc/rmt_struct.h"
#include <math.h>

static const char *TAG = "RMT_DRV";

// RMT channel handles
static rmt_channel_handle_t light_chan = NULL;
static rmt_channel_handle_t sound_chan = NULL;

// Encoder handles
static rmt_encoder_handle_t light_encoder = NULL;
static rmt_encoder_handle_t sound_encoder = NULL;

// Current state
static struct {
    bool light_enabled;
    uint32_t light_freq;
    uint8_t light_duty;
    bool sound_enabled;
    uint32_t sound_freq;
    uint8_t sound_duty;
} state = {0};

// Simple bytes encoder for PWM simulation
static rmt_encoder_handle_t create_bytes_encoder(void)
{
    rmt_bytes_encoder_config_t enc_cfg = {
        .bit0 = {
            .level0 = 1,
            .duration0 = 1,
            .level1 = 0,
            .duration1 = 1,
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = 2,
            .level1 = 0,
            .duration1 = 1,
        },
        .flags.msb_first = 1,
    };
    rmt_encoder_handle_t encoder = NULL;
    rmt_new_bytes_encoder(&enc_cfg, &encoder);
    return encoder;
}

esp_err_t rmt_driver_init(void)
{
    ESP_LOGI(TAG, "Initializing RMT driver");
    
    esp_err_t ret;
    
    // Configure light PWM channel (IO3)
    rmt_tx_channel_config_t light_cfg = {
        .gpio_num = GPIO_LIGHT_PWM,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000,  // 1MHz resolution
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
        .resolution_hz = 1000000,  // 1MHz resolution
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
    
    // Create encoders
    light_encoder = create_bytes_encoder();
    sound_encoder = create_bytes_encoder();
    
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
    gpio_set_level(GPIO_SOUND_EN, 0);  // Default low
    
    ESP_LOGI(TAG, "RMT driver initialized");
    return ESP_OK;
}

static esp_err_t set_pwm_output(rmt_channel_handle_t chan, rmt_encoder_handle_t encoder,
                                 uint32_t freq_hz, uint8_t duty_percent)
{
    if (freq_hz == 0 || duty_percent == 0) {
        // Stop output by sending empty transaction
        rmt_disable(chan);
        rmt_enable(chan);
        return ESP_OK;
    }
    
    // Calculate RMT timing for PWM
    // RMT resolution: 1MHz = 1us per tick
    // Period = 1/freq seconds = 1000000/freq us
    uint32_t period_us = 1000000 / freq_hz;
    uint32_t high_us = (period_us * duty_percent) / 100;
    uint32_t low_us = period_us - high_us;
    
    // Create simple on/off pattern
    rmt_symbol_word_t symbols[2] = {
        {
            .level0 = 1,
            .duration0 = high_us,
            .level1 = 0,
            .duration1 = low_us,
        },
        {
            .level0 = 0,
            .duration0 = 0,
            .level1 = 0,
            .duration1 = 0,
        },
    };
    
    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,  // Infinite loop
        .flags.eot_level = 0,
    };
    
    return rmt_transmit(chan, encoder, symbols, sizeof(symbols), &tx_cfg);
}

esp_err_t rmt_set_light(bool enable, uint32_t freq_hz, uint8_t duty_percent)
{
    ESP_LOGI(TAG, "Set light: enable=%d, freq=%lu, duty=%d", enable, freq_hz, duty_percent);
    
    // Clamp values
    if (freq_hz > FREQ_MAX) freq_hz = FREQ_MAX;
    if (duty_percent > DUTY_LIGHT_MAX) duty_percent = DUTY_LIGHT_MAX;
    
    state.light_enabled = enable;
    state.light_freq = freq_hz;
    state.light_duty = duty_percent;
    
    if (enable && freq_hz > 0 && duty_percent > 0) {
        return set_pwm_output(light_chan, light_encoder, freq_hz, duty_percent);
    } else {
        // Stop output
        rmt_disable(light_chan);
        rmt_enable(light_chan);
    }
    
    return ESP_OK;
}

esp_err_t rmt_set_sound(bool enable, uint32_t freq_hz, uint8_t duty_percent)
{
    ESP_LOGI(TAG, "Set sound: enable=%d, freq=%lu, duty=%d", enable, freq_hz, duty_percent);
    
    // Clamp values
    if (freq_hz > FREQ_MAX) freq_hz = FREQ_MAX;
    if (duty_percent < DUTY_SOUND_MIN) duty_percent = DUTY_SOUND_MIN;
    if (duty_percent > DUTY_SOUND_MAX) duty_percent = DUTY_SOUND_MAX;
    
    state.sound_enabled = enable;
    state.sound_freq = freq_hz;
    state.sound_duty = duty_percent;
    
    // Control sound enable GPIO
    gpio_set_level(GPIO_SOUND_EN, enable ? 1 : 0);
    
    if (enable && freq_hz > 0) {
        return set_pwm_output(sound_chan, sound_encoder, freq_hz, duty_percent);
    } else {
        // Stop output
        rmt_disable(sound_chan);
        rmt_enable(sound_chan);
    }
    
    return ESP_OK;
}

// Getters
bool rmt_get_light_enabled(void) { return state.light_enabled; }
uint32_t rmt_get_light_freq(void) { return state.light_freq; }
uint8_t rmt_get_light_duty(void) { return state.light_duty; }
bool rmt_get_sound_enabled(void) { return state.sound_enabled; }
uint32_t rmt_get_sound_freq(void) { return state.sound_freq; }
uint8_t rmt_get_sound_duty(void) { return state.sound_duty; }

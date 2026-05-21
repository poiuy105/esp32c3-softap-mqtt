#include "gpio_control.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "GPIO_CTRL";

// Current LED state
static bool led_state = false;

esp_err_t gpio_control_init(void)
{
    ESP_LOGI(TAG, "Initializing GPIO control");
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_LED),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LED GPIO: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Default LED off (high level = off, since active low)
    gpio_set_level(GPIO_LED, 1);
    led_state = false;
    
    ESP_LOGI(TAG, "GPIO control initialized");
    return ESP_OK;
}

esp_err_t gpio_set_led(bool on)
{
    // LED is active low: on = GPIO low, off = GPIO high
    int level = on ? 0 : 1;
    esp_err_t ret = gpio_set_level(GPIO_LED, level);
    
    if (ret == ESP_OK) {
        led_state = on;
        ESP_LOGI(TAG, "LED %s (GPIO %d)", on ? "ON" : "OFF", level);
    }
    
    return ret;
}

bool gpio_get_led(void)
{
    return led_state;
}

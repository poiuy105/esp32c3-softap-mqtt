#include "gpio_control.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

static const char *TAG = "LED";

// LED state
static bool led_on = false;
static led_mode_t current_led_mode = LED_MODE_OFF;

// LED blink timer
static TimerHandle_t led_timer = NULL;
static bool led_blink_state = false;

static uint32_t led_mode_to_period_ms(led_mode_t mode)
{
    switch (mode) {
        case LED_MODE_SLOW_BLINK:  return 1000;  // 1s on/off
        case LED_MODE_FAST_BLINK:  return 200;  // 200ms on/off
        case LED_MODE_WARN_BLINK:  return 100;  // 100ms on/off
        default:                    return 0;
    }
}

static void led_timer_callback(TimerHandle_t timer)
{
    led_blink_state = !led_blink_state;
    gpio_set_level(GPIO_LED, led_blink_state ? 0 : 1);  // active low
}

esp_err_t gpio_set_led_mode(led_mode_t mode)
{
    current_led_mode = mode;
    ESP_LOGI(TAG, "LED mode: %d", mode);

    // Stop existing timer
    if (led_timer) {
        xTimerStop(led_timer, 0);
    }

    switch (mode) {
        case LED_MODE_OFF:
            gpio_set_level(GPIO_LED, 1);  // off (high)
            led_on = false;
            break;
        case LED_MODE_ON:
            gpio_set_level(GPIO_LED, 0);  // on (low)
            led_on = true;
            break;
        case LED_MODE_SLOW_BLINK:
        case LED_MODE_FAST_BLINK:
        case LED_MODE_WARN_BLINK: {
            uint32_t period = led_mode_to_period_ms(mode);
            if (led_timer == NULL) {
                led_timer = xTimerCreate("led_blink", pdMS_TO_TICKS(period),
                                          pdTRUE, NULL, led_timer_callback);
            } else {
                xTimerChangePeriod(led_timer, pdMS_TO_TICKS(period), 0);
            }
            led_blink_state = false;
            gpio_set_level(GPIO_LED, 0);  // start with LED on
            xTimerStart(led_timer, 0);
            break;
        }
    }
    return ESP_OK;
}

led_mode_t gpio_get_led_mode(void)
{
    return current_led_mode;
}

esp_err_t gpio_control_init(void)
{
    ESP_LOGI(TAG, "Initializing LED control on GPIO %d", GPIO_LED);

    // Configure GPIO_LED
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

    // Default LED off
    gpio_set_level(GPIO_LED, 1);
    led_on = false;

    // Configure GPIO0 as low output
    gpio_config_t gpio0_conf = {
        .pin_bit_mask = (1ULL << GPIO_LOW_OUT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&gpio0_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO0: %s", esp_err_to_name(ret));
        return ret;
    }
    gpio_set_level(GPIO_LOW_OUT, 0);  // Set low
    ESP_LOGI(TAG, "GPIO0 set to low output");

    ESP_LOGI(TAG, "LED control initialized");
    return ESP_OK;
}

esp_err_t gpio_set_led(bool on)
{
    int level = on ? 0 : 1;
    esp_err_t ret = gpio_set_level(GPIO_LED, level);
    if (ret == ESP_OK) {
        led_on = on;
    }
    return ret;
}

bool gpio_get_led(void)
{
    return led_on;
}

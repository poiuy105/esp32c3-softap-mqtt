#include "button_driver.h"
#include "gpio_control.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

static const char *TAG = "BUTTON";

// Button state
static button_factory_reset_callback_t factory_reset_cb = NULL;
static bool button_pressed = false;
static int64_t button_press_start = 0;
static TimerHandle_t button_timer = NULL;
static bool factory_reset_armed = false;

static void button_timer_callback(TimerHandle_t timer)
{
    bool btn_level = gpio_get_level(GPIO_BTN);

    if (btn_level == 0) {
        // Button still pressed (low)
        int64_t press_duration = (esp_timer_get_time() - button_press_start) / 1000;  // ms

        if (press_duration >= 3000 && !factory_reset_armed) {
            factory_reset_armed = true;
            ESP_LOGW(TAG, "Factory reset armed! Release within 5s to cancel, hold 5s to confirm");
            // Switch LED to warning blink
            gpio_set_led_mode(LED_MODE_WARN_BLINK);
        }

        if (press_duration >= 5000 && factory_reset_armed) {
            // Execute factory reset
            ESP_LOGW(TAG, "Factory reset triggered! (held %lld ms)", press_duration);
            factory_reset_armed = false;
            button_pressed = false;

            // LED solid on for 1s before reset
            gpio_set_led_mode(LED_MODE_ON);

            if (factory_reset_cb) {
                factory_reset_cb();
            }
        }
    } else {
        // Button released
        if (factory_reset_armed) {
            ESP_LOGI(TAG, "Factory reset cancelled (button released)");
            factory_reset_armed = false;
            // Restore previous LED mode - will be set by state machine
        }
        button_pressed = false;
    }
}

static void button_gpio_isr(void *arg)
{
    // Debounce: just record the press, timer will handle it
    if (gpio_get_level(GPIO_BTN) == 0 && !button_pressed) {
        button_pressed = true;
        button_press_start = esp_timer_get_time();
    }
}

esp_err_t button_init(button_factory_reset_callback_t callback)
{
    ESP_LOGI(TAG, "Initializing button on GPIO %d", GPIO_BTN);

    factory_reset_cb = callback;

    // Configure button GPIO as input with pull-up
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_BTN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure button GPIO: %s", esp_err_to_name(ret));
        return ret;
    }

    // Install GPIO ISR service
    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install ISR service: %s", esp_err_to_name(ret));
        return ret;
    }

    // Add ISR handler for button
    ret = gpio_isr_handler_add(GPIO_BTN, button_gpio_isr, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add ISR handler: %s", esp_err_to_name(ret));
        return ret;
    }

    // Create button polling timer (100ms)
    button_timer = xTimerCreate("button_poll", pdMS_TO_TICKS(100),
                                pdTRUE, NULL, button_timer_callback);
    if (button_timer) {
        xTimerStart(button_timer, 0);
    }

    ESP_LOGI(TAG, "Button initialized (long press 5s = factory reset)");
    return ESP_OK;
}

#include "buzzer.h"

#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BUZZER";

static gpio_num_t s_gpio = GPIO_NUM_NC;
static bool s_active_low;
static bool s_initialized;

static void drive(bool on)
{
    gpio_set_level(s_gpio, (on != s_active_low) ? 1 : 0);
}

esp_err_t buzzer_init(int gpio, bool active_low)
{
    if (gpio < 0 || gpio >= GPIO_NUM_MAX || !GPIO_IS_VALID_OUTPUT_GPIO(gpio)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_gpio = (gpio_num_t)gpio;
    s_active_low = active_low;

    gpio_reset_pin(s_gpio);
    drive(false);
    esp_err_t err = gpio_set_direction(s_gpio, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) {
        return err;
    }
    drive(false);
    s_initialized = true;
    ESP_LOGI(TAG, "buzzer GPIO%d active_%s", gpio, active_low ? "low" : "high");
    return ESP_OK;
}

void buzzer_beep(uint32_t on_ms, uint32_t off_ms, uint32_t count)
{
    if (!s_initialized) {
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        drive(true);
        vTaskDelay(pdMS_TO_TICKS(on_ms));
        drive(false);
        if (i + 1 < count) {
            vTaskDelay(pdMS_TO_TICKS(off_ms));
        }
    }
}

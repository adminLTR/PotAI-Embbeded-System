#include "pump.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

static const char *TAG = "PUMP";

static gpio_num_t s_gpio = GPIO_NUM_NC;
static bool s_active_low;
static bool s_initialized;
static volatile bool s_on;
static esp_timer_handle_t s_off_timer;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static void drive(bool on)
{
    const int level = (on != s_active_low) ? 1 : 0;
    gpio_set_level(s_gpio, level);
}

static void off_timer_cb(void *arg)
{
    (void)arg;
    pump_stop();
}

esp_err_t pump_init(int gpio, bool active_low)
{
    if (gpio < 0 || gpio >= GPIO_NUM_MAX || !GPIO_IS_VALID_OUTPUT_GPIO(gpio)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_gpio = (gpio_num_t)gpio;
    s_active_low = active_low;

    if (s_off_timer == NULL) {
        const esp_timer_create_args_t args = {.callback = off_timer_cb, .name = "pump_off"};
        esp_err_t err = esp_timer_create(&args, &s_off_timer);
        if (err != ESP_OK) {
            return err;
        }
    }

    /* Nivel de repouso escrito ANTES de habilitar a saida: sem pulso no rele. */
    gpio_reset_pin(s_gpio);
    drive(false);
    esp_err_t err = gpio_set_direction(s_gpio, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) {
        return err;
    }
    drive(false);

    s_on = false;
    s_initialized = true;
    ESP_LOGI(TAG, "rele GPIO%d active_%s, bomba desligada", gpio, active_low ? "low" : "high");
    return ESP_OK;
}

esp_err_t pump_start(uint32_t duration_ms)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (duration_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ESP_OK;
    portENTER_CRITICAL(&s_lock);
    if (s_on) {
        err = ESP_ERR_INVALID_STATE;
    } else {
        drive(true);
        s_on = true;
    }
    portEXIT_CRITICAL(&s_lock);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_timer_start_once(s_off_timer, (uint64_t)duration_ms * 1000ULL);
    if (err != ESP_OK) {
        pump_stop(); /* sem timer nao ha garantia de desligamento */
        return err;
    }
    ESP_LOGI(TAG, "ON por %lu ms", (unsigned long)duration_ms);
    return ESP_OK;
}

void pump_stop(void)
{
    if (!s_initialized) {
        return;
    }
    if (s_off_timer != NULL) {
        esp_timer_stop(s_off_timer); /* ESP_ERR_INVALID_STATE se nao estava ativo: ignorado */
    }
    bool was_on;
    portENTER_CRITICAL(&s_lock);
    was_on = s_on;
    drive(false);
    s_on = false;
    portEXIT_CRITICAL(&s_lock);
    if (was_on) {
        ESP_LOGI(TAG, "OFF");
    }
}

bool pump_is_on(void)
{
    return s_on;
}

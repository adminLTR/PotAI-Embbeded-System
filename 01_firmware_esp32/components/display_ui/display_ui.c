#include "display_ui.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DISPLAY_UI";

#define UI_POLL_MS 20

static display_ui_config_t s_cfg;
static bool s_started;

void display_ui_format_page(uint8_t page, uint8_t plant_id, const device_snapshot_t *s,
                            char line0[17], char line1[17])
{
    line0[0] = '\0';
    line1[0] = '\0';
    if (s == NULL) {
        return;
    }

    switch (page) {
    case 0:
        if (s->sample_valid) {
            snprintf(line0, 17, "Temp: %.1f C", (double)s->temperature_c);
            snprintf(line1, 17, "Hum.Amb: %.0f %%", (double)s->humidity_pct);
        } else {
            snprintf(line0, 17, "Temp: -- C");
            snprintf(line1, 17, "Hum.Amb: -- %%");
        }
        break;
    case 1:
        if (s->soil_valid) {
            snprintf(line0, 17, "Hum.Suelo: %.0f %%", (double)s->soil_moisture_pct);
        } else {
            snprintf(line0, 17, "Hum.Suelo: -- %%");
        }
        snprintf(line1, 17, "Planta: %u", (unsigned)plant_id);
        break;
    default:
        snprintf(line0, 17, "IA: %.0f ml", (double)s->last_ml_predicted);
        snprintf(line1, 17, "Bomba:%s Rieg:%lu", s->pump_on ? "ON " : "OFF",
                 (unsigned long)s->irrigation_count);
        break;
    }
}

static void render(uint8_t page, char cache[2][17], bool force)
{
    device_snapshot_t snap;
    device_state_copy(&snap);

    char lines[2][17];
    display_ui_format_page(page, s_cfg.plant_id, &snap, lines[0], lines[1]);

    for (uint8_t row = 0; row < 2; row++) {
        if (force || strcmp(lines[row], cache[row]) != 0) {
            if (lcd_i2c_write_line(row, lines[row]) == ESP_OK) {
                memcpy(cache[row], lines[row], 17);
            }
        }
    }
}

static void ui_task(void *arg)
{
    (void)arg;
    uint8_t page = 0;
    char cache[2][17] = {{0}, {0}};
    uint32_t pressed_ms = 0;
    bool latched = false; /* ja contou esta pressao */
    uint64_t last_refresh_us = 0;
    bool force = true;

    while (1) {
        const bool down = gpio_get_level((gpio_num_t)s_cfg.button_gpio) == 0;
        if (down) {
            pressed_ms += UI_POLL_MS;
            if (!latched && pressed_ms >= s_cfg.button_debounce_ms) {
                latched = true;
                page = (uint8_t)((page + 1) % DISPLAY_UI_PAGE_COUNT);
                force = true;
                ESP_LOGI(TAG, "[UI] pagina %u", page);
            }
        } else {
            pressed_ms = 0;
            latched = false;
        }

        const uint64_t now_us = (uint64_t)esp_timer_get_time();
        if (force || (now_us - last_refresh_us) >= (uint64_t)s_cfg.refresh_ms * 1000ULL) {
            render(page, cache, force);
            last_refresh_us = now_us;
            force = false;
        }
        vTaskDelay(pdMS_TO_TICKS(UI_POLL_MS));
    }
}

esp_err_t display_ui_start(const display_ui_config_t *config)
{
    if (config == NULL || config->refresh_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    s_cfg = *config;

    gpio_config_t btn = {
        .pin_bit_mask = 1ULL << s_cfg.button_gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&btn);
    if (err != ESP_OK) {
        return err;
    }

    err = lcd_i2c_init(&s_cfg.lcd);
    if (err != ESP_OK) {
        return err;
    }

    if (xTaskCreate(ui_task, "ui_task", 3072, NULL, 3, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    ESP_LOGI(TAG, "UI ativa: botao GPIO%d, %u paginas", s_cfg.button_gpio, DISPLAY_UI_PAGE_COUNT);
    return ESP_OK;
}

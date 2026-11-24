/* Misma secuencia que el driver de la disciplina que ya leia el modulo.
 * La seccion critica cubre solo la respuesta: con Wi-Fi activo una
 * interrupcion dura mas que el pulso de 80 us y la lectura sale por timeout. */
#include "dht11.h"

#include <string.h>
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#define DHT11_TIMEOUT_US 120

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static bool wait_level(gpio_num_t gpio, int level, int timeout_us)
{
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(gpio) != level) {
        if ((esp_timer_get_time() - start) > timeout_us) {
            return false;
        }
    }
    return true;
}

esp_err_t dht11_read(gpio_num_t gpio, dht11_reading_t *reading)
{
    if (reading == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(reading, 0, sizeof(*reading));

    if (gpio < 0 || gpio > 39) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t data[5] = {0};

    gpio_reset_pin(gpio);
    gpio_set_direction(gpio, GPIO_MODE_OUTPUT_OD);
    gpio_set_pull_mode(gpio, GPIO_PULLUP_ONLY);

    gpio_set_level(gpio, 0);
    esp_rom_delay_us(20000);
    gpio_set_level(gpio, 1);
    esp_rom_delay_us(40);

    gpio_set_direction(gpio, GPIO_MODE_INPUT);
    gpio_set_pull_mode(gpio, GPIO_PULLUP_ONLY);

    taskENTER_CRITICAL(&s_mux);

    bool timed_out = !wait_level(gpio, 0, DHT11_TIMEOUT_US) ||
                     !wait_level(gpio, 1, DHT11_TIMEOUT_US) ||
                     !wait_level(gpio, 0, DHT11_TIMEOUT_US);

    for (int bit = 0; bit < 40 && !timed_out; ++bit) {
        if (!wait_level(gpio, 1, DHT11_TIMEOUT_US)) {
            timed_out = true;
            break;
        }

        int64_t high_start = esp_timer_get_time();
        if (!wait_level(gpio, 0, DHT11_TIMEOUT_US)) {
            timed_out = true;
            break;
        }
        int high_us = (int)(esp_timer_get_time() - high_start);

        data[bit / 8] <<= 1;
        if (high_us > 50) {
            data[bit / 8] |= 1;
        }
    }

    taskEXIT_CRITICAL(&s_mux);

    if (timed_out) {
        return ESP_ERR_TIMEOUT;
    }

    uint8_t checksum = (uint8_t)(data[0] + data[1] + data[2] + data[3]);
    reading->checksum_ok = (checksum == data[4]);
    if (!reading->checksum_ok) {
        return ESP_ERR_INVALID_CRC;
    }

    reading->humidity_pct = data[0];
    reading->temperature_c = data[2];
    return ESP_OK;
}

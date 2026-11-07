#include "dht11.h"

#include <string.h>
#include "esp_rom_sys.h"
#include "esp_timer.h"

static int wait_for_level(gpio_num_t gpio, int level, uint32_t timeout_us)
{
    int64_t start = esp_timer_get_time();

    while (gpio_get_level(gpio) != level) {
        if ((uint32_t)(esp_timer_get_time() - start) > timeout_us) {
            return -1;
        }
    }

    return (int)(esp_timer_get_time() - start);
}

esp_err_t dht11_read(gpio_num_t gpio, dht11_reading_t *reading)
{
    if (reading == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(reading, 0, sizeof(*reading));

    /*
     * Mantem a sequencia de GPIO ja usada nas aulas anteriores:
     * durante a janela critica do protocolo DHT11 evitamos gpio_config(),
     * que reconfigura o pino por completo e pode introduzir atraso excessivo.
     */
    ESP_ERROR_CHECK(gpio_set_direction(gpio, GPIO_MODE_OUTPUT_OD));
    ESP_ERROR_CHECK(gpio_set_pull_mode(gpio, GPIO_PULLUP_ONLY));

    gpio_set_level(gpio, 0);
    esp_rom_delay_us(20000);
    gpio_set_level(gpio, 1);
    esp_rom_delay_us(40);

    ESP_ERROR_CHECK(gpio_set_direction(gpio, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_pull_mode(gpio, GPIO_PULLUP_ONLY));

    if (wait_for_level(gpio, 0, 100) < 0) return ESP_ERR_TIMEOUT;
    if (wait_for_level(gpio, 1, 100) < 0) return ESP_ERR_TIMEOUT;
    if (wait_for_level(gpio, 0, 100) < 0) return ESP_ERR_TIMEOUT;

    uint8_t data[5] = {0};
    for (int bit = 0; bit < 40; ++bit) {
        if (wait_for_level(gpio, 1, 100) < 0) return ESP_ERR_TIMEOUT;
        int high_us = wait_for_level(gpio, 0, 120);
        if (high_us < 0) return ESP_ERR_TIMEOUT;

        data[bit / 8] <<= 1;
        if (high_us > 50) {
            data[bit / 8] |= 1;
        }
    }

    uint8_t checksum = data[0] + data[1] + data[2] + data[3];
    reading->checksum_ok = (checksum == data[4]);
    reading->humidity_pct = data[0];
    reading->temperature_c = data[2];

    return reading->checksum_ok ? ESP_OK : ESP_ERR_INVALID_CRC;
}

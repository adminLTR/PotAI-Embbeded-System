#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int temperature_c;
    int humidity_pct;
    bool checksum_ok;
} dht11_reading_t;

esp_err_t dht11_read(gpio_num_t gpio, dht11_reading_t *reading);

#ifdef __cplusplus
}
#endif

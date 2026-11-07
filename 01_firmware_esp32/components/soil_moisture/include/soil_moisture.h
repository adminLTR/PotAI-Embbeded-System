#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int adc_gpio;   /* ADC1: GPIO32..39 na ESP32 (ADC2 conflita com Wi-Fi) */
    int raw_dry;    /* leitura ADC (12 bits) com o sensor no ar/solo seco  = 0 % */
    int raw_wet;    /* leitura ADC (12 bits) com o sensor na agua          = 100 % */
    int samples;    /* amostras por leitura (media) */
} soil_moisture_config_t;

esp_err_t soil_moisture_init(const soil_moisture_config_t *config);

/*
 * Le o sensor. out_pct fica em 0..100 (limitado). Retorna ESP_ERR_INVALID_RESPONSE
 * se o valor bruto indicar sensor desconectado/curto-circuito.
 * out_raw pode ser NULL.
 */
esp_err_t soil_moisture_read(float *out_pct, int *out_raw);

/* Conversao pura (testavel): bruto ADC -> percentual 0..100. */
float soil_moisture_raw_to_pct(int raw, int raw_dry, int raw_wet);

#ifdef __cplusplus
}
#endif

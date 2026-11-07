#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Inferencia edge de un Random Forest (regresion) comprimido.
 *
 * El modelo vive en flash (EMBED_FILES de model/rf_model.bin, generado por
 * 03_model_ML/export_model_esp32.py). Se lee "in place": no se copia a RAM.
 *
 * Entradas : id de planta (1..8), humedad del suelo (%), temperatura (C)
 * Salida   : mililitros de riego recomendados
 */

/* Valida cabecera, tamano y CRC16 del binario embebido. */
esp_err_t ml_model_init(void);
bool ml_model_is_ready(void);

esp_err_t ml_model_predict_ml(uint8_t plant_id,
                              float soil_moisture_pct,
                              float temperature_c,
                              float *out_ml);

uint16_t ml_model_tree_count(void);
size_t ml_model_size_bytes(void);

#ifdef __cplusplus
}
#endif

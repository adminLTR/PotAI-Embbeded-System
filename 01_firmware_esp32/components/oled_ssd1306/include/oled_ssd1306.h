#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OLED_COLS 16
#define OLED_ROWS 2

typedef struct {
    int sda_gpio;
    int scl_gpio;
    uint8_t address;   /* 0x3C en casi todos los modulos de 0.96" */
    uint32_t freq_hz;  /* 400000 recomendado */
} oled_config_t;

/*
 * OLED SSD1306 128x64 por I2C.
 * La interfaz imita al LCD de 2 lineas: cada linea admite 16 caracteres.
 * No es thread-safe: usarla desde una sola task.
 * Si el modulo no responde, devuelve error y el resto del firmware sigue.
 */
esp_err_t oled_init(const oled_config_t *config);

/* Escribe `text` en la linea (0..1), completando con espacios hasta 16 columnas. */
esp_err_t oled_write_line(uint8_t row, const char *text);

#ifdef __cplusplus
}
#endif

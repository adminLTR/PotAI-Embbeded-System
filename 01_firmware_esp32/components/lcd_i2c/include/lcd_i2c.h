#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LCD_I2C_COLS 16
#define LCD_I2C_ROWS 2

typedef struct {
    int sda_gpio;
    int scl_gpio;
    uint8_t address;      /* 0x27 (PCF8574) ou 0x3F (PCF8574A) */
    uint32_t freq_hz;     /* 100000 recomendado */
    bool backlight;
} lcd_i2c_config_t;

/*
 * LCD HD44780 16x2 com backpack PCF8574 (P0=RS P1=RW P2=E P3=BL P4..P7=D4..D7).
 * Nao e thread-safe: use a partir de uma unica task.
 */
esp_err_t lcd_i2c_init(const lcd_i2c_config_t *config);
esp_err_t lcd_i2c_clear(void);
esp_err_t lcd_i2c_set_backlight(bool on);

/* Escreve `text` na linha (0..1), completando com espacos ate 16 colunas. */
esp_err_t lcd_i2c_write_line(uint8_t row, const char *text);

#ifdef __cplusplus
}
#endif

#pragma once

#include <stdint.h>
#include "device_state.h"
#include "esp_err.h"
#include "oled_ssd1306.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    oled_config_t oled;
    int button_gpio;           /* boton entre el GPIO y GND (pull-up interno) */
    uint32_t button_debounce_ms;
    uint32_t refresh_ms;       /* periodo de actualizacion del OLED */
    uint8_t plant_id;          /* solo para mostrar */
} display_ui_config_t;

#define DISPLAY_UI_PAGE_COUNT 3

/*
 * Inicializa el OLED 128x64 y crea la task de interfaz. Cada pulsacion avanza
 * a la pagina siguiente (circular):
 *   0: temperatura ambiente + humedad ambiente
 *   1: humedad del suelo + planta
 *   2: ultima decision de la IA (ml) + estado de la bomba
 * Si el OLED no responde, devuelve error y el resto del firmware sigue.
 */
esp_err_t display_ui_start(const display_ui_config_t *config);

/* Formata a pagina `page` (funcao pura, 2 linhas de ate 16 caracteres + '\0'). */
void display_ui_format_page(uint8_t page, uint8_t plant_id, const device_snapshot_t *snapshot,
                            char line0[17], char line1[17]);

#ifdef __cplusplus
}
#endif

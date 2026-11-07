#pragma once

#include <stdint.h>
#include "device_state.h"
#include "esp_err.h"
#include "lcd_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lcd_i2c_config_t lcd;
    int button_gpio;           /* botao entre o GPIO e GND (pull-up interno) */
    uint32_t button_debounce_ms;
    uint32_t refresh_ms;       /* periodo de atualizacao do LCD */
    uint8_t plant_id;          /* so para exibir */
} display_ui_config_t;

#define DISPLAY_UI_PAGE_COUNT 3

/*
 * Inicializa o LCD 16x2 e cria a task de interface. Cada pressao do botao avanca
 * para a pagina seguinte (circular):
 *   0: temperatura ambiente + umidade ambiente
 *   1: umidade do solo + planta
 *   2: ultima decisao da IA (ml) + estado da bomba
 * Se o LCD nao responder, retorna erro e o resto do firmware segue normalmente.
 */
esp_err_t display_ui_start(const display_ui_config_t *config);

/* Formata a pagina `page` (funcao pura, 2 linhas de ate 16 caracteres + '\0'). */
void display_ui_format_page(uint8_t page, uint8_t plant_id, const device_snapshot_t *snapshot,
                            char line0[17], char line1[17]);

#ifdef __cplusplus
}
#endif

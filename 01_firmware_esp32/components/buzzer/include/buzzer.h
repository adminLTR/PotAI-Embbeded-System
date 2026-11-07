#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Buzzer ATIVO (oscilador interno) ligado a um GPIO. */
esp_err_t buzzer_init(int gpio, bool active_low);

/* Emite `count` bips (bloqueante, usa vTaskDelay). Ignorado se nao inicializado. */
void buzzer_beep(uint32_t on_ms, uint32_t off_ms, uint32_t count);

#ifdef __cplusplus
}
#endif

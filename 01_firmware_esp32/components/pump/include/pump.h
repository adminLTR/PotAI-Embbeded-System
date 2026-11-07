#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bomba de agua acionada por rele. O pino inicia sempre com a bomba DESLIGADA. */
esp_err_t pump_init(int gpio, bool active_low);

/*
 * Liga a bomba por duration_ms. O desligamento e garantido por um esp_timer
 * (independente da task chamadora), servindo de protecao contra travamentos.
 */
esp_err_t pump_start(uint32_t duration_ms);

/* Desliga imediatamente (idempotente). */
void pump_stop(void);

bool pump_is_on(void);

#ifdef __cplusplus
}
#endif

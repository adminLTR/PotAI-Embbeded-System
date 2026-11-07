#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*irrigation_event_cb_t)(const char *event_name, void *ctx);

typedef struct {
    uint8_t plant_id;             /* 1..8: entrada "localname" do modelo */
    uint32_t check_period_ms;     /* periodo de decisao da IA */
    uint32_t cooldown_s;          /* minimo entre duas irrigacoes (o solo demora a absorver) */
    uint32_t min_ml;              /* abaixo disso nao liga a bomba */
    uint32_t flow_ml_per_min;     /* vazao medida da bomba */
    uint32_t max_on_ms;           /* limite duro de uma irrigacao */
    float soil_max_pct;           /* acima disso nunca irriga (seguranca) */
    uint32_t max_sample_age_ms;   /* leituras mais velhas que isso sao descartadas */
    uint32_t beep_count;          /* bips ao ligar a bomba (0 = sem buzzer) */
    irrigation_event_cb_t event_cb; /* "pump_on" / "pump_off" (pode ser NULL) */
    void *event_ctx;
} irrigation_config_t;

/* Cria a task de irrigacao automatica. Requer ml_model_init() e pump_init() previos. */
esp_err_t irrigation_start(const irrigation_config_t *config);

/* Duracao da bomba para entregar `ml` (limitada por max_on_ms). Funcao pura. */
uint32_t irrigation_duration_ms(float ml, uint32_t flow_ml_per_min, uint32_t max_on_ms);

#ifdef __cplusplus
}
#endif

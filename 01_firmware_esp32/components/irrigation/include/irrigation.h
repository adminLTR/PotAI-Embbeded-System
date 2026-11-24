#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*irrigation_event_cb_t)(const char *event_name, void *ctx);

/* Resumo de uma irrigacao concluida (para enviar a nuvem/app). */
typedef struct {
    uint32_t seq;               /* numero da irrigacao desde o boot */
    uint8_t plant_id;
    float soil_moisture_pct;    /* umidade do solo ANTES de regar */
    float temperature_c;
    float ml_predicted;         /* decisao da IA */
    float ml_delivered;         /* estimado pelo tempo de bomba x vazao */
    uint32_t duration_ms;
    uint64_t uptime_ms;         /* instante do fim da irrigacao */
} irrigation_record_t;

typedef void (*irrigation_record_cb_t)(const irrigation_record_t *record, void *ctx);

typedef struct {
    uint8_t plant_id;             /* 1..8: entrada "localname" do modelo */
    bool automatic;               /* false: no riega sola; el pedido manual sigue activo */
    uint32_t check_period_ms;     /* periodo de decisao da IA */
    uint32_t cooldown_s;          /* 0 = el siguiente ciclo de la IA puede regar otra vez */
    uint32_t min_ml;              /* abaixo disso nao liga a bomba */
    uint32_t flow_ml_per_min;     /* vazao medida da bomba */
    uint32_t max_on_ms;           /* limite duro de uma irrigacao */
    float soil_max_pct;           /* acima disso nunca irriga (seguranca) */
    uint32_t max_sample_age_ms;   /* leituras mais velhas que isso sao descartadas */
    uint32_t beep_count;          /* bips ao ligar a bomba (0 = sem buzzer) */
    irrigation_event_cb_t event_cb; /* "pump_on" / "pump_off" (pode ser NULL) */
    void *event_ctx;
    irrigation_record_cb_t record_cb; /* ao concluir cada irrigacao (pode ser NULL) */
    void *record_ctx;
} irrigation_config_t;

/* Cria a task de irrigacao. Se automatic=true, requer ml_model_init() previo. pump_init() sempre. */
esp_err_t irrigation_start(const irrigation_config_t *config);

/*
 * Pide un riego ya, sin las trabas de la IA (suelo, volumen minimo, cooldown).
 * La duracion sigue limitada por max_on_ms. ml debe estar entre 10 y 500.
 * Despierta la task; no bloquea hasta que la bomba termine.
 */
esp_err_t irrigation_request_manual(uint16_t ml);

/* Duracao da bomba para entregar `ml` (limitada por max_on_ms). Funcao pura. */
uint32_t irrigation_duration_ms(float ml, uint32_t flow_ml_per_min, uint32_t max_on_ms);

#ifdef __cplusplus
}
#endif

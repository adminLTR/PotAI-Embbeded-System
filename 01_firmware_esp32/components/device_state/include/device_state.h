#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char device_id[33];

    /* Ultima amostra VALIDA disponivel. */
    float temperature_c;
    float humidity_pct;
    bool sample_valid;
    uint64_t sample_uptime_ms;

    /* Estado local atual. */
    bool door_open;
    bool led_on;
    uint32_t open_event_count;
    uint32_t raw_isr_count;

    /* Ultima leitura VALIDA da umidade do solo. */
    float soil_moisture_pct;
    int soil_raw;
    bool soil_valid;
    uint64_t soil_sample_uptime_ms;

    /* Irrigacao automatica (decisao da IA na borda). */
    bool pump_on;
    float last_ml_predicted;
    float last_ml_delivered;
    uint32_t irrigation_count;

    /* Instante, em uptime do ESP32, da ultima atualizacao do snapshot por qualquer subsistema. */
    uint64_t last_update_uptime_ms;
} device_snapshot_t;

void device_state_init(const char *device_id);

/*
 * Registra uma tentativa de aquisicao. Se valid=false, a ultima amostra valida
 * permanece disponivel; apenas last_update_uptime_ms e atualizado. Assim,
 * /api/v1/telemetry/latest representa a ultima amostra valida, e nao a ultima
 * tentativa de leitura do sensor.
 */
void device_state_update_telemetry(
    float temperature_c,
    float humidity_pct,
    bool valid,
    uint64_t update_uptime_ms);

void device_state_update_local(
    bool door_open,
    bool led_on,
    uint32_t open_event_count,
    uint32_t raw_isr_count,
    uint64_t update_uptime_ms);

void device_state_update_led(bool led_on, uint64_t update_uptime_ms);

/* Como device_state_update_telemetry: se valid=false, mantem a ultima leitura valida. */
void device_state_update_soil(
    float soil_moisture_pct,
    int soil_raw,
    bool valid,
    uint64_t update_uptime_ms);

void device_state_update_irrigation(
    bool pump_on,
    float last_ml_predicted,
    float last_ml_delivered,
    uint32_t irrigation_count,
    uint64_t update_uptime_ms);

void device_state_copy(device_snapshot_t *out);

#ifdef __cplusplus
}
#endif

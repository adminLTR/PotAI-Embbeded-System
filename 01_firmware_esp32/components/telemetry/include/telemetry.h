#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct {
    const char *device_id;
    uint32_t seq;
    float temperature_c;
    float humidity_pct;
    bool valid;
    uint64_t sample_uptime_ms;
    /* PotAI: campos adicionais (a nuvem os trata como opcionais). */
    float soil_moisture_pct;
    bool soil_valid;
    uint8_t plant_id;
    float ml_predicted;
    bool pump_on;
    uint32_t irrigation_count;
} telemetry_data_t;
esp_err_t telemetry_encode_json(const telemetry_data_t *data, char *buffer, size_t buffer_len);

/* Registro de uma irrigacao concluida (topico psi3542/<thing>/irrigation). */
typedef struct {
    const char *device_id;
    uint32_t seq;
    uint8_t plant_id;
    float soil_moisture_pct;
    float temperature_c;
    float ml_predicted;
    float ml_delivered;
    uint32_t duration_ms;
    uint64_t pump_uptime_ms;
} irrigation_report_data_t;
esp_err_t irrigation_report_encode_json(const irrigation_report_data_t *data, char *buffer, size_t buffer_len);

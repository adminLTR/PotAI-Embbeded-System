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
} telemetry_data_t;
esp_err_t telemetry_encode_json(const telemetry_data_t *data, char *buffer, size_t buffer_len);

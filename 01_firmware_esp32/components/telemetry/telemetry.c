#include "telemetry.h"
#include <inttypes.h>
#include <stdio.h>
esp_err_t telemetry_encode_json(const telemetry_data_t *data, char *buffer, size_t len)
{
    if (data == NULL || buffer == NULL || len == 0 || data->device_id == NULL) return ESP_ERR_INVALID_ARG;
    int n = snprintf(buffer, len,
        "{\"device_id\":\"%s\",\"seq\":%" PRIu32 ",\"temperature_c\":%.1f,\"humidity_pct\":%.1f,\"valid\":%s,\"sample_uptime_ms\":%" PRIu64 "}",
        data->device_id, data->seq, (double)data->temperature_c, (double)data->humidity_pct,
        data->valid ? "true" : "false", data->sample_uptime_ms);
    return (n < 0 || (size_t)n >= len) ? ESP_ERR_NO_MEM : ESP_OK;
}

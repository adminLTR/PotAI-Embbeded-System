#include "telemetry.h"
#include <inttypes.h>
#include <stdio.h>
esp_err_t telemetry_encode_json(const telemetry_data_t *data, char *buffer, size_t len)
{
    if (data == NULL || buffer == NULL || len == 0 || data->device_id == NULL) return ESP_ERR_INVALID_ARG;
    int n = snprintf(buffer, len,
        "{\"device_id\":\"%s\",\"seq\":%" PRIu32 ",\"temperature_c\":%.1f,\"humidity_pct\":%.1f,\"valid\":%s,\"sample_uptime_ms\":%" PRIu64 ","
        "\"soil_moisture_pct\":%.1f,\"soil_valid\":%s,\"plant_id\":%u,\"ml_predicted\":%.1f,\"pump_on\":%s,\"irrigation_count\":%" PRIu32 "}",
        data->device_id, data->seq, (double)data->temperature_c, (double)data->humidity_pct,
        data->valid ? "true" : "false", data->sample_uptime_ms,
        (double)data->soil_moisture_pct, data->soil_valid ? "true" : "false", (unsigned)data->plant_id,
        (double)data->ml_predicted, data->pump_on ? "true" : "false", data->irrigation_count);
    return (n < 0 || (size_t)n >= len) ? ESP_ERR_NO_MEM : ESP_OK;
}

esp_err_t irrigation_report_encode_json(const irrigation_report_data_t *data, char *buffer, size_t len)
{
    if (data == NULL || buffer == NULL || len == 0 || data->device_id == NULL) return ESP_ERR_INVALID_ARG;
    int n = snprintf(buffer, len,
        "{\"device_id\":\"%s\",\"seq\":%" PRIu32 ",\"plant_id\":%u,\"soil_moisture_pct\":%.1f,\"temperature_c\":%.1f,"
        "\"ml_predicted\":%.1f,\"ml_delivered\":%.1f,\"duration_ms\":%" PRIu32 ",\"pump_uptime_ms\":%" PRIu64 "}",
        data->device_id, data->seq, (unsigned)data->plant_id, (double)data->soil_moisture_pct, (double)data->temperature_c,
        (double)data->ml_predicted, (double)data->ml_delivered, data->duration_ms, data->pump_uptime_ms);
    return (n < 0 || (size_t)n >= len) ? ESP_ERR_NO_MEM : ESP_OK;
}

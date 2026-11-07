#include "health.h"
#include <inttypes.h>
#include <stdio.h>

esp_err_t health_encode_json(const health_data_t *data, char *buffer, size_t len)
{
    if (data == NULL || buffer == NULL || len == 0 ||
        data->device_id == NULL || data->firmware_version == NULL ||
        data->connectivity == NULL || data->reset_reason == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int n = snprintf(
        buffer,
        len,
        "{\"device_id\":\"%s\",\"firmware\":\"%s\","
        "\"uptime_s\":%" PRIu64 ",\"connectivity\":\"%s\","
        "\"time_synced\":%s,\"mqtt_connected\":%s,"
        "\"command_subscribed\":%s,\"sensor_ok\":%s,"
        "\"last_sensor_ok_ms\":%" PRIu64 ","
        "\"sensor_error_count\":%" PRIu32 ","
        "\"publish_drop_count\":%" PRIu32 ","
        "\"command_drop_count\":%" PRIu32 ","
        "\"free_heap_bytes\":%" PRIu32 ",\"reset_reason\":\"%s\"}",
        data->device_id,
        data->firmware_version,
        (uint64_t)(data->uptime_ms / 1000ULL),
        data->connectivity,
        data->time_synced ? "true" : "false",
        data->mqtt_connected ? "true" : "false",
        data->command_subscribed ? "true" : "false",
        data->sensor_ok ? "true" : "false",
        data->last_sensor_ok_ms,
        data->sensor_error_count,
        data->publish_drop_count,
        data->command_drop_count,
        data->free_heap_bytes,
        data->reset_reason);

    return (n < 0 || (size_t)n >= len) ? ESP_ERR_NO_MEM : ESP_OK;
}

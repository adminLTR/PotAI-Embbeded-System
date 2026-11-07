#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    const char *device_id;
    const char *firmware_version;
    uint64_t uptime_ms;
    const char *connectivity;
    bool time_synced;
    bool mqtt_connected;
    bool command_subscribed;
    bool sensor_ok;
    uint64_t last_sensor_ok_ms;
    uint32_t sensor_error_count;
    uint32_t publish_drop_count;
    uint32_t command_drop_count;
    uint32_t free_heap_bytes;
    const char *reset_reason;
} health_data_t;

esp_err_t health_encode_json(const health_data_t *data, char *buffer, size_t buffer_len);

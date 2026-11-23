#include "api_logic.h"

#include "connectivity_fsm.h"
#include "sdkconfig.h"

#include <inttypes.h>
#include <stdio.h>

static api_status_t write_or_too_small(int n, size_t size)
{
    if (n < 0 || (size_t)n >= size) {
        return API_STATUS_BUFFER_TOO_SMALL;
    }
    return API_STATUS_OK;
}

api_status_t api_build_health_response(char *buffer, size_t size)
{
    if (buffer == NULL || size == 0) {
        return API_STATUS_BUFFER_TOO_SMALL;
    }

    int n = snprintf(buffer, size, "{\"ok\":true}");
    return write_or_too_small(n, size);
}

api_status_t api_build_status_response(
    const device_snapshot_t *snapshot,
    connectivity_state_t connectivity,
    char *buffer,
    size_t size)
{
    if (buffer == NULL || size == 0 || snapshot == NULL) {
        return API_STATUS_BUFFER_TOO_SMALL;
    }

    const char *conn = connectivity_state_name(connectivity);
    if (conn == NULL) {
        conn = "UNKNOWN";
    }

    int n = snprintf(
        buffer,
        size,
        "{\"device_id\":\"%s\",\"connectivity\":\"%s\",\"door_open\":%s,\"led_on\":%s,"
        "\"open_event_count\":%" PRIu32 ",\"raw_isr_count\":%" PRIu32 ","
        "\"sample_valid\":%s,\"soil_valid\":%s,\"soil_moisture_pct\":%.1f,"
        "\"pump_on\":%s,\"irrigation_count\":%" PRIu32 ","
        "\"ml_predicted\":%.1f,\"ml_delivered\":%.1f}",
        snapshot->device_id,
        conn,
        snapshot->door_open ? "true" : "false",
        snapshot->led_on ? "true" : "false",
        snapshot->open_event_count,
        snapshot->raw_isr_count,
        snapshot->sample_valid ? "true" : "false",
        snapshot->soil_valid ? "true" : "false",
        (double)snapshot->soil_moisture_pct,
        snapshot->pump_on ? "true" : "false",
        snapshot->irrigation_count,
        (double)snapshot->last_ml_predicted,
        (double)snapshot->last_ml_delivered);

    return write_or_too_small(n, size);
}

api_status_t api_build_telemetry_response(const device_snapshot_t *snapshot, char *buffer, size_t size)
{
    if (buffer == NULL || size == 0 || snapshot == NULL) {
        return API_STATUS_BUFFER_TOO_SMALL;
    }

    if (!snapshot->sample_valid) {
        int n = snprintf(buffer, size, "{\"error\":\"telemetry_not_available\"}");
        if (n < 0 || (size_t)n >= size) {
            return API_STATUS_BUFFER_TOO_SMALL;
        }
        return API_STATUS_UNAVAILABLE;
    }

    int n = snprintf(
        buffer,
        size,
        "{\"device_id\":\"%s\",\"temperature_c\":%.1f,\"humidity_pct\":%.1f,\"valid\":%s,"
        "\"sample_uptime_ms\":%" PRIu64 ","
        "\"soil_moisture_pct\":%.1f,\"soil_valid\":%s,\"plant_id\":%u,"
        "\"ml_predicted\":%.1f,\"pump_on\":%s,\"irrigation_count\":%" PRIu32 "}",
        snapshot->device_id,
        (double)snapshot->temperature_c,
        (double)snapshot->humidity_pct,
        snapshot->sample_valid ? "true" : "false",
        snapshot->sample_uptime_ms,
        (double)snapshot->soil_moisture_pct,
        snapshot->soil_valid ? "true" : "false",
        (unsigned)CONFIG_A10_PLANT_ID,
        (double)snapshot->last_ml_predicted,
        snapshot->pump_on ? "true" : "false",
        snapshot->irrigation_count);

    return write_or_too_small(n, size);
}

const char *api_status_name(api_status_t status)
{
    switch (status) {
    case API_STATUS_OK:
        return "OK";
    case API_STATUS_UNAVAILABLE:
        return "UNAVAILABLE";
    case API_STATUS_BUFFER_TOO_SMALL:
        return "BUFFER_TOO_SMALL";
    default:
        return "UNKNOWN";
    }
}
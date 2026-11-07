#include "device_state.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static device_snapshot_t s_snapshot;
static SemaphoreHandle_t s_mutex;

static void lock(void)
{
    if (s_mutex != NULL) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    }
}

static void unlock(void)
{
    if (s_mutex != NULL) {
        xSemaphoreGive(s_mutex);
    }
}

void device_state_init(const char *device_id)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        configASSERT(s_mutex != NULL);
    }

    lock();
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    if (device_id != NULL) {
        strncpy(
            s_snapshot.device_id,
            device_id,
            sizeof(s_snapshot.device_id) - 1);
        s_snapshot.device_id[sizeof(s_snapshot.device_id) - 1] = '\0';
    }
    unlock();
}

void device_state_update_telemetry(
    float temperature_c,
    float humidity_pct,
    bool valid,
    uint64_t update_uptime_ms)
{
    lock();

    if (valid) {
        s_snapshot.temperature_c = temperature_c;
        s_snapshot.humidity_pct = humidity_pct;
        s_snapshot.sample_valid = true;
        s_snapshot.sample_uptime_ms = update_uptime_ms;
    }

    /* Uma falha posterior nao apaga a ultima amostra valida. */
    s_snapshot.last_update_uptime_ms = update_uptime_ms;

    unlock();
}

void device_state_update_local(
    bool door_open,
    bool led_on,
    uint32_t open_event_count,
    uint32_t raw_isr_count,
    uint64_t update_uptime_ms)
{
    lock();
    s_snapshot.door_open = door_open;
    s_snapshot.led_on = led_on;
    s_snapshot.open_event_count = open_event_count;
    s_snapshot.raw_isr_count = raw_isr_count;
    s_snapshot.last_update_uptime_ms = update_uptime_ms;
    unlock();
}

void device_state_update_led(bool led_on, uint64_t update_uptime_ms)
{
    lock();
    s_snapshot.led_on = led_on;
    s_snapshot.last_update_uptime_ms = update_uptime_ms;
    unlock();
}

void device_state_update_soil(
    float soil_moisture_pct,
    int soil_raw,
    bool valid,
    uint64_t update_uptime_ms)
{
    lock();
    s_snapshot.soil_raw = soil_raw;
    if (valid) {
        s_snapshot.soil_moisture_pct = soil_moisture_pct;
        s_snapshot.soil_valid = true;
        s_snapshot.soil_sample_uptime_ms = update_uptime_ms;
    }
    s_snapshot.last_update_uptime_ms = update_uptime_ms;
    unlock();
}

void device_state_update_irrigation(
    bool pump_on,
    float last_ml_predicted,
    float last_ml_delivered,
    uint32_t irrigation_count,
    uint64_t update_uptime_ms)
{
    lock();
    s_snapshot.pump_on = pump_on;
    s_snapshot.last_ml_predicted = last_ml_predicted;
    s_snapshot.last_ml_delivered = last_ml_delivered;
    s_snapshot.irrigation_count = irrigation_count;
    s_snapshot.last_update_uptime_ms = update_uptime_ms;
    unlock();
}

void device_state_copy(device_snapshot_t *out)
{
    if (out == NULL) {
        return;
    }

    lock();
    *out = s_snapshot;
    unlock();
}

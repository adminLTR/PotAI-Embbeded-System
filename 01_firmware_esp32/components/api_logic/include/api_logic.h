#pragma once
#include <stddef.h>
#include "device_state.h"
#include "connectivity_fsm.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { API_STATUS_OK = 0, API_STATUS_UNAVAILABLE = 1, API_STATUS_BUFFER_TOO_SMALL = 2 } api_status_t;
api_status_t api_build_health_response(char *buffer, size_t size);
api_status_t api_build_status_response(const device_snapshot_t *snapshot, connectivity_state_t connectivity, char *buffer, size_t size);
api_status_t api_build_telemetry_response(const device_snapshot_t *snapshot, char *buffer, size_t size);
const char *api_status_name(api_status_t status);
#ifdef __cplusplus
}
#endif

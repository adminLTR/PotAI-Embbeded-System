#pragma once
#include <stdbool.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    TIME_SYNC_NOT_SYNCED = 0,
    TIME_SYNC_SYNCING,
    TIME_SYNC_SYNCED,
    TIME_SYNC_ERROR,
} time_sync_state_t;
esp_err_t time_sync_start(const char *server_name);
time_sync_state_t time_sync_get_state(void);
bool time_sync_is_ready(void);
const char *time_sync_state_name(time_sync_state_t state);
#ifdef __cplusplus
}
#endif

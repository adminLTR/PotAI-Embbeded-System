#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t http_service_start(void);
esp_err_t http_service_stop(void);
bool http_service_is_started(void);

#ifdef __cplusplus
}
#endif

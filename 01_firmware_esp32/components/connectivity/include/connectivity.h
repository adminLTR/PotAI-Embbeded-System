#pragma once

#include "esp_err.h"
#include "connectivity_fsm.h"

#ifdef __cplusplus
extern "C" {
#endif

void connectivity_init(void);
esp_err_t connectivity_start(void);
connectivity_state_t connectivity_get_state(void);

#ifdef __cplusplus
}
#endif

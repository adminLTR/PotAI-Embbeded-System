#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CONNECTIVITY_LOCAL_ONLY = 0,
    CONNECTIVITY_CONNECTING = 1,
    CONNECTIVITY_ASSOCIATED = 2,
    CONNECTIVITY_ONLINE = 3,
} connectivity_state_t;

typedef enum {
    CONNECTIVITY_EVENT_START = 0,
    CONNECTIVITY_EVENT_ASSOCIATED = 1,
    CONNECTIVITY_EVENT_IP_READY = 2,
    CONNECTIVITY_EVENT_DISCONNECTED = 3,
} connectivity_event_t;

typedef struct {
    connectivity_state_t state;
} connectivity_fsm_t;

void connectivity_fsm_init(connectivity_fsm_t *fsm);

connectivity_state_t connectivity_fsm_handle(
    connectivity_fsm_t *fsm,
    connectivity_event_t event);

connectivity_state_t connectivity_fsm_state(
    const connectivity_fsm_t *fsm);

const char *connectivity_state_name(connectivity_state_t state);
const char *connectivity_event_name(connectivity_event_t event);

#ifdef __cplusplus
}
#endif

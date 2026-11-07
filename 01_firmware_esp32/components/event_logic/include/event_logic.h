#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DOOR_CLOSED = 0,
    DOOR_OPEN = 1,
} door_state_t;

typedef enum {
    RESULT_INVALID = -1,
    RESULT_NO_CHANGE = 0,
    RESULT_OPEN_ACCEPTED = 1,
    RESULT_CLOSE_ACCEPTED = 2,
} result_t;

typedef struct {
    door_state_t door_state;
    uint32_t open_event_count;
    uint64_t last_transition_ms;
} system_state_t;

void system_state_init(system_state_t *state);

result_t handle_door_level(
    system_state_t *state,
    bool door_open,
    uint64_t now_ms);

const char *door_state_name(door_state_t state);
const char *result_name(result_t result);

#ifdef __cplusplus
}
#endif

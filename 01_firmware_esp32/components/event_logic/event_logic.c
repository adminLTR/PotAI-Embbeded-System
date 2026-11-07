#include "event_logic.h"

void system_state_init(system_state_t *state)
{
    if (state == 0) {
        return;
    }

    state->door_state = DOOR_CLOSED;
    state->open_event_count = 0;
    state->last_transition_ms = 0;
}

result_t handle_door_level(
    system_state_t *state,
    bool door_open,
    uint64_t now_ms)
{
    if (state == 0) {
        return RESULT_INVALID;
    }

    if (state->door_state == DOOR_CLOSED && door_open) {
        state->door_state = DOOR_OPEN;
        state->open_event_count++;
        state->last_transition_ms = now_ms;
        return RESULT_OPEN_ACCEPTED;
    }

    if (state->door_state == DOOR_OPEN && !door_open) {
        state->door_state = DOOR_CLOSED;
        state->last_transition_ms = now_ms;
        return RESULT_CLOSE_ACCEPTED;
    }

    return RESULT_NO_CHANGE;
}

const char *door_state_name(door_state_t state)
{
    switch (state) {
    case DOOR_CLOSED:
        return "CLOSED";
    case DOOR_OPEN:
        return "OPEN";
    default:
        return "INVALID";
    }
}

const char *result_name(result_t result)
{
    switch (result) {
    case RESULT_INVALID:
        return "INVALID";
    case RESULT_NO_CHANGE:
        return "NO_CHANGE";
    case RESULT_OPEN_ACCEPTED:
        return "OPEN_ACCEPTED";
    case RESULT_CLOSE_ACCEPTED:
        return "CLOSE_ACCEPTED";
    default:
        return "UNKNOWN";
    }
}

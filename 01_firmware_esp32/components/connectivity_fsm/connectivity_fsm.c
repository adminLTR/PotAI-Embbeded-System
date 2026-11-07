#include "connectivity_fsm.h"

void connectivity_fsm_init(connectivity_fsm_t *fsm)
{
    if (fsm == 0) {
        return;
    }

    fsm->state = CONNECTIVITY_LOCAL_ONLY;
}

connectivity_state_t connectivity_fsm_handle(
    connectivity_fsm_t *fsm,
    connectivity_event_t event)
{
    if (fsm == 0) {
        return CONNECTIVITY_LOCAL_ONLY;
    }

    switch (fsm->state) {
    case CONNECTIVITY_LOCAL_ONLY:
        if (event == CONNECTIVITY_EVENT_START) {
            fsm->state = CONNECTIVITY_CONNECTING;
        }
        break;

    case CONNECTIVITY_CONNECTING:
        if (event == CONNECTIVITY_EVENT_ASSOCIATED) {
            fsm->state = CONNECTIVITY_ASSOCIATED;
        } else if (event == CONNECTIVITY_EVENT_IP_READY) {
            /*
             * Decisao defensiva consolidada na Aula 6: IP_READY e uma
             * evidencia mais forte e permite reconhecer ONLINE mesmo se o
             * evento ASSOCIATED nao tiver sido observado pela aplicacao.
             */
            fsm->state = CONNECTIVITY_ONLINE;
        } else if (event == CONNECTIVITY_EVENT_DISCONNECTED) {
            fsm->state = CONNECTIVITY_LOCAL_ONLY;
        }
        break;

    case CONNECTIVITY_ASSOCIATED:
        if (event == CONNECTIVITY_EVENT_IP_READY) {
            fsm->state = CONNECTIVITY_ONLINE;
        } else if (event == CONNECTIVITY_EVENT_DISCONNECTED) {
            fsm->state = CONNECTIVITY_LOCAL_ONLY;
        }
        break;

    case CONNECTIVITY_ONLINE:
        if (event == CONNECTIVITY_EVENT_DISCONNECTED) {
            fsm->state = CONNECTIVITY_LOCAL_ONLY;
        }
        break;

    default:
        fsm->state = CONNECTIVITY_LOCAL_ONLY;
        break;
    }

    return fsm->state;
}

connectivity_state_t connectivity_fsm_state(const connectivity_fsm_t *fsm)
{
    return fsm ? fsm->state : CONNECTIVITY_LOCAL_ONLY;
}

const char *connectivity_state_name(connectivity_state_t state)
{
    switch (state) {
    case CONNECTIVITY_LOCAL_ONLY:
        return "LOCAL_ONLY";
    case CONNECTIVITY_CONNECTING:
        return "CONNECTING";
    case CONNECTIVITY_ASSOCIATED:
        return "ASSOCIATED";
    case CONNECTIVITY_ONLINE:
        return "ONLINE";
    default:
        return "UNKNOWN";
    }
}

const char *connectivity_event_name(connectivity_event_t event)
{
    switch (event) {
    case CONNECTIVITY_EVENT_START:
        return "START";
    case CONNECTIVITY_EVENT_ASSOCIATED:
        return "ASSOCIATED";
    case CONNECTIVITY_EVENT_IP_READY:
        return "IP_READY";
    case CONNECTIVITY_EVENT_DISCONNECTED:
        return "DISCONNECTED";
    default:
        return "UNKNOWN";
    }
}

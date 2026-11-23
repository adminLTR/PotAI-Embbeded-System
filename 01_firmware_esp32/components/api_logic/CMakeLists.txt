idf_component_register(
    SRCS "api_logic.c"
    INCLUDE_DIRS "include"
    REQUIRES device_state connectivity_fsm
)
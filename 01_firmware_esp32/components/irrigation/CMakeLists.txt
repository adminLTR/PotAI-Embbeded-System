idf_component_register(
    SRCS "irrigation.c"
    INCLUDE_DIRS "include"
    REQUIRES ml_model pump buzzer device_state esp_timer log freertos
)

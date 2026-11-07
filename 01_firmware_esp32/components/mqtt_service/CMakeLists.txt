idf_component_register(
    SRCS "mqtt_service.c"
    INCLUDE_DIRS "include"
    REQUIRES health telemetry mqtt_adapter cloud_command
)

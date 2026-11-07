idf_component_register(
    SRCS "mqtt_adapter.c"
    INCLUDE_DIRS "include"
    REQUIRES mqtt esp_event esp_netif aws_iot_credentials
)

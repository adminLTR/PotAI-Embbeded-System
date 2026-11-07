idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES dht11 health event_logic connectivity device_state http_service api_logic telemetry mqtt_service mqtt_adapter time_sync cloud_command aws_iot_credentials ml_model soil_moisture pump buzzer lcd_i2c display_ui irrigation esp_driver_gpio esp_timer log
)

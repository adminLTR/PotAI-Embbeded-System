idf_component_register(
    SRCS "display_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES oled_ssd1306 device_state esp_driver_gpio esp_timer log freertos
)

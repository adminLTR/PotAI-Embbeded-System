idf_component_register(
    SRCS "display_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES lcd_i2c device_state esp_driver_gpio esp_timer log freertos
)

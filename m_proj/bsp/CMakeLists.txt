idf_component_register(SRCS "app_led.c"
                       SRCS "app_bsp.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_driver_gpio freertos)

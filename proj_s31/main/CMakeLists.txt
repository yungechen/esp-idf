include(${CMAKE_CURRENT_LIST_DIR}/common/CMakeLists.txt)
include(${CMAKE_CURRENT_LIST_DIR}/bsp/CMakeLists.txt)

idf_component_register(SRCS "app_main.c"
                            "cmd_ethernet.c"
                            ${COMMON_SRCS}
                            ${BSP_SRCS}
                       INCLUDE_DIRS "."
                                     ${COMMON_INCLUDE_DIRS}
                                     ${BSP_INCLUDE_DIRS}
                       PRIV_REQUIRES fatfs esp_netif esp_driver_gptimer esp_eth console nvs_flash spi_flash esp_driver_uart esp_driver_gpio esp_wifi esp_timer)

include(${CMAKE_CURRENT_LIST_DIR}/common/CMakeLists.txt)
include(${CMAKE_CURRENT_LIST_DIR}/bsp/CMakeLists.txt)

# Component name == directory name under proj_s31/thirdparty/ (EXTRA_COMPONENT_DIRS).
set(MAIN_SRCS "app_main.c" ${COMMON_SRCS} ${BSP_SRCS})
set(MAIN_PRIV_REQUIRES
    littlefs esp_netif esp_driver_gptimer console nvs_flash spi_flash
    esp_driver_uart esp_driver_gpio esp_wifi esp_timer
    esp_driver_i2c esp_driver_i2s esp_codec_dev
)

if(CONFIG_APP_INIT_ETHERNET)
    list(APPEND MAIN_SRCS "cmd_ethernet.c")
    list(APPEND MAIN_PRIV_REQUIRES esp_eth ethernet_init)
endif()

idf_component_register(SRCS ${MAIN_SRCS}
                       INCLUDE_DIRS "."
                                     ${COMMON_INCLUDE_DIRS}
                                     ${BSP_INCLUDE_DIRS}
                       PRIV_REQUIRES ${MAIN_PRIV_REQUIRES})

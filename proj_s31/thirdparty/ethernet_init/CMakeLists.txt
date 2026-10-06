set(priv_requires esp_driver_gpio esp_driver_spi esp_eth)
if(CONFIG_ETHERNET_PHY_YT8531)
    list(APPEND priv_requires yt8531)
endif()

idf_component_register(SRCS "ethernet_init.c"
                       PRIV_REQUIRES ${priv_requires}
                       INCLUDE_DIRS ".")

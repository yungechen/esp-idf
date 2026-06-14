#include "app_led.h"

#include "app_bsp.h"

esp_err_t app_bsp_init(void)
{
#if CONFIG_APP_LED_SUPPORT
    ESP_ERROR_CHECK(app_led_init());
#endif
    return ESP_OK;
}
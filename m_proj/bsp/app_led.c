#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "app_led.h"

static const char *TAG = "app_led";

static esp_err_t app_led_init_impl(void)
{
    gpio_config_t gpio_init_struct = {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pin_bit_mask = 1ULL << CONFIG_APP_LED_PIN,
    };

    esp_err_t ret = gpio_config(&gpio_init_struct);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    gpio_set_level(CONFIG_APP_LED_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(1000));
    gpio_set_level(CONFIG_APP_LED_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(1000));

    return ESP_OK;
}

static const T_AppBspModule s_app_led_module = {
    .name = "led",
    .start = app_led_init_impl,
};

T_AppBspModule *app_led_init(void)
{
    return (T_AppBspModule *)&s_app_led_module;
}


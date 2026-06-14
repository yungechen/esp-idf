#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_led.h"

esp_err_t app_led_init(void)
{
    gpio_config_t gpio_init_struct;
    gpio_init_struct.intr_type      = GPIO_INTR_DISABLE;
    gpio_init_struct.mode           = GPIO_MODE_INPUT_OUTPUT;
    gpio_init_struct.pull_up_en     = GPIO_PULLUP_DISABLE;
    gpio_init_struct.pull_down_en   = GPIO_PULLDOWN_DISABLE;
    gpio_init_struct.pin_bit_mask   = 1ULL << CONFIG_APP_LED_PIN;
    ESP_ERROR_CHECK(gpio_config(&gpio_init_struct));

    gpio_set_level(CONFIG_APP_LED_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(1000));
    gpio_set_level(CONFIG_APP_LED_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(1000));
    return ESP_OK;
}

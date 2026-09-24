#include "esp_err.h"
#include "esp_console.h"
#include "iperf_cmd.h"
#include "cmd_system.h"
#include "cmd_nvs.h"
#include "cmd_filemgr.h"
#include "cmdmgr.h"

#define APP_FILEMGR_HISTORY_PATH "/mnt/history.txt"

static const char *TAG = "cmdmgr";

esp_err_t cmdmgr_init(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();


    repl_config.history_save_path = APP_FILEMGR_HISTORY_PATH;
    repl_config.prompt = "esp32>";

    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));
    
    iperf_cmd_register_iperf();

    register_system_common();
    register_nvs();
    register_filemgr();

    // start console REPL
    ESP_ERROR_CHECK(esp_console_start_repl(repl));

    return ESP_OK;
}

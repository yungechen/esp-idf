/*
 * SPDX-FileCopyrightText: 2018-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_console.h"
#include "esp_event.h"
#include "esp_eth.h"
#include "esp_netif.h"
#include "ethernet_init.h"
#include "esp_vfs_fat.h"
#include "cmd_system.h"
#include "cmd_ethernet.h"

#include "iperf_cmd.h"

static const char *TAG = "eth_example";

static esp_eth_handle_t *s_eth_handles = NULL;
static uint8_t s_eth_port_cnt = 0;

static SemaphoreHandle_t ip_got_sem;

/**
 * @brief Initialize YT8531 PHY specific configuration
 *
 * @note This function demonstrates how to configure PHY specific registers needed for proper
 *       operation of the PHY without specific PHY driver by just using the esp_eth_ioctl API.
 *       This example is YT8531 specific but you can use it as a template to configure other PHYs.
 *
 * @param eth_handle Ethernet handle
 * @return ESP_OK on success, ESP_FAIL on failure
 */
 static esp_err_t eth_phy_yt8531_specific_init(esp_eth_handle_t eth_handle)
 {
    /* When the YT8531 PHY is reset during the Generic 802.3 PHY driver initialization, it disables auto negotiation.
     * So we need to enable it again. This is undocumented but observed behavior.
     */
    bool auto_nego_en = true;
    ESP_RETURN_ON_ERROR(esp_eth_ioctl(eth_handle, ETH_CMD_S_AUTONEGO, &auto_nego_en), TAG, "set auto negotiation failed");
 
    /*
    * RGMII requires Tx and Rx paths clock delays to be configured.
    *
    * Target delay: ~2 ns on both Tx and Rx.
    */
    esp_eth_phy_reg_rw_data_t phy_reg = {.reg_value_p = NULL};
    uint32_t reg_val;
    phy_reg.reg_value_p = &reg_val;

    // --- Configure RX ~2 ns coarse delay (EXT_CHIP_CONFIG 0xA001, bit[8]) ---
    reg_val = 0xA001;
    phy_reg.reg_addr = 0x1E;  // EXT address register
    ESP_RETURN_ON_ERROR(esp_eth_ioctl(eth_handle, ETH_CMD_WRITE_PHY_REG, &phy_reg), TAG, "write EXT addr reg (Chip_Config) failed");
    phy_reg.reg_addr = 0x1F;  // EXT data register
    ESP_RETURN_ON_ERROR(esp_eth_ioctl(eth_handle, ETH_CMD_READ_PHY_REG, &phy_reg), TAG, "read Chip_Config failed");
    reg_val |= (1U << 8);     // set rxc_dly_en
    ESP_RETURN_ON_ERROR(esp_eth_ioctl(eth_handle, ETH_CMD_WRITE_PHY_REG, &phy_reg), TAG, "write Chip_Config failed");

    // --- Configure TX ~2 ns delay (EXT_RGMII_CONFIG1 0xA003, bits[7:0]) ---
    reg_val = 0xA003;
    phy_reg.reg_addr = 0x1E;  // EXT address register
    ESP_RETURN_ON_ERROR(esp_eth_ioctl(eth_handle, ETH_CMD_WRITE_PHY_REG, &phy_reg), TAG, "write EXT addr reg (RGMII_Config1) failed");
    phy_reg.reg_addr = 0x1F;  // EXT data register
    ESP_RETURN_ON_ERROR(esp_eth_ioctl(eth_handle, ETH_CMD_READ_PHY_REG, &phy_reg), TAG, "read RGMII_Config1 failed");
    // Clear tx_delay_sel [3:0] and tx_delay_sel_fe [7:4], then set both to 13 (~1.95 ns)
    reg_val = (reg_val & ~0x00FFU) | (13U << 4) | (13U << 0);
    ESP_RETURN_ON_ERROR(esp_eth_ioctl(eth_handle, ETH_CMD_WRITE_PHY_REG, &phy_reg), TAG, "write RGMII_Config1 failed");

    ESP_LOGI(TAG, "RGMII PHY delays configured: Rx ~2 ns (coarse), Tx ~2 ns (13 steps x 150 ps)");
    return ESP_OK;
 }

#if CONFIG_EXAMPLE_STORE_HISTORY

#define MOUNT_PATH "/data"
#define HISTORY_PATH MOUNT_PATH "/history.txt"

static void initialize_filesystem(void)
{
    static wl_handle_t wl_handle;
    const esp_vfs_fat_mount_config_t mount_config = {
        .max_files = 4,
        .format_if_mount_failed = true
    };
    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(MOUNT_PATH, "storage", &mount_config, &wl_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(err));
        return;
    }
}
#endif // CONFIG_EXAMPLE_STORE_HISTORY

static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    xSemaphoreGive(ip_got_sem);
}

void init_ethernet_and_netif(void)
{
    ip_got_sem = xSemaphoreCreateBinary();
    if (ip_got_sem == NULL) {
        ESP_LOGE(TAG, "Failed to create semaphore");
        return;
    }

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(ethernet_init_all(&s_eth_handles, &s_eth_port_cnt));

    for (int i = 0; i < s_eth_port_cnt; i++) {
        ESP_ERROR_CHECK(eth_phy_yt8531_specific_init(s_eth_handles[i]));
    }

    ESP_ERROR_CHECK(esp_netif_init());
    esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
    esp_netif_config_t cfg_spi = {
        .base = &esp_netif_config,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH
    };
    char if_key_str[10];
    char if_desc_str[10];
    char num_str[3];
    for (int i = 0; i < s_eth_port_cnt; i++) {
        itoa(i, num_str, 10);
        strcat(strcpy(if_key_str, "ETH_"), num_str);
        strcat(strcpy(if_desc_str, "eth"), num_str);
        esp_netif_config.if_key = if_key_str;
        esp_netif_config.if_desc = if_desc_str;
        esp_netif_config.route_prio -= i*5;
        esp_netif_t *eth_netif = esp_netif_new(&cfg_spi);

        // attach Ethernet driver to TCP/IP stack
        ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(s_eth_handles[i])));
    }

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

    for (int i = 0; i < s_eth_port_cnt; i++) {
        ESP_ERROR_CHECK(esp_eth_start(s_eth_handles[i]));
    }

    if (xSemaphoreTake(ip_got_sem, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Timeout waiting for ETH IP");
    }
}

void app_main(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
#if CONFIG_EXAMPLE_STORE_HISTORY
    initialize_filesystem();
    repl_config.history_save_path = HISTORY_PATH;
#endif
    repl_config.prompt = "iperf>";
    // init console REPL environment
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));

    // init Ethernet and netif
    init_ethernet_and_netif();

    /* Register commands */
    register_system_common();
    iperf_cmd_register_iperf();
    register_ethernet_commands();

    printf("\n =======================================================\n");
    printf(" |       Steps to Test Ethernet Bandwidth              |\n");
    printf(" |                                                     |\n");
    printf(" |  1. Enter 'help', check all supported commands      |\n");
    printf(" |  2. Wait ESP32 to get IP from DHCP                  |\n");
    printf(" |  3. Enter 'ethernet info', optional                 |\n");
    printf(" |  4. Server: 'iperf -u -s -i 3'                      |\n");
    printf(" |  5. Client: 'iperf -u -c SERVER_IP -t 60 -i 3'      |\n");
    printf(" |                                                     |\n");
    printf(" =======================================================\n\n");

    // start console REPL
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}

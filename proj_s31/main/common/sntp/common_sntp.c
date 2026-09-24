#include "esp_netif_sntp.h"
#include "common_sntp.h"

esp_err_t common_sntp_init(void)
{
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&cfg);
    setenv("TZ", "CST-8", 1);
    tzset();
    return ESP_OK;
}

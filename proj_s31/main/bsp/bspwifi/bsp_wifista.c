#include "bsp_wifimgr.h"
#include "esp_wifi_types_generic.h"

static esp_err_t bsp_wifi_sta_start(void);
static esp_err_t bsp_wifi_sta_stop(void);
static void bsp_wifi_sta_on_event(E_WIFI_MGR_EVENT evt, void *user);

static T_WIFI_MGR_OPS s_wifi_sta_ops = {
    .idf_mode = WIFI_MODE_STA,
    .start = bsp_wifi_sta_start,
    .stop = bsp_wifi_sta_stop,
    .on_event = bsp_wifi_sta_on_event,
};

static esp_err_t bsp_wifi_sta_start(void)
{
    return ESP_OK;
}

static esp_err_t bsp_wifi_sta_stop(void)
{
    return ESP_OK;
}

static void bsp_wifi_sta_on_event(E_WIFI_MGR_EVENT evt, void *user)
{
    
}

T_WIFI_MGR_OPS *get_wifi_sta_ops(void)
{
    return &s_wifi_sta_ops;
}
#include "bsp_wifimgr.h"

static esp_err_t bsp_wifiap_start(void);
static esp_err_t bsp_wifiap_stop(void);
static void bsp_wifiap_on_event(E_WIFI_MGR_EVENT evt, void *user, void *data);

static T_WIFI_MGR_OPS s_wifi_ap_ops = {
    .idf_mode = WIFI_MODE_AP,
    .start = bsp_wifiap_start,
    .stop = bsp_wifiap_stop,
    .on_event = bsp_wifiap_on_event,
};

static esp_err_t bsp_wifiap_start(void)
{
    return ESP_OK;
}

static esp_err_t bsp_wifiap_stop(void)
{
    return ESP_OK;
}

static void bsp_wifiap_on_event(E_WIFI_MGR_EVENT evt, void *user, void *data)
{
    
}

T_WIFI_MGR_OPS *get_wifi_ap_ops(void)
{
    return &s_wifi_ap_ops;
}
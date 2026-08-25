#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_wifi_types_generic.h"
#include "bsp_wifimgr.h"

#define SCAN_LIST_SIZE 20
#define MAX_CONNECT_CNT 10

typedef struct _T_WIFI_STA_CTX
{
    wifi_ap_record_t ap_info[SCAN_LIST_SIZE];   // ap info
    uint16_t ap_count;                          // ap count
    uint16_t number;                            // number of aps

    uint8_t connect_cnt;                       // connect count
}T_WIFI_STA_CTX;

T_WIFI_STA_CTX g_wifi_sta_ctx = {0};

static const char *TAG = "wifi_sta";

static esp_err_t bsp_wifi_sta_start(void);
static esp_err_t bsp_wifi_sta_stop(void);
static void bsp_wifi_sta_on_event(E_WIFI_MGR_EVENT evt, void *user);
static esp_err_t bsp_wifi_sta_set_cfg(const T_WIFI_MGR_CFG *apcfg, const T_WIFI_MGR_CFG *stacfg);
static esp_err_t bsp_wifi_sta_change_cfg(const T_WIFI_MGR_CFG *apcfg, const T_WIFI_MGR_CFG *stacfg);

static T_WIFI_MGR_OPS s_wifi_sta_ops = {
    .idf_mode = WIFI_MODE_STA,
    .start = bsp_wifi_sta_start,
    .stop = bsp_wifi_sta_stop,
    .on_event = bsp_wifi_sta_on_event,
    .set_cfg = bsp_wifi_sta_set_cfg,
    .change_cfg = bsp_wifi_sta_change_cfg,
};

static esp_err_t bsp_wifi_sta_start(void)
{
    esp_wifi_start();
    return ESP_OK;
}

static esp_err_t bsp_wifi_sta_stop(void)
{
    esp_wifi_stop();
    return ESP_OK;
}

static esp_err_t bsp_wifi_sta_set_cfg(const T_WIFI_MGR_CFG *apcfg, const T_WIFI_MGR_CFG *stacfg)
{
    if(NULL == stacfg)
    {
        return ESP_ERR_INVALID_ARG;
    }
    wifi_config_t cfg;

    memset(&cfg, 0, sizeof(wifi_config_t));
    uint32_t len = strlen(stacfg->ssid);
    if(len > sizeof(cfg.sta.ssid))
    {
        len = sizeof(cfg.sta.ssid);
    }
    memcpy(&cfg.sta.ssid, stacfg->ssid, len);

    len = strlen(stacfg->pwd);
    if(len > sizeof(cfg.sta.password))
    {
        len = sizeof(cfg.sta.password);
    }
    memcpy(cfg.sta.password, stacfg->pwd, len);

    cfg.sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    cfg.sta.threshold.rssi = -127;
    esp_wifi_set_mode(WIFI_MODE_STA);
    
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    ESP_LOGI(TAG, "connect_cnt: %u", g_wifi_sta_ctx.connect_cnt);
    return ESP_OK;
}

static esp_err_t bsp_wifi_sta_change_cfg(const T_WIFI_MGR_CFG *apcfg, const T_WIFI_MGR_CFG *stacfg)
{
    bsp_wifi_sta_set_cfg(apcfg, stacfg);
    esp_wifi_disconnect();
    if(g_wifi_sta_ctx.connect_cnt >= 11)
    {
        esp_wifi_connect();
    }
    g_wifi_sta_ctx.connect_cnt = 0;
    return ESP_OK;
}

static void bsp_wifi_sta_on_event(E_WIFI_MGR_EVENT evt, void *user)
{
    switch(evt)
    {
    case WIFI_MGR_EVT_STA_START:
        esp_wifi_connect();
        break;
    case WIFI_MGR_EVT_STA_CONNECTED:
        break;
    case WIFI_MGR_EVT_STA_DISCONNECTED:
        if(g_wifi_sta_ctx.connect_cnt <= MAX_CONNECT_CNT)
        {
            g_wifi_sta_ctx.connect_cnt++;
            esp_wifi_connect();
            ESP_LOGI(TAG, "connect_cnt: %u", g_wifi_sta_ctx.connect_cnt);
        }      
        break;
    case WIFI_MGR_EVT_STA_GOT_IP:
        break;
    case WIFI_MGR_EVT_10S_TIMER:
    {
        memset(g_wifi_sta_ctx.ap_info, 0, sizeof(g_wifi_sta_ctx.ap_info));
        g_wifi_sta_ctx.number = SCAN_LIST_SIZE;
        esp_err_t err = esp_wifi_scan_start(NULL, true);
        if(ESP_OK == err)
        {
            esp_wifi_scan_get_ap_num(&g_wifi_sta_ctx.ap_count);
            esp_wifi_scan_get_ap_records(&g_wifi_sta_ctx.number, g_wifi_sta_ctx.ap_info);
            ESP_LOGI(TAG, "---- scan: %u APs (show %u) ----", g_wifi_sta_ctx.ap_count, g_wifi_sta_ctx.number);
            for(int i = 0; i < g_wifi_sta_ctx.number; i++)
            {
                ESP_LOGI(TAG, "%-32.32s  rssi=%4d  ch=%3d  auth=%d",
                       (char *)g_wifi_sta_ctx.ap_info[i].ssid,
                       g_wifi_sta_ctx.ap_info[i].rssi,
                       g_wifi_sta_ctx.ap_info[i].primary,
                       g_wifi_sta_ctx.ap_info[i].authmode);
            }
        }
        break;
    }
    default:
        break;
    }
}

T_WIFI_MGR_OPS *get_wifi_sta_ops(void)
{
    return &s_wifi_sta_ops;
}
#ifndef __BSP_WIFIMGR_H__
#define __BSP_WIFIMGR_H__

#include "esp_err.h"
#include "esp_wifi.h"
#include "freertos/idf_additions.h"

typedef enum _E_WIFI_MGR_EVENT
{
    WIFI_MGR_EVT_START = 0,

    WIFI_MGR_EVT_STA_START,
    WIFI_MGR_EVT_STA_GOT_IP,
    WIFI_MGR_EVT_STA_CONNECTED,
    WIFI_MGR_EVT_STA_DISCONNECTED,

    WIFI_MGR_EVT_10S_TIMER,

    WIFI_MGR_EVT_MAX,
}E_WIFI_MGR_EVENT;

typedef struct _T_WIFI_MGR_CFG
{
    char ssid[32];
    char pwd[64];
} T_WIFI_MGR_CFG;

typedef struct _T_WIFI_MGR_OPS
{
    wifi_mode_t  idf_mode;                                                                  // wifi mode
    TaskHandle_t task_handle;                                                               // task handle
    esp_err_t    (*start)(void);                                                            // wifi start
    esp_err_t    (*stop)(void);                                                             // wifi stop
    void         (*on_event)(E_WIFI_MGR_EVENT evt, void *user, void *event_data);           // wifi event callback
    esp_err_t    (*set_cfg)(const T_WIFI_MGR_CFG *apcfg, const T_WIFI_MGR_CFG *stacfg);     // set wifi config
    esp_err_t    (*change_cfg)(const T_WIFI_MGR_CFG *apcfg, const T_WIFI_MGR_CFG *stacfg);  // change wifi config
}T_WIFI_MGR_OPS;

esp_err_t bsp_wifimgr_init(wifi_mode_t mode);
esp_err_t bsp_wifimgr_start(void);
esp_err_t bsp_wifimgr_stop(void);
esp_err_t bsp_wifimgr_set_cfg(const char *mode, const char *ssid, const char *pwd);

#endif // __BSP_WIFIMGR_H__

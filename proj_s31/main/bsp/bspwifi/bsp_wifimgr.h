#ifndef __BSP_WIFIMGR_H__
#define __BSP_WIFIMGR_H__

#include "esp_err.h"
#include "esp_wifi.h"

typedef enum _E_WIFI_MGR_EVENT
{
    WIFI_MGR_EVT_START = 0,
    WIFI_MGR_EVT_MAX,
}E_WIFI_MGR_EVENT;

typedef struct _T_WIFI_MGR_OPS
{
    wifi_mode_t  idf_mode;
    esp_err_t (*start)(void);
    esp_err_t (*stop)(void);
    void (*on_event)(E_WIFI_MGR_EVENT evt, void *user);
}T_WIFI_MGR_OPS;

esp_err_t bsp_wifimgr_init(wifi_mode_t mode);
esp_err_t bsp_wifimgr_start(void);
esp_err_t bsp_wifimgr_stop(void);

#endif // __BSP_WIFIMGR_H__

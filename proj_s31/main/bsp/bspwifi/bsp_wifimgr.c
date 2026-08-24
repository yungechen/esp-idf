#include "bsp_wifimgr.h"
#include "esp_err.h"

extern T_WIFI_MGR_OPS *get_wifi_ap_ops(void);
extern T_WIFI_MGR_OPS *get_wifi_sta_ops(void);
extern T_WIFI_MGR_OPS *get_wifi_apsta_ops(void);

typedef struct _T_WIFI_MGR_CTX
{
    T_WIFI_MGR_OPS *ops;
}T_WIFI_MGR_CTX;

T_WIFI_MGR_CTX *g_wifi_mgr_ctx = NULL;

esp_err_t bsp_wifimgr_start()
{
    if(g_wifi_mgr_ctx == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    return g_wifi_mgr_ctx->ops->start();
}

esp_err_t bsp_wifimgr_stop()
{
    if(g_wifi_mgr_ctx == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    return g_wifi_mgr_ctx->ops->stop();
}

esp_err_t bsp_wifimgr_init(wifi_mode_t mode)
{
    g_wifi_mgr_ctx = (T_WIFI_MGR_CTX *)calloc(1, sizeof(T_WIFI_MGR_CTX));
    if(g_wifi_mgr_ctx == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    switch (mode)
    {
        case WIFI_MODE_AP:
            g_wifi_mgr_ctx->ops = get_wifi_ap_ops();
            break;
        case WIFI_MODE_STA:
            g_wifi_mgr_ctx->ops = get_wifi_sta_ops();
            break;
        case WIFI_MODE_APSTA:
            g_wifi_mgr_ctx->ops = get_wifi_apsta_ops();
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

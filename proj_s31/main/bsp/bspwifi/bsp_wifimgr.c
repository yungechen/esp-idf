#include <inttypes.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include "esp_log.h"
#include "esp_event.h"
#include "bsp_wifimgr.h"
#include "cJSON.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_netif_types.h"
#include "esp_wifi.h"
#include "esp_wifi_types_generic.h"

#define DEFAULT_SSID "TEST_ROUTER"
#define DEFAULT_PWD  "12345678"

extern T_WIFI_MGR_OPS *get_wifi_ap_ops(void);
extern T_WIFI_MGR_OPS *get_wifi_sta_ops(void);
extern T_WIFI_MGR_OPS *get_wifi_apsta_ops(void);

extern esp_err_t wifi_cmd_init(void);

#define WIFI_INFO_DATA_PATH "/mnt/config/wifi_info.json"
#define WIFI_INFO_DATA_DIR "/mnt/config"
const char *TAG = "bsp_wifimgr";

typedef struct _T_WIFI_MGR_CTX
{
    T_WIFI_MGR_OPS *ops;
    esp_netif_t *netif_sta;
    esp_netif_t *netif_ap;

    T_WIFI_MGR_CFG ap_cfg;    // AP config
    T_WIFI_MGR_CFG sta_cfg;   // STA config
    TaskHandle_t task_handle; // task handle
}T_WIFI_MGR_CTX;

T_WIFI_MGR_CTX *g_wifi_mgr_ctx = NULL;

/**
 * @brief Read WiFi Config from JSON File
 * if file not exist, return ESP_FAIL
 * 
 * @author Chen Yunge (chenyunge@roborock.com)
 * @param[out] ctx 
 * @return esp_err_t 
 * 
 * @warning 
 * @note 
 * @attention 
*/
esp_err_t bsp_wifimgr_readjson(T_WIFI_MGR_CTX *ctx)
{
    if(ctx == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(ctx->ap_cfg.ssid, DEFAULT_SSID, sizeof(ctx->ap_cfg.ssid) - 1);
    strncpy(ctx->ap_cfg.pwd, DEFAULT_PWD, sizeof(ctx->ap_cfg.pwd) - 1);
    strncpy(ctx->sta_cfg.ssid, DEFAULT_SSID, sizeof(ctx->sta_cfg.ssid) - 1);
    strncpy(ctx->sta_cfg.pwd, DEFAULT_PWD, sizeof(ctx->sta_cfg.pwd) - 1);

    FILE *fp = fopen(WIFI_INFO_DATA_PATH, "r");
    if(NULL == fp)
    {
        ESP_LOGI(TAG, "WiFi info file not exist");
        return ESP_FAIL;
    }

    char buf[512] = {0};
    size_t len = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);

    if(len <= 0)
    {
        return ESP_FAIL;
    }
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if(NULL == root)
    {
        ESP_LOGW(TAG, "Parse JSON failed: %s", cJSON_GetErrorPtr());
        return ESP_FAIL;
    }

    cJSON *ap_json_cfg = cJSON_GetObjectItem(root, "ap");
    cJSON *sta_json_cfg = cJSON_GetObjectItem(root, "sta");
    cJSON *ssid = NULL, *pwd = NULL;

    if(ap_json_cfg != NULL)
    {
        ssid = cJSON_GetObjectItem(ap_json_cfg, "ssid");
        pwd = cJSON_GetObjectItem(ap_json_cfg, "pwd");
        if(NULL != ssid && NULL != pwd)
        {
            strncpy(ctx->ap_cfg.ssid, ssid->valuestring, sizeof(ctx->ap_cfg.ssid) - 1);
            strncpy(ctx->ap_cfg.pwd, pwd->valuestring, sizeof(ctx->ap_cfg.pwd) - 1);
        }
    }

    if(sta_json_cfg != NULL)
    {
        ssid = cJSON_GetObjectItem(sta_json_cfg, "ssid");
        pwd = cJSON_GetObjectItem(sta_json_cfg, "pwd");
        if(NULL != ssid && NULL != pwd)
        {
            strncpy(ctx->sta_cfg.ssid, ssid->valuestring, sizeof(ctx->sta_cfg.ssid) - 1);
            strncpy(ctx->sta_cfg.pwd, pwd->valuestring, sizeof(ctx->sta_cfg.pwd) - 1);
        }
    }

    cJSON_Delete(root);
    root = NULL;

    return ESP_OK;
}

/**
 * @brief Write WiFi Config to JSON File
 *
 * @param[in] ctx
 * @return esp_err_t
 */
static esp_err_t bsp_wifimgr_writejson(T_WIFI_MGR_CTX *ctx)
{
    if(ctx == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if(root == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    cJSON *ap_json_cfg = cJSON_CreateObject();
    cJSON *sta_json_cfg = cJSON_CreateObject();
    if(ap_json_cfg == NULL || sta_json_cfg == NULL)
    {
        cJSON_Delete(root);
        cJSON_Delete(ap_json_cfg);
        cJSON_Delete(sta_json_cfg);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(ap_json_cfg, "ssid", ctx->ap_cfg.ssid);
    cJSON_AddStringToObject(ap_json_cfg, "pwd", ctx->ap_cfg.pwd);
    cJSON_AddStringToObject(sta_json_cfg, "ssid", ctx->sta_cfg.ssid);
    cJSON_AddStringToObject(sta_json_cfg, "pwd", ctx->sta_cfg.pwd);
    cJSON_AddItemToObject(root, "ap", ap_json_cfg);
    cJSON_AddItemToObject(root, "sta", sta_json_cfg);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if(json_str == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    FILE *fp = fopen(WIFI_INFO_DATA_PATH, "w");
    if(fp == NULL)
    {
        ESP_LOGE(TAG, "Failed to open %s for write", WIFI_INFO_DATA_PATH);
        cJSON_free(json_str);
        return ESP_FAIL;
    }

    size_t len = strlen(json_str);
    size_t written = fwrite(json_str, 1, len, fp);
    fclose(fp);
    cJSON_free(json_str);

    if(written != len)
    {
        ESP_LOGE(TAG, "Failed to write WiFi info file");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "WiFi info saved to %s", WIFI_INFO_DATA_PATH);
    return ESP_OK;
}

esp_err_t bsp_wifimgr_set_cfg(const char *mode, const char *ssid, const char *pwd)
{
    if(g_wifi_mgr_ctx == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if(mode == NULL || ssid == NULL || pwd == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    T_WIFI_MGR_CFG cfg = {0};
    strncpy(cfg.ssid, ssid, sizeof(cfg.ssid) - 1);
    strncpy(cfg.pwd, pwd, sizeof(cfg.pwd) - 1);

    if(strcmp(mode, "ap") == 0)
    {
        memcpy(&g_wifi_mgr_ctx->ap_cfg, &cfg, sizeof(T_WIFI_MGR_CFG));
        g_wifi_mgr_ctx->ops->change_cfg(&cfg, NULL);
    }
    else
    {
        memcpy(&g_wifi_mgr_ctx->sta_cfg, &cfg, sizeof(T_WIFI_MGR_CFG));
        g_wifi_mgr_ctx->ops->change_cfg(NULL, &cfg);
    }

    return bsp_wifimgr_writejson(g_wifi_mgr_ctx);
}

esp_err_t bsp_wifimgr_stop()
{
    if(g_wifi_mgr_ctx == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    return g_wifi_mgr_ctx->ops->stop();
}

static void wifi_mgr_idf_event_handler(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    T_WIFI_MGR_CTX *ctx = (T_WIFI_MGR_CTX *)arg;
    if(ctx == NULL)
    {
        return;
    }
    if(WIFI_EVENT == base)
    {
        switch(id)
        {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA start");
            ctx->ops->on_event(WIFI_MGR_EVT_STA_START, ctx, event_data);
            break;
        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "STA connected");
            ctx->ops->on_event(WIFI_MGR_EVT_STA_CONNECTED, ctx, event_data);
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGI(TAG, "STA disconnected");
            ctx->ops->on_event(WIFI_MGR_EVT_STA_DISCONNECTED, ctx, event_data);
            break;
        case WIFI_EVENT_AP_STACONNECTED:
            break;
        case WIFI_EVENT_FTM_REPORT:
            ctx->ops->on_event(WIFI_MGR_EVT_FTM_REPORT, ctx, event_data);
            break;
        default:
            break;
        }
    }
    else if(IP_EVENT == base)
    {
        if(id == IP_EVENT_STA_GOT_IP)
        {
            char ip_str[16] = {0};
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "STA got ip:%s", esp_ip4addr_ntoa(&event->ip_info.ip, ip_str, sizeof(ip_str)));
        }
        else if(id == IP_EVENT_STA_LOST_IP)
        {

        }
    }
}

/**
 * @brief set Wi-Fi manager mode and cfg
 * 
 * @author Chen Yunge (chenyunge@roborock.com)
 * @param[in] mode Wi-Fi mode: ap, sta, apsta
 * @return esp_err_t 
 * 
 * @warning 
 * @note 
 * @attention 
*/
esp_err_t bsp_wifimgr_init(wifi_mode_t mode)
{
    // create wifi config dir
    struct stat st;
    if(stat(WIFI_INFO_DATA_DIR, &st) == 0)
    {
        if(!S_ISDIR(st.st_mode))
        {
            ESP_LOGE(TAG, "WiFi config dir is not a directory");
            return ESP_FAIL;
        }
    }
    else if(mkdir(WIFI_INFO_DATA_DIR, 0755) != 0 && errno != EEXIST)
    {
        ESP_LOGE(TAG, "Failed to create WiFi config dir");
        return ESP_FAIL;
    }

    g_wifi_mgr_ctx = (T_WIFI_MGR_CTX *)calloc(1, sizeof(T_WIFI_MGR_CTX));
    if(g_wifi_mgr_ctx == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    switch (mode)
    {
        case WIFI_MODE_AP:
            g_wifi_mgr_ctx->netif_ap = esp_netif_create_default_wifi_ap();
            g_wifi_mgr_ctx->ops = get_wifi_ap_ops();
            break;
        case WIFI_MODE_STA:
            g_wifi_mgr_ctx->netif_sta = esp_netif_create_default_wifi_sta();
            g_wifi_mgr_ctx->ops = get_wifi_sta_ops();
            break;
        case WIFI_MODE_APSTA:
            g_wifi_mgr_ctx->netif_sta = esp_netif_create_default_wifi_sta();
            g_wifi_mgr_ctx->netif_ap = esp_netif_create_default_wifi_ap();
            g_wifi_mgr_ctx->ops = get_wifi_apsta_ops();
            break;
        default:
            free(g_wifi_mgr_ctx);
            g_wifi_mgr_ctx = NULL;
            return ESP_ERR_INVALID_ARG;
    }

    // init wifi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // init wifi cmd
    wifi_cmd_init();

    // stop power save
    esp_wifi_set_ps(WIFI_PS_NONE);

    // register event handler
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_mgr_idf_event_handler, g_wifi_mgr_ctx);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_mgr_idf_event_handler, g_wifi_mgr_ctx);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, wifi_mgr_idf_event_handler, g_wifi_mgr_ctx);


    // read wifi config from json file
    if(bsp_wifimgr_readjson(g_wifi_mgr_ctx) == ESP_ERR_INVALID_ARG)
    {
        return ESP_FAIL;
    }

    g_wifi_mgr_ctx->ops->set_cfg(&g_wifi_mgr_ctx->ap_cfg, &g_wifi_mgr_ctx->sta_cfg);

    // start wifi
    ESP_ERROR_CHECK(g_wifi_mgr_ctx->ops->start());

    return ESP_OK;
}

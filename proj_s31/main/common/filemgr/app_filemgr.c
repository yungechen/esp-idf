#include "app_filemgr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_littlefs.h"

static const char *TAG = "app_filemgr";

esp_err_t app_filemgr_mount(void)
{
    static bool is_mounted = false;
    if (is_mounted) {
        return ESP_OK;
    }

    esp_vfs_littlefs_conf_t conf = 
    {
        .base_path = APP_FILEMGR_MOUNT_PATH,
        .partition_label = APP_FILEMGR_PARTITION_LABEL,
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to mount LittleFS (%s)", esp_err_to_name(err));
        return err;
    }

    size_t total = 0, used = 0;
    if (esp_littlefs_info(APP_FILEMGR_PARTITION_LABEL, &total, &used) == ESP_OK) 
    {
        ESP_LOGI(TAG, "LittleFS mounted at %s (total=%u, used=%u)",
                 APP_FILEMGR_MOUNT_PATH, (unsigned)total, (unsigned)used);
    } 
    else 
    {
        ESP_LOGI(TAG, "LittleFS mounted at %s", APP_FILEMGR_MOUNT_PATH);
    }

    is_mounted = true;
    return ESP_OK;
}

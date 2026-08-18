#include "app_filemgr.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"

static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;
static const char *TAG = "app_filemgr";

esp_err_t app_filemgr_mount(void)
{
    if(s_wl_handle != WL_INVALID_HANDLE)
    {
        return ESP_OK;
    }

    esp_vfs_fat_mount_config_t cfg = 
    {
        .max_files = 32,
        .format_if_mount_failed = true,
    };

    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(APP_FILEMGR_MOUNT_PATH, APP_FILEMGR_PARTITION_LABEL, &cfg, &s_wl_handle);

    if (err != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

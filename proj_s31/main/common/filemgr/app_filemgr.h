#ifndef __APP_FILEMGR_H__
#define __APP_FILEMGR_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define APP_FILEMGR_MOUNT_PATH "/mnt"
#define APP_FILEMGR_PARTITION_LABEL "storage"
#define APP_FILEMGR_HISTORY_PATH APP_FILEMGR_MOUNT_PATH "/history.txt"

esp_err_t app_filemgr_mount(void);

#endif

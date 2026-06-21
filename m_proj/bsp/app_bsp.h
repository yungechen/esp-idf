#ifndef __APP_BSP_H__
#define __APP_BSP_H__

#include "esp_err.h"

typedef enum _E_AppBspTimer
{
    APP_BSP_TIMER_1MS = 0,          // 1ms
    APP_BSP_TIMER_10MS,             // 10ms
    APP_BSP_TIMER_100MS,            // 100ms
    APP_BSP_TIMER_1S,               // 1s
    APP_BSP_TIMER_MAX,
}E_AppBspTimer;

typedef void (*T_AppBspTimerCallback)(void);

typedef struct {
    const char *name;
    esp_err_t (*start)(void);
    esp_err_t (*stop)(void);
} T_AppBspModule;

esp_err_t app_bsp_init(void);
esp_err_t app_bsp_timer_register(E_AppBspTimer timer, T_AppBspTimerCallback callback);

#endif

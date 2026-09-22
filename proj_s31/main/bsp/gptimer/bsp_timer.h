#ifndef __BSP_TIMER_H__
#define __BSP_TIMER_H__

#include "portmacro.h"
#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

esp_err_t bsp_timer_init(void);

typedef enum _E_TIMER_TYPE
{
    TIMER_T_10MS = 0,
    TIMER_T_100MS,
    TIMER_T_1S,
    TIMER_T_MAX,
}E_TIMER_TYPE;

typedef BaseType_t (*on_timer_cb)(void *arg);
esp_err_t bsp_timer_register(E_TIMER_TYPE type, on_timer_cb func, void *arg);
esp_err_t bsp_timer_start(void);

#ifdef __cplusplus
}
#endif
#endif
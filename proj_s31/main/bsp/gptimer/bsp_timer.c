/**
 * @file     bsp_timer.c
 * @author Chen Yunge (chenyunge@roborock.com)
 * @brief Initialize the gptimer
 * @version 1.0.0
 * @date 2026-09-22
 * 
 * @copyright Copyright (c) 2026 Roborock All rights reserved.
 * 
 * @par 修改日志
 * <table>
 * <tr><th>Date    <th>Version <th>Author <th>description
 * <tr><td>2026-09-22 <td>{version} <td>Chen Yunge <td>{description}
 * </table>
*/
#include <stdint.h>
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/gptimer.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "bsp_timer.h"

#define TIMER_CB_TBL_MAX 20

typedef struct _T_TIMER_CB_TBL
{
    on_timer_cb cb;
    void *arg;
}T_TIMER_CB_TBL;

static gptimer_handle_t s_timer_handle = NULL;
static bool s_run_flag = false;
static uint8_t s_timer_cb_idx[TIMER_T_MAX] = {0};
static T_TIMER_CB_TBL s_timer_cb_tbl[TIMER_T_MAX][TIMER_CB_TBL_MAX] = {NULL};
static const char *TAG = "GPTIMER";

esp_err_t bsp_timer_register(E_TIMER_TYPE type, on_timer_cb func, void *arg)
{
    if(type >= TIMER_T_MAX || func == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if(s_timer_cb_idx[type] >= TIMER_CB_TBL_MAX)
    {
        return ESP_ERR_NO_MEM;
    }

    if(s_run_flag)
    {
        ESP_LOGE(TAG, "timer is running");
        return ESP_ERR_INVALID_STATE;
    }

    s_timer_cb_tbl[type][s_timer_cb_idx[type]].cb = func;
    s_timer_cb_tbl[type][s_timer_cb_idx[type]].arg = arg;
    s_timer_cb_idx[type]++;
    return ESP_OK;
}

static bool IRAM_ATTR on_alarm(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_data)
{
    static uint16_t cnt_100ms = 0, cnt_1s = 0;
    BaseType_t yield = pdFALSE;
    cnt_100ms++;
    cnt_1s++;

    //10ms
    for(uint8_t i = 0; i < s_timer_cb_idx[TIMER_T_10MS]; i++)
    {
        if(s_timer_cb_tbl[TIMER_T_10MS][i].cb == NULL)
        {
            break;
        }
        yield |= s_timer_cb_tbl[TIMER_T_10MS][i].cb(s_timer_cb_tbl[TIMER_T_10MS][i].arg);
    }

    // 100ms
    if(cnt_100ms % 10 == 0)
    {
        for(uint8_t i = 0; i < s_timer_cb_idx[TIMER_T_100MS]; i++)
        {
            if(s_timer_cb_tbl[TIMER_T_100MS][i].cb == NULL)
            {
                break;
            }
            yield |= s_timer_cb_tbl[TIMER_T_100MS][i].cb(s_timer_cb_tbl[TIMER_T_100MS][i].arg);
        }
        cnt_100ms = 0;
    }

    // 1s
    if(cnt_1s % 100 == 0)
    {
        for(uint8_t i = 0; i < s_timer_cb_idx[TIMER_T_1S]; i++)
        {
            if(s_timer_cb_tbl[TIMER_T_1S][i].cb == NULL)
            {
                break;
            }
            yield |= s_timer_cb_tbl[TIMER_T_1S][i].cb(s_timer_cb_tbl[TIMER_T_1S][i].arg);
        }
        cnt_1s = 0;
    }
    return yield == pdTRUE;
}

esp_err_t bsp_timer_start(void)
{
    s_run_flag = true;
    return gptimer_start(s_timer_handle);
}

esp_err_t bsp_timer_init(void)
{
    // init timer
    gptimer_config_t timer_config =
    {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, // 1MHz, 1 tick=1us
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &s_timer_handle));
    
    gptimer_event_callbacks_t cbs =
    {
        .on_alarm = on_alarm,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(s_timer_handle, &cbs, NULL));
    ESP_ERROR_CHECK(gptimer_enable(s_timer_handle));


    gptimer_alarm_config_t alarm_config =
    {
        .alarm_count = 10000, // period = 10ms
        .flags.auto_reload_on_alarm = true,
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(s_timer_handle, &alarm_config));

    return ESP_OK;
}


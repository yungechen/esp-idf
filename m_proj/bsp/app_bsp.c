#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"

#include "app_bsp.h"
#include "app_led.h"

static const char *TAG = "app_bsp";

static T_AppBspModule *m_modules[CONFIG_APP_BSP_MODULE];
static int m_module_count = 0;

typedef struct _T_AppBspTimer
{
    T_AppBspTimerCallback callback_tbls[CONFIG_APP_BSP_MODULE];     // 当前定时器所有的回调函数
    esp_timer_handle_t timer_handle;                                // 当前定时器的句柄
    E_AppBspTimer timer_id;                                         // 当前定时器的id
    int callback_cnt;                                               // 当前定时器所有的回调函数数量
    int dbg_cnt;
}T_AppBspTimer;

T_AppBspTimer m_timer[APP_BSP_TIMER_MAX];

static esp_err_t app_add_module(T_AppBspModule *module)
{
    if(m_module_count >= CONFIG_APP_BSP_MODULE)
    {
        ESP_LOGE(TAG, "Module queue is full");
        return ESP_ERR_NO_MEM;
    }

    if(module == NULL)
    {
        ESP_LOGE(TAG, "Module is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    m_modules[m_module_count++] = module;
    return ESP_OK;
}

static void timer_callback(void *arg)
{
    // 回调内容尽量短

    T_AppBspTimer *pTimer = (T_AppBspTimer *)arg;

    for(int i = 0; i < pTimer->callback_cnt; i++)
    {
        pTimer->callback_tbls[i]();
    }

    pTimer->dbg_cnt++;
    if(pTimer->dbg_cnt >= 1000)
    {
        pTimer->dbg_cnt = 0;
        ESP_LOGI(TAG, "Timer %d callback end", pTimer->timer_id);
    }
}

esp_err_t app_bsp_timer_register(E_AppBspTimer timer, T_AppBspTimerCallback callback)
{
    if(timer >= APP_BSP_TIMER_MAX)
    {
        ESP_LOGE(TAG, "Timer %d is not supported", timer);
        return ESP_ERR_INVALID_ARG;
    }

    T_AppBspTimer *pTimer = &m_timer[timer];

    if(pTimer->callback_cnt >= CONFIG_APP_BSP_MODULE)
    {
        ESP_LOGE(TAG, "Timer %d callback queue is full", timer);
        return ESP_ERR_NO_MEM;
    }
    pTimer->callback_tbls[pTimer->callback_cnt++] = callback;
    return ESP_OK;
}

esp_err_t app_bsp_timer_start(void)
{
    esp_timer_create_args_t timer_args = 
    {
        .callback = &timer_callback,
    };

    timer_args.arg = &m_timer[APP_BSP_TIMER_1MS];
    timer_args.name = "m_timer_1ms";
    esp_timer_create(&timer_args, &m_timer[APP_BSP_TIMER_1MS].timer_handle);
    m_timer[APP_BSP_TIMER_1MS].timer_id = APP_BSP_TIMER_1MS;

    timer_args.arg = &m_timer[APP_BSP_TIMER_10MS];
    timer_args.name = "m_timer_10ms";
    esp_timer_create(&timer_args, &m_timer[APP_BSP_TIMER_10MS].timer_handle);
    m_timer[APP_BSP_TIMER_10MS].timer_id = APP_BSP_TIMER_10MS;

    timer_args.arg = &m_timer[APP_BSP_TIMER_100MS];
    timer_args.name = "m_timer_100ms";
    esp_timer_create(&timer_args, &m_timer[APP_BSP_TIMER_100MS].timer_handle);
    m_timer[APP_BSP_TIMER_100MS].timer_id = APP_BSP_TIMER_100MS;

    timer_args.arg = &m_timer[APP_BSP_TIMER_1S];
    timer_args.name = "m_timer_1s";
    esp_timer_create(&timer_args, &m_timer[APP_BSP_TIMER_1S].timer_handle);
    m_timer[APP_BSP_TIMER_1S].timer_id = APP_BSP_TIMER_1S;
    
    esp_timer_start_periodic(m_timer[APP_BSP_TIMER_1MS].timer_handle, 1000);
    esp_timer_start_periodic(m_timer[APP_BSP_TIMER_10MS].timer_handle, 10000);
    esp_timer_start_periodic(m_timer[APP_BSP_TIMER_100MS].timer_handle, 100000);
    esp_timer_start_periodic(m_timer[APP_BSP_TIMER_1S].timer_handle, 1000000);   

    return ESP_OK;
}

static void app_bsp_startup_modules(void)
{
    esp_err_t ret;
    for(int i = 0; i < m_module_count; i++)
    {
        ret = m_modules[i]->start();
        if(ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Start module %s failed: %s", m_modules[i]->name, esp_err_to_name(ret));
        }
    }
}

esp_err_t app_bsp_init(void)
{
    esp_err_t ret;
#if CONFIG_APP_LED_SUPPORT
    ret = app_add_module(app_led_init());
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Add led module failed: %s", esp_err_to_name(ret));
    }
#endif



    // start up all modules
    app_bsp_startup_modules();

    app_bsp_timer_start();

    return ESP_OK;
}

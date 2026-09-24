/**
 * @file     osal.c
 * @author Chen Yunge (chenyunge@roborock.com)
 * @brief 实现系统监控等功能的基础函数
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

#include <stdlib.h>
#include <inttypes.h>
#include <time.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "osal.h"
#include "bsp_timer.h"

#define OSAL_TASK_ARRAY_OFFSET 4

static TaskHandle_t s_osal_task_handle = NULL;
static const char *TAG = "OSAL";

#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
static TaskStatus_t *s_prev_task_arr = NULL;
static UBaseType_t s_prev_count = 0;
static configRUN_TIME_COUNTER_TYPE s_prev_total_runtime = 0;
static bool s_prev_valid = false;
#endif

typedef enum _E_OSAL_TASK_MSGID
{
    OSAL_TASK_MSGID_START = 0,
    OSAL_TASK_MSGID_1S,
    OSAL_TASK_MSGID_END,
}E_OSAL_TASK_MSGID;

static const char *task_state_str(eTaskState state)
{
    switch(state)
    {
    case eRunning:
        return "Running";
    case eReady:
        return "Ready";
    case eBlocked:
        return "Blocked";
    case eSuspended:
        return "Suspended";
    case eDeleted:
        return "Deleted";
    default:
        return "Unknown";
    }
}

static const char *stack_mem_str(const void *stack_base)
{
    if(esp_ptr_external_ram(stack_base))
    {
        return "EXT";
    }
    if(esp_ptr_internal(stack_base))
    {
        return "INT";
    }
    return "UNK";
}

#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
/**
 * @brief calc task cpu usage percent in last sample window
 * 
 * @author Chen Yunge (chenyunge@roborock.com)
 * @param[in] cur current task status
 * @param[in] total_elapsed total run time delta
 * @param[out] cpu_percent cpu usage percent, valid only when return true
 * @return true if matched previous snapshot
*/
static bool calc_task_cpu_percent(const TaskStatus_t *cur,
                                  configRUN_TIME_COUNTER_TYPE total_elapsed,
                                  uint32_t *cpu_percent)
{
    UBaseType_t i = 0;
    configRUN_TIME_COUNTER_TYPE task_elapsed = 0;

    if(NULL == cur || NULL == cpu_percent || false == s_prev_valid || 0 == total_elapsed)
    {
        return false;
    }

    for(i = 0; i < s_prev_count; i++)
    {
        if(s_prev_task_arr[i].xHandle == cur->xHandle)
        {
            if(cur->ulRunTimeCounter >= s_prev_task_arr[i].ulRunTimeCounter)
            {
                task_elapsed = cur->ulRunTimeCounter - s_prev_task_arr[i].ulRunTimeCounter;
            }
            else
            {
                /* counter wrap */
                task_elapsed = cur->ulRunTimeCounter;
            }
            *cpu_percent = (uint32_t)((task_elapsed * 100UL) /
                                      (total_elapsed * CONFIG_FREERTOS_NUMBER_OF_CORES));
            return true;
        }
    }
    return false;
}

static void save_task_snapshot(TaskStatus_t *arr, UBaseType_t count,
                               configRUN_TIME_COUNTER_TYPE total_runtime)
{
    if(NULL != s_prev_task_arr)
    {
        free(s_prev_task_arr);
        s_prev_task_arr = NULL;
    }

    s_prev_task_arr = arr;
    s_prev_count = count;
    s_prev_total_runtime = total_runtime;
    s_prev_valid = (NULL != arr && count > 0);
}
#endif

/**
 * @brief print all task status and heap info
 * 
 * @author Chen Yunge (chenyunge@roborock.com)
 * 
 * @warning 
 * @note usStackHighWaterMark unit is StackType_t words
 * @attention EXT means stack in PSRAM, INT means internal RAM;
 *            CPU% is average over last mem_print interval (scheme B)
*/
void mem_print(void)
{
    UBaseType_t task_num = uxTaskGetNumberOfTasks() + OSAL_TASK_ARRAY_OFFSET;
    TaskStatus_t *task_status_arr = NULL;
    configRUN_TIME_COUNTER_TYPE total_runtime = 0;
    UBaseType_t count = 0;
    UBaseType_t i = 0;
    uint32_t hwm_bytes = 0;
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    configRUN_TIME_COUNTER_TYPE total_elapsed = 0;
    uint32_t cpu_percent = 0;
    bool has_cpu = false;
#endif

    task_status_arr = malloc(task_num * sizeof(TaskStatus_t));
    if(NULL == task_status_arr)
    {
        ESP_LOGE(TAG, "mem_print: no mem");
        return;
    }

    count = uxTaskGetSystemState(task_status_arr, task_num, &total_runtime);
    if(0 == count)
    {
        ESP_LOGE(TAG, "mem_print: uxTaskGetSystemState failed");
        free(task_status_arr);
        return;
    }

#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    if(s_prev_valid && total_runtime >= s_prev_total_runtime)
    {
        total_elapsed = total_runtime - s_prev_total_runtime;
    }
#endif

    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);

    char *time_str = calloc(64, sizeof(char));
    if (!time_str)
    {
        ESP_LOGE(TAG, "No memory for wav file path");
        return;
    }
    strftime(time_str, 64, "--------%Y%m%d_%H%M%S---------------", &t);

    ESP_LOGI(TAG, "%s", time_str);
    free(time_str);
    time_str = NULL;

    ESP_LOGI(TAG, "---- tasks (%u) ----", (unsigned)count);
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    ESP_LOGI(TAG, "%-16s %-10s %4s %8s %4s %5s %12s %12s",
             "Name", "State", "Prio", "HWM(B)", "Mem", "CPU%", "StackBase", "Handle");
#else
    ESP_LOGI(TAG, "%-16s %-10s %4s %8s %4s %12s %12s",
             "Name", "State", "Prio", "HWM(B)", "Mem", "StackBase", "Handle");
#endif

    for(i = 0; i < count; i++)
    {
        hwm_bytes = (uint32_t)task_status_arr[i].usStackHighWaterMark * sizeof(StackType_t);
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
        has_cpu = calc_task_cpu_percent(&task_status_arr[i], total_elapsed, &cpu_percent);
        if(has_cpu)
        {
            ESP_LOGI(TAG, "%-16s %-10s %4u %8" PRIu32 " %4s %4" PRIu32 "%% %12p %12p",
                     task_status_arr[i].pcTaskName,
                     task_state_str(task_status_arr[i].eCurrentState),
                     (unsigned)task_status_arr[i].uxCurrentPriority,
                     hwm_bytes,
                     stack_mem_str((void *)task_status_arr[i].pxStackBase),
                     cpu_percent,
                     (void *)task_status_arr[i].pxStackBase,
                     (void *)task_status_arr[i].xHandle);
        }
        else
        {
            ESP_LOGI(TAG, "%-16s %-10s %4u %8" PRIu32 " %4s %5s %12p %12p",
                     task_status_arr[i].pcTaskName,
                     task_state_str(task_status_arr[i].eCurrentState),
                     (unsigned)task_status_arr[i].uxCurrentPriority,
                     hwm_bytes,
                     stack_mem_str((void *)task_status_arr[i].pxStackBase),
                     "-",
                     (void *)task_status_arr[i].pxStackBase,
                     (void *)task_status_arr[i].xHandle);
        }
#else
        ESP_LOGI(TAG, "%-16s %-10s %4u %8" PRIu32 " %4s %12p %12p",
                 task_status_arr[i].pcTaskName,
                 task_state_str(task_status_arr[i].eCurrentState),
                 (unsigned)task_status_arr[i].uxCurrentPriority,
                 hwm_bytes,
                 stack_mem_str((void *)task_status_arr[i].pxStackBase),
                 (void *)task_status_arr[i].pxStackBase,
                 (void *)task_status_arr[i].xHandle);
#endif
    }

#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    /* keep current snapshot as previous for next interval */
    save_task_snapshot(task_status_arr, count, total_runtime);
    task_status_arr = NULL;
#else
    free(task_status_arr);
    task_status_arr = NULL;
#endif

    ESP_LOGI(TAG, "---- heap ----");
    ESP_LOGI(TAG, "free heap        : %" PRIu32, esp_get_free_heap_size());
    ESP_LOGI(TAG, "free internal    : %" PRIu32, esp_get_free_internal_heap_size());
    ESP_LOGI(TAG, "free spiram      : %u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "min free ever    : %" PRIu32, esp_get_minimum_free_heap_size());
    ESP_LOGI(TAG, "largest free blk : %u",
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
}

void osal_task(void *arg)
{
    uint32_t notify_bits = 0;
    uint8_t cnt_1s = 0;

    for(;;)
    {
        xTaskNotifyWait(0, 0xFFFFFFFF, &notify_bits, portMAX_DELAY);

        if(CHK_BIT(notify_bits, MSG_BIT(OSAL_TASK_MSGID_1S)))
        {
            cnt_1s++;
            if(cnt_1s >= 10)
            {
                mem_print();
                cnt_1s = 0;
            }
        }
    }
}

static BaseType_t osal_timer_cb(void *arg)
{
    BaseType_t ret = pdFALSE;
    E_TIMER_TYPE type = (E_TIMER_TYPE)arg;

    switch(type)
    {
    case TIMER_T_1S:
        xTaskNotifyFromISR(s_osal_task_handle, MSG_BIT(OSAL_TASK_MSGID_1S), eSetBits, &ret);
        break;
    default:
        break;
    }
    return ret;
}

void osal_init(void)
{
    xTaskCreate(osal_task, "osal_task", 4096, NULL, 2, &s_osal_task_handle);
    bsp_timer_register(TIMER_T_1S, osal_timer_cb, (void *)TIMER_T_1S);
}

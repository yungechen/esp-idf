# WiFi STA 初始化实施方案

> 状态：待评审
> 日期：2026-08-19
> 涉及目录：`proj_s31/main/`

## 背景与目标

在现有 `bsp_wifimgr` 框架（`T_WIFI_MGR_OPS` 虚表按 `wifi_mode_t` 分发）下实现 WiFi STA 初始化。
配置来源为 console 命令写入，wifimgr 将配置持久化为 JSON 文件；开机读取该 JSON，
读取失败则进入等待配置状态。断线重连策略：重试 5 次后停止并上报事件。

已确认的设计决策：

- 配置文件路径：`/data/wifi_cfg.json`，格式 `{"ssid":"...","password":"..."}`
- console 命令形式：`wifi_cfg <ssid> <password>`（位置参数，均必填）
- 重连策略：最多重试 5 次，耗尽后停止并上报 `WIFI_MGR_EVT_DISCONNECTED`

## 改动总览

| # | 文件 | 动作 | 原因 |
|---|------|------|------|
| 1 | `proj_s31/main/CMakeLists.txt` | 修改 | 加 `esp_wifi`/`json` 依赖、登记新源文件 |
| 2 | `proj_s31/main/bsp/bspwifi/bsp_wifimgr.h` | 修改 | 扩展事件枚举、ops 增加 `set_config`、新增配置 API 声明 |
| 3 | `proj_s31/main/bsp/bspwifi/bsp_wifimgr.c` | 修改 | 实现 JSON 配置读写、配置校验与下发 |
| 4 | `proj_s31/main/bsp/bspwifi/bsp_wifista.c` | 修改 | STA 完整初始化、事件处理、5 次重连 |
| 5 | `proj_s31/main/cmd_wifi.c` / `cmd_wifi.h` | 新增 | console 命令 `wifi_cfg <ssid> <password>` |
| 6 | `proj_s31/main/app_main.c` | 修改 | 注册 wifi 命令（一行调用 + include） |

`bsp_wifiap.c` / `bsp_wifiapsta.c` 使用指定初始化器，新增的 `set_config` 成员自动为 NULL，
`bsp_wifimgr_set_sta_config` 中已判空，**这两个文件不用改**。

---

## 1. `main/CMakeLists.txt`

**原因**：STA 代码用到 `esp_wifi` 组件 API，JSON 序列化用到 ESP-IDF 自带 `json`（cJSON）组件；`cmd_wifi.c` 需登记编译。

```cmake
idf_component_register(SRCS "app_main.c"
                            "cmd_ethernet.c"
                            "cmd_wifi.c"
                            ${COMMON_SRCS}
                            ${BSP_SRCS}
                       INCLUDE_DIRS "."
                                     ${COMMON_INCLUDE_DIRS}
                                     ${BSP_INCLUDE_DIRS}
                       PRIV_REQUIRES fatfs esp_netif esp_eth esp_wifi console nvs_flash spi_flash esp_driver_uart esp_driver_gpio json)
```

> 注：`wear_levelling` 是 `app_filemgr.c` 用到的，目前靠 `fatfs` 传递依赖已能编过，本次不动。

## 2. `bsp/bspwifi/bsp_wifimgr.h`

**原因**：

1. 上层需要感知连接状态（等配置/已连接/拿到 IP/断线），现有枚举只有 `START`；
2. console 写入的配置要通过 ops 下发到具体模式，ops 需要 `set_config` 成员；
3. 配置结构体和读写/事件上报 API 需要对外声明（`bsp_wifista.c` 要调 `load` 和 `post_event`）。

```c
#ifndef __BSP_WIFIMGR_H__
#define __BSP_WIFIMGR_H__

#include "esp_err.h"
#include "esp_wifi.h"

#define WIFI_MGR_CFG_FILE_PATH    "/data/wifi_cfg.json"
#define WIFI_MGR_CONN_MAX_RETRY   5

typedef enum _E_WIFI_MGR_EVENT
{
    WIFI_MGR_EVT_START = 0,
    WIFI_MGR_EVT_WAIT_CFG,      // 无有效配置，等待 console 写入
    WIFI_MGR_EVT_CONNECTED,
    WIFI_MGR_EVT_GOT_IP,
    WIFI_MGR_EVT_DISCONNECTED,  // 含重连耗尽后的最终断线
    WIFI_MGR_EVT_MAX,
}E_WIFI_MGR_EVENT;

typedef struct _T_WIFI_MGR_CFG
{
    char ssid[32];       // 最长 31 + '\0'
    char password[64];   // 最长 63 + '\0'
}T_WIFI_MGR_CFG;

typedef struct _T_WIFI_MGR_OPS
{
    wifi_mode_t  idf_mode;
    esp_err_t (*start)(void);
    esp_err_t (*stop)(void);
    esp_err_t (*set_config)(const char *ssid, const char *password); // AP/APSTA 暂置 NULL
    void (*on_event)(E_WIFI_MGR_EVENT evt, void *user);
}T_WIFI_MGR_OPS;

esp_err_t bsp_wifimgr_init(wifi_mode_t mode);
esp_err_t bsp_wifimgr_start(void);
esp_err_t bsp_wifimgr_stop(void);

/* console 命令入口：校验 -> 存 JSON -> 下发到当前模式 */
esp_err_t bsp_wifimgr_set_sta_config(const char *ssid, const char *password);

/* 供模式实现（bsp_wifista.c）使用 */
esp_err_t bsp_wifimgr_load_cfg(T_WIFI_MGR_CFG *cfg);
esp_err_t bsp_wifimgr_save_cfg(const T_WIFI_MGR_CFG *cfg);
void      bsp_wifimgr_post_event(E_WIFI_MGR_EVENT evt, void *user);

#endif // __BSP_WIFIMGR_H__
```

## 3. `bsp/bspwifi/bsp_wifimgr.c`

**原因**：

1. 实现 `/data/wifi_cfg.json` 的读写（FATFS 已由 `app_filemgr_mount()` 挂载，直接用标准 stdio）；
2. `set_sta_config` 做参数校验（避免非法配置写进 flash）；
3. `post_event` 让模式层能上报事件；
4. 顺带补两个小问题：`init` 参数非法时内存泄漏、`start/stop` 未判空 ops。

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bsp_wifimgr.h"
#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "bsp_wifimgr";

extern T_WIFI_MGR_OPS *get_wifi_ap_ops(void);
extern T_WIFI_MGR_OPS *get_wifi_sta_ops(void);
extern T_WIFI_MGR_OPS *get_wifi_apsta_ops(void);

typedef struct _T_WIFI_MGR_CTX
{
    T_WIFI_MGR_OPS *ops;
}T_WIFI_MGR_CTX;

static T_WIFI_MGR_CTX *g_wifi_mgr_ctx = NULL;

esp_err_t bsp_wifimgr_load_cfg(T_WIFI_MGR_CFG *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *f = fopen(WIFI_MGR_CFG_FILE_PATH, "r");
    if (f == NULL) {
        ESP_LOGW(TAG, "cfg file %s not found", WIFI_MGR_CFG_FILE_PATH);
        return ESP_ERR_NOT_FOUND;
    }

    char buf[256] = {0};
    size_t len = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (len == 0) {
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        ESP_LOGE(TAG, "cfg file json parse failed");
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_FAIL;
    cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    cJSON *password = cJSON_GetObjectItem(root, "password");
    if (cJSON_IsString(ssid) && strlen(ssid->valuestring) > 0
        && strlen(ssid->valuestring) < sizeof(cfg->ssid)
        && cJSON_IsString(password)
        && strlen(password->valuestring) < sizeof(cfg->password)) {
        strlcpy(cfg->ssid, ssid->valuestring, sizeof(cfg->ssid));
        strlcpy(cfg->password, password->valuestring, sizeof(cfg->password));
        ret = ESP_OK;
    } else {
        ESP_LOGE(TAG, "cfg file content invalid");
    }

    cJSON_Delete(root);
    return ret;
}

esp_err_t bsp_wifimgr_save_cfg(const T_WIFI_MGR_CFG *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "ssid", cfg->ssid);
    cJSON_AddStringToObject(root, "password", cfg->password);
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json_str == NULL) {
        return ESP_ERR_NO_MEM;
    }

    FILE *f = fopen(WIFI_MGR_CFG_FILE_PATH, "w");
    if (f == NULL) {
        free(json_str);
        ESP_LOGE(TAG, "open %s for write failed", WIFI_MGR_CFG_FILE_PATH);
        return ESP_FAIL;
    }
    fputs(json_str, f);
    fclose(f);
    free(json_str);

    ESP_LOGI(TAG, "cfg saved to %s (ssid=%s)", WIFI_MGR_CFG_FILE_PATH, cfg->ssid);
    return ESP_OK;
}

void bsp_wifimgr_post_event(E_WIFI_MGR_EVENT evt, void *user)
{
    if (g_wifi_mgr_ctx != NULL && g_wifi_mgr_ctx->ops != NULL
        && g_wifi_mgr_ctx->ops->on_event != NULL) {
        g_wifi_mgr_ctx->ops->on_event(evt, user);
    }
}

esp_err_t bsp_wifimgr_set_sta_config(const char *ssid, const char *password)
{
    size_t ssid_len = (ssid != NULL) ? strlen(ssid) : 0;
    size_t pwd_len = (password != NULL) ? strlen(password) : 0;

    // ssid 1~31 字节；password 为空（开放网络）或 8~63 字节（WPA/WPA2）
    if (ssid_len == 0 || ssid_len >= sizeof(((T_WIFI_MGR_CFG *)0)->ssid)
        || (pwd_len > 0 && (pwd_len < 8 || pwd_len >= sizeof(((T_WIFI_MGR_CFG *)0)->password)))) {
        return ESP_ERR_INVALID_ARG;
    }
    if (g_wifi_mgr_ctx == NULL || g_wifi_mgr_ctx->ops == NULL
        || g_wifi_mgr_ctx->ops->set_config == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    T_WIFI_MGR_CFG cfg = {0};
    strlcpy(cfg.ssid, ssid, sizeof(cfg.ssid));
    strlcpy(cfg.password, (password != NULL) ? password : "", sizeof(cfg.password));

    esp_err_t err = bsp_wifimgr_save_cfg(&cfg);   // 先落盘，保证重启可用
    if (err != ESP_OK) {
        return err;
    }
    return g_wifi_mgr_ctx->ops->set_config(ssid, password);
}

esp_err_t bsp_wifimgr_start(void)
{
    if (g_wifi_mgr_ctx == NULL || g_wifi_mgr_ctx->ops == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return g_wifi_mgr_ctx->ops->start();
}

esp_err_t bsp_wifimgr_stop(void)
{
    if (g_wifi_mgr_ctx == NULL || g_wifi_mgr_ctx->ops == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return g_wifi_mgr_ctx->ops->stop();
}

esp_err_t bsp_wifimgr_init(wifi_mode_t mode)
{
    g_wifi_mgr_ctx = (T_WIFI_MGR_CTX *)calloc(1, sizeof(T_WIFI_MGR_CTX));
    if (g_wifi_mgr_ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }

    switch (mode) {
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
            free(g_wifi_mgr_ctx);          // 修复：原代码此处泄漏
            g_wifi_mgr_ctx = NULL;
            return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}
```

## 4. `bsp/bspwifi/bsp_wifista.c`

**原因**：核心实现。要点：

1. netif/event loop/wifi 驱动的一次性初始化；
2. start 时尝试读 JSON，无配置则进入 `WAIT_CFG` 不发起连接；
3. `STA_DISCONNECTED` 重连，上限 `WIFI_MGR_CONN_MAX_RETRY`（5 次），耗尽后停止并上报 `DISCONNECTED`；
4. `set_config` 支持运行中切换配置（先 disconnect 再 connect）。

```c
#include <string.h>
#include "bsp_wifimgr.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

static const char *TAG = "bsp_wifista";

typedef enum _E_STA_STATE
{
    STA_STATE_IDLE = 0,
    STA_STATE_WAIT_CFG,
    STA_STATE_CONNECTING,
    STA_STATE_CONNECTED,
}E_STA_STATE;

static esp_netif_t *s_sta_netif = NULL;
static bool s_wifi_inited = false;
static bool s_has_cfg = false;
static E_STA_STATE s_state = STA_STATE_IDLE;
static int s_retry_cnt = 0;

static void bsp_wifi_sta_event_handler(void *arg, esp_event_base_t event_base,
                                       int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (s_has_cfg) {
            s_state = STA_STATE_CONNECTING;
            esp_wifi_connect();
        } else {
            s_state = STA_STATE_WAIT_CFG;
            ESP_LOGW(TAG, "no wifi config, use 'wifi_cfg <ssid> <password>' to configure");
            bsp_wifimgr_post_event(WIFI_MGR_EVT_WAIT_CFG, NULL);
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_state = STA_STATE_CONNECTING;
        if (s_retry_cnt < WIFI_MGR_CONN_MAX_RETRY) {
            s_retry_cnt++;
            ESP_LOGW(TAG, "disconnected, retry %d/%d", s_retry_cnt, WIFI_MGR_CONN_MAX_RETRY);
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "retry exhausted, give up");
            bsp_wifimgr_post_event(WIFI_MGR_EVT_DISCONNECTED, event_data);
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        s_retry_cnt = 0;
        bsp_wifimgr_post_event(WIFI_MGR_EVT_CONNECTED, event_data);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        s_state = STA_STATE_CONNECTED;
        s_retry_cnt = 0;
        ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&event->ip_info.ip));
        bsp_wifimgr_post_event(WIFI_MGR_EVT_GOT_IP, event_data);
    }
}

static esp_err_t bsp_wifi_sta_start(void)
{
    if (!s_wifi_inited) {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        s_sta_netif = esp_netif_create_default_wifi_sta();

        wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                   bsp_wifi_sta_event_handler, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                   bsp_wifi_sta_event_handler, NULL));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
        s_wifi_inited = true;
    }

    // 开机读取持久化配置，读不到则启动驱动后等待 console 配置
    T_WIFI_MGR_CFG cfg = {0};
    if (bsp_wifimgr_load_cfg(&cfg) == ESP_OK) {
        wifi_config_t wifi_cfg = {0};
        strlcpy((char *)wifi_cfg.sta.ssid, cfg.ssid, sizeof(wifi_cfg.sta.ssid));
        strlcpy((char *)wifi_cfg.sta.password, cfg.password, sizeof(wifi_cfg.sta.password));
        wifi_cfg.sta.threshold.authmode =
            (strlen(cfg.password) > 0) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
        wifi_cfg.sta.pmf_cfg.capable = true;
        wifi_cfg.sta.pmf_cfg.required = false;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
        s_has_cfg = true;
        ESP_LOGI(TAG, "cfg loaded, ssid=%s", cfg.ssid);
    }

    s_retry_cnt = 0;
    ESP_ERROR_CHECK(esp_wifi_start());
    bsp_wifimgr_post_event(WIFI_MGR_EVT_START, NULL);
    return ESP_OK;
}

static esp_err_t bsp_wifi_sta_stop(void)
{
    if (!s_wifi_inited) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_wifi_disconnect();
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                 bsp_wifi_sta_event_handler));
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                 bsp_wifi_sta_event_handler));
    ESP_ERROR_CHECK(esp_wifi_deinit());
    if (s_sta_netif != NULL) {
        esp_netif_destroy_default_wifi(s_sta_netif);
        s_sta_netif = NULL;
    }

    s_wifi_inited = false;
    s_has_cfg = false;
    s_state = STA_STATE_IDLE;
    s_retry_cnt = 0;
    return ESP_OK;
}

static esp_err_t bsp_wifi_sta_set_config(const char *ssid, const char *password)
{
    if (!s_wifi_inited) {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_config_t wifi_cfg = {0};
    strlcpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password, (password != NULL) ? password : "",
            sizeof(wifi_cfg.sta.password));
    wifi_cfg.sta.threshold.authmode =
        (password != NULL && strlen(password) > 0) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wifi_cfg.sta.pmf_cfg.capable = true;
    wifi_cfg.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    s_has_cfg = true;
    s_retry_cnt = 0;

    if (s_state == STA_STATE_WAIT_CFG) {
        s_state = STA_STATE_CONNECTING;
        return esp_wifi_connect();
    }

    // 已有连接/正在连接：断开重连到新配置
    s_state = STA_STATE_CONNECTING;
    esp_wifi_disconnect();
    return esp_wifi_connect();
}

static void bsp_wifi_sta_on_event(E_WIFI_MGR_EVENT evt, void *user)
{
    // 预留：当前仅打日志，后续可通知 app 层
    ESP_LOGD(TAG, "sta event %d", evt);
}

static T_WIFI_MGR_OPS s_wifi_sta_ops = {
    .idf_mode = WIFI_MODE_STA,
    .start = bsp_wifi_sta_start,
    .stop = bsp_wifi_sta_stop,
    .set_config = bsp_wifi_sta_set_config,
    .on_event = bsp_wifi_sta_on_event,
};

T_WIFI_MGR_OPS *get_wifi_sta_ops(void)
{
    return &s_wifi_sta_ops;
}
```

## 5. 新增 `main/cmd_wifi.h` / `main/cmd_wifi.c`

**原因**：console 配置入口，仿照 `cmd_ethernet.c` 的 argtable3 写法；两个位置参数均必填（`arg_str1`），
开放网络可传占位串或后续扩展为可选参数。

`cmd_wifi.h`：

```c
/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Register WiFi commands
void register_wifi_commands(void);

#ifdef __cplusplus
}
#endif
```

`cmd_wifi.c`：

```c
/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>
#include "cmd_wifi.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "bsp_wifimgr.h"

/* "wifi_cfg" command: wifi_cfg <ssid> <password> */
static struct {
    struct arg_str *ssid;
    struct arg_str *password;
    struct arg_end *end;
} wifi_cfg_args;

static int wifi_cfg_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&wifi_cfg_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, wifi_cfg_args.end, argv[0]);
        return 1;
    }

    esp_err_t err = bsp_wifimgr_set_sta_config(wifi_cfg_args.ssid->sval[0],
                                               wifi_cfg_args.password->sval[0]);
    if (err != ESP_OK) {
        printf("wifi config failed: %s\r\n", esp_err_to_name(err));
        return 1;
    }

    printf("wifi config saved, connecting to %s ...\r\n", wifi_cfg_args.ssid->sval[0]);
    return 0;
}

void register_wifi_commands(void)
{
    wifi_cfg_args.ssid = arg_str1(NULL, NULL, "<ssid>", "SSID of AP");
    wifi_cfg_args.password = arg_str1(NULL, NULL, "<password>", "password of AP");
    wifi_cfg_args.end = arg_end(2);
    const esp_console_cmd_t cmd = {
        .command = "wifi_cfg",
        .help = "Set WiFi STA config, save to flash and connect",
        .hint = NULL,
        .func = wifi_cfg_cmd,
        .argtable = &wifi_cfg_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
```

## 6. `main/app_main.c`

**原因**：注册新命令，位置与 `register_ethernet_commands()` 一致（`cmdmgr_init()` 之后）。

```c
#include "cmd_wifi.h"        // 顶部 include 区新增

    /* Register commands */
    register_ethernet_commands();
    register_wifi_commands();          // 新增
```

---

## 验证步骤

1. `cd proj_s31 && idf.py build` 编译通过；
2. 烧录后首次启动：无 `/data/wifi_cfg.json` → 串口提示 `use 'wifi_cfg <ssid> <password>' to configure`；
3. 输入 `wifi_cfg myssid mypassword` → 提示已保存并开始连接 → 打印 `got ip:`；
4. 重启 → 自动读 JSON 连接，无需再次配置；
5. 输错密码 → 观察重试 5 次后打印 `retry exhausted, give up`。

## 实现细节说明

- **事件线程**：`on_event` 回调运行在 wifi 事件任务上下文，目前只打日志是安全的；
  以后若要做重活需转到别的任务。
- **password 必填**：按确认的 `wifi_cfg <ssid> <password>` 两参数实现；连接开放网络时可传任意
  8 位以上占位串，或后续把 password 改成可选参数。
- **公共初始化位置**：`esp_netif_init` / `esp_event_loop_create_default` / `esp_wifi_init`
  目前放在 `bsp_wifista.c` 内（带 `s_wifi_inited` 防重复）。后续实现 AP/APSTA 模式时，
  这部分应上提到 `bsp_wifimgr.c` 公共层。

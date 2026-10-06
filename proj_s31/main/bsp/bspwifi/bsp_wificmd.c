#include <stdio.h>
#include <string.h>
#include "esp_console.h"
#include "esp_err.h"
#include "argtable3/argtable3.h"
#include "bsp_wifimgr.h"

typedef struct _T_WIFI_CMD_ARGS
{
    struct arg_str *mode;
    struct arg_str *ssid;
    struct arg_str *pwd;
    struct arg_str *bssid; /* SoftAP ssid */
    struct arg_str *bpwd;  /* SoftAP password */
    struct arg_end *end;
}T_WIFI_CMD_ARGS;

static T_WIFI_CMD_ARGS s_wifi_args;

static bool is_empty_arg(const char *s)
{
    return (s == NULL || s[0] == '\0' || strcmp(s, "-") == 0);
}

static int cmd_wifi(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&s_wifi_args);

    if(nerrors != 0)
    {
        arg_print_errors(stderr, s_wifi_args.end, argv[0]);
        return 1;
    }

    const char *mode = s_wifi_args.mode->sval[0];
    const char *ssid = s_wifi_args.ssid->sval[0];
    const char *pwd = s_wifi_args.pwd->sval[0];
    const char *bssid = s_wifi_args.bssid->sval[0];
    const char *bpwd = s_wifi_args.bpwd->sval[0];

    if(strcmp(mode, "sta") != 0 &&
       strcmp(mode, "ap") != 0 &&
       strcmp(mode, "apsta") != 0)
    {
        printf("valid mode: sta|ap|apsta, Invalid mode: %s\n", mode);
        return 1;
    }

    esp_err_t err = ESP_OK;

    /* 按 mode 校验必填项 */
    if ((strcmp(mode, "sta") == 0 || strcmp(mode, "ap") == 0) && !is_empty_arg(ssid)) 
    {
        if(!is_empty_arg(pwd))
        {
            err = bsp_wifimgr_set_cfg(mode, ssid, pwd);
        }
        else
        {
            printf("pwd required for %s\r\n", mode);
            return 1;
        }
    }

    if(strcmp(mode, "apsta") == 0)
    {
        if(!is_empty_arg(ssid) && !is_empty_arg(bssid))
        {
            err = bsp_wifimgr_set_cfg("sta", ssid, pwd);
            err = bsp_wifimgr_set_cfg("ap", bssid, bpwd);
        }
        else
        {
            printf("ssid and bssid required for apsta\r\n");
            return 1;
        }
    }

    return 0;
}

esp_err_t wifi_cmd_init(void)
{
    s_wifi_args.mode  = arg_str1(NULL, NULL, "<mode>",  "sta|ap|apsta");
    s_wifi_args.ssid  = arg_str1(NULL, NULL, "<ssid>",  "STA SSID, use - if unused");
    s_wifi_args.pwd   = arg_str1(NULL, NULL, "<key>",   "STA password, use - if unused");
    s_wifi_args.bssid = arg_str1(NULL, NULL, "<bssid>", "SoftAP SSID, use - if unused");
    s_wifi_args.bpwd  = arg_str1(NULL, NULL, "<bkey>",  "SoftAP password, use - if unused");
    s_wifi_args.end   = arg_end(5);
    const esp_console_cmd_t cmd = {
        .command  = "wifi",
        .help     = "wifi <mode> <ssid> <pwd> <bssid> <bpwd>",
        .hint     = NULL,
        .func     = &cmd_wifi,
        .argtable = &s_wifi_args,
    };
    return esp_console_cmd_register(&cmd);
}

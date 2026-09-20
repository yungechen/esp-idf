#include <stdio.h>
#include <string.h>
#include "esp_console.h"
#include "esp_err.h"
#include "argtable3/argtable3.h"
#include "app_ftpsrv.h"

typedef struct _T_FTPSRV_CMD_ARGS
{
    struct arg_str *cmd;
    struct arg_end *end;
}T_FTPSRV_CMD_ARGS;

static T_FTPSRV_CMD_ARGS s_ftpsrv_args;

static bool is_empty_arg(const char *s)
{
    return (s == NULL || s[0] == '\0' || strcmp(s, "-") == 0);
}

static int cmd_ftpsrv(int argc, char **argv)
{
    if(argc < 2)
    {
        printf("Invalid arguments\n");
        printf("Usage: ftpsrv <cmd>\n");
        printf("  start <port> <user> <pass> <root> <name>: start FTP server\n");
        printf("  stop <srv_idx>: stop FTP server\n");
        printf("  help: show this help message\n");
        return 1;
    }

    if(strcmp(argv[1], "start") == 0)
    {// ftpsrv start 21 esp32 esp32 /data ftpsrv1
        if(argc < 6)
        {
            printf("Invalid arguments\n");
            printf("Usage: ftpsrv start <port> <user> <pass> <root> <name>\n");
            return 1;
        }

        T_AppFtpSrvCfg cfg = 
        {
            .port = atoi(argv[2]),
            .user = argv[3],
            .pass = argv[4],
            .root = argv[5],
            .name = argc > 6 ? argv[6] : NULL,
        };
        esp_err_t err = ftp_server_start(&cfg);
        if(err != ESP_OK)
        {
            printf("Failed to start FTP server: %s\n", esp_err_to_name(err));
            return 1;
        }
        printf("FTP server started on port %d index is %d\n", cfg.port, cfg.srv_idx);
        return 0;
    }
    else if(strcmp(argv[1], "stop") == 0)
    {
        if(argc < 3)
        {
            printf("Invalid arguments\n");
            printf("Usage: ftpsrv stop <srv_idx>\n");
            return 1;
        }
        int index = atoi(argv[2]);
        if(index < 0)
        {
            printf("Invalid arguments\n");
            printf("Usage: ftpsrv stop <srv_idx>\n");
            return 1;
        }

        esp_err_t err = ftp_server_stop(index);
        if(err != ESP_OK)
        {
            printf("Failed to stop FTP server: %s\n", esp_err_to_name(err));
            return 1;
        }
        printf("FTP server stopped on index %d\n", index);
        return 0;
    }
    else if(strcmp(argv[1], "help") == 0)
    {
        printf("ftpsrv <cmd>\n");
        printf("  start <port> <user> <pass> <root>: start FTP server\n");
        printf("  stop <srv_idx>: stop FTP server\n");
        printf("  help: show this help message\n");
        return 0;
    }
    return 1;
}

esp_err_t ftpsrv_cmd_init(void)
{
    s_ftpsrv_args.cmd   = arg_str1(NULL, NULL, "<cmd>", "start|stop|help");
    s_ftpsrv_args.end   = arg_end(2);
    const esp_console_cmd_t cmd = {
        .command  = "ftpsrv",
        .help     = "ftpsrv <cmd>",
        .hint     = NULL,
        .func     = &cmd_ftpsrv,
        .argtable = &s_ftpsrv_args, 
    };
    return esp_console_cmd_register(&cmd);
}
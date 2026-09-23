#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "esp_console.h"
#include "esp_err.h"
#include "argtable3/argtable3.h"
#include "bsp_audiomgr.h"

typedef struct _T_AUDIO_CMD_ARGS
{
    struct arg_str *time;
    struct arg_end *end;
}T_AUDIO_CMD_ARGS;

static T_AUDIO_CMD_ARGS s_audio_args;

static bool is_empty_arg(const char *s)
{
    return (s == NULL || s[0] == '\0' || strcmp(s, "-") == 0);
}

static int cmd_audio(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&s_audio_args);

    if(nerrors != 0)
    {
        arg_print_errors(stderr, s_audio_args.end, argv[0]);
        return 1;
    }

    const char *time = s_audio_args.time->sval[0];
    int time_val = atoi(time);

    if(time_val <= 0)
    {
        printf("time must be greater than 0\r\n");
        return 1;
    }

    if(audio_mic_start(time_val) != ESP_OK)
    {
        printf("audio mic start failed\r\n");
        return 1;
    }

    return 0;
}

esp_err_t audio_cmd_init(void)
{
    s_audio_args.time = arg_str1(NULL, NULL, "<time>", "time in seconds");
    s_audio_args.end = arg_end(2);

    const esp_console_cmd_t audio_cmd = {
        .command = "audio",
        .help = "Audio command",
        .hint = NULL,
        .func = &cmd_audio,
    };

    return esp_console_cmd_register(&audio_cmd);
}


/* Console commands for browsing the /mnt filesystem (ls/cd/pwd).

   The console prompt is fixed at startup, so a current-working-directory
   state is kept here and cd/pwd operate on it. All paths are jailed
   under APP_FILEMGR_MOUNT_PATH (/mnt): ".." at the root stays at the root.

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "app_filemgr.h"
#include "cmd_filemgr.h"

#define FILEMGR_PATH_MAX 256

static const char *TAG = "cmd_filemgr";

static char s_cwd[FILEMGR_PATH_MAX] = APP_FILEMGR_MOUNT_PATH;

static struct {
    struct arg_lit *long_fmt;
    struct arg_str *path;
    struct arg_end *end;
} ls_args;

static struct {
    struct arg_str *path;
    struct arg_end *end;
} cd_args;

/* Normalize `arg` (absolute under /mnt, or relative to `cwd`) into an
 * absolute path under APP_FILEMGR_MOUNT_PATH, resolving "." and "..".
 * ".." at the mount root stays at the root (jail). Returns false if the
 * input is an absolute path outside /mnt, or the result doesn't fit. */
static bool normalize_path(const char *cwd, const char *arg, char *out, size_t out_len)
{
    char joined[FILEMGR_PATH_MAX];

    if (arg[0] == '/') {
        if (strcmp(arg, "/") == 0) {
            arg = APP_FILEMGR_MOUNT_PATH;
        } else if (strncmp(arg, APP_FILEMGR_MOUNT_PATH "/", strlen(APP_FILEMGR_MOUNT_PATH) + 1) != 0
                   && strcmp(arg, APP_FILEMGR_MOUNT_PATH) != 0) {
            return false;   /* absolute path outside /mnt */
        }
        if (strlcpy(joined, arg, sizeof(joined)) >= sizeof(joined)) {
            return false;
        }
    } else {
        if (strcmp(cwd, "/") == 0) {
            joined[0] = '\0';
        } else if (strlcpy(joined, cwd, sizeof(joined)) >= sizeof(joined)) {
            return false;
        }
        if (strlcat(joined, "/", sizeof(joined)) >= sizeof(joined)
            || strlcat(joined, arg, sizeof(joined)) >= sizeof(joined)) {
            return false;
        }
    }

    /* Split on '/', resolving "." and ".." segment by segment */
    size_t root_len = strlen(APP_FILEMGR_MOUNT_PATH);
    strlcpy(out, APP_FILEMGR_MOUNT_PATH, out_len);

    char *saveptr = NULL;
    /* skip the mount prefix itself; tokens below are relative to it */
    char *tokens = joined + root_len;
    for (char *tok = strtok_r(tokens, "/", &saveptr); tok != NULL; tok = strtok_r(NULL, "/", &saveptr)) {
        if (strcmp(tok, ".") == 0) {
            continue;
        }
        if (strcmp(tok, "..") == 0) {
            /* pop last segment, but never above the mount root */
            char *last = strrchr(out, '/');
            if (last != NULL && (size_t)(last - out) >= root_len) {
                *last = '\0';
            }
            if (out[0] == '\0') {
                strlcpy(out, APP_FILEMGR_MOUNT_PATH, out_len);
            }
            continue;
        }
        if (strlcat(out, "/", out_len) >= out_len
            || strlcat(out, tok, out_len) >= out_len) {
            return false;
        }
    }
    return true;
}

static void print_entry_long(const char *dir, const char *name)
{
    char path[FILEMGR_PATH_MAX];
    struct stat st;
    struct tm tm_info;
    char time_str[20] = "----/--/-- --:--";

    snprintf(path, sizeof(path), "%s/%s", dir, name);
    if (stat(path, &st) != 0) {
        printf("? %10s  %s\n", "-", name);
        return;
    }

    localtime_r(&st.st_mtime, &tm_info);
    strftime(time_str, sizeof(time_str), "%Y/%m/%d %H:%M", &tm_info);
    printf("%c %10lu  %s  %s\n",
           S_ISDIR(st.st_mode) ? 'd' : '-',
           (unsigned long)st.st_size, time_str, name);
}

static int do_ls(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &ls_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, ls_args.end, argv[0]);
        return 1;
    }

    char dir[FILEMGR_PATH_MAX];
    const char *path = (ls_args.path->count > 0) ? ls_args.path->sval[0] : NULL;
    if (path == NULL) {
        strlcpy(dir, s_cwd, sizeof(dir));
    } else if (!normalize_path(s_cwd, path, dir, sizeof(dir))) {
        ESP_LOGE(TAG, "Invalid path '%s' (must stay under " APP_FILEMGR_MOUNT_PATH ")", path);
        return 1;
    }

    struct stat st;
    if (stat(dir, &st) != 0) {
        ESP_LOGE(TAG, "'%s' does not exist", dir);
        return 1;
    }
    if (!S_ISDIR(st.st_mode)) {
        /* ls on a file lists the file itself */
        const char *name = strrchr(dir, '/');
        name = (name != NULL) ? name + 1 : dir;
        if (ls_args.long_fmt->count > 0) {
            char parent[FILEMGR_PATH_MAX];
            strlcpy(parent, dir, sizeof(parent));
            char *slash = strrchr(parent, '/');
            if (slash != NULL) {
                *slash = '\0';
            }
            print_entry_long(parent[0] ? parent : "/", name);
        } else {
            printf("%s\n", name);
        }
        return 0;
    }

    DIR *dp = opendir(dir);
    if (dp == NULL) {
        ESP_LOGE(TAG, "Failed to open '%s'", dir);
        return 1;
    }

    bool long_fmt = ls_args.long_fmt->count > 0;
    struct dirent *entry;
    while ((entry = readdir(dp)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (long_fmt) {
            print_entry_long(dir, entry->d_name);
        } else if (entry->d_type == DT_DIR) {
            printf("%s/\n", entry->d_name);
        } else {
            printf("%s\n", entry->d_name);
        }
    }
    closedir(dp);
    return 0;
}

static int do_cd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &cd_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, cd_args.end, argv[0]);
        return 1;
    }

    const char *path = cd_args.path->sval[0];
    char dir[FILEMGR_PATH_MAX];
    if (!normalize_path(s_cwd, path, dir, sizeof(dir))) {
        ESP_LOGE(TAG, "Invalid path '%s' (must stay under " APP_FILEMGR_MOUNT_PATH ")", path);
        return 1;
    }

    struct stat st;
    if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        ESP_LOGE(TAG, "'%s' is not a directory", path);
        return 1;
    }

    strlcpy(s_cwd, dir, sizeof(s_cwd));
    printf("%s\n", s_cwd);
    return 0;
}

static int do_pwd(int argc, char **argv)
{
    printf("%s\n", s_cwd);
    return 0;
}

void register_filemgr(void)
{
    ls_args.long_fmt = arg_lit0("l", NULL, "list with type, size and modification time");
    ls_args.path = arg_str0(NULL, NULL, "<path>", "directory to list (default: current directory)");
    ls_args.end = arg_end(2);

    cd_args.path = arg_str1(NULL, NULL, "<path>", "directory to change to");
    cd_args.end = arg_end(2);

    const esp_console_cmd_t ls_cmd = {
        .command = "ls",
        .help = "List directory contents under /mnt.\n"
        "Examples:\n"
        " ls \n"
        " ls -l subdir \n"
        " ls /mnt \n",
        .hint = NULL,
        .func = &do_ls,
        .argtable = &ls_args
    };

    const esp_console_cmd_t cd_cmd = {
        .command = "cd",
        .help = "Change current directory (restricted to /mnt).\n"
        "Examples: cd subdir, cd .., cd /mnt",
        .hint = NULL,
        .func = &do_cd,
        .argtable = &cd_args
    };

    const esp_console_cmd_t pwd_cmd = {
        .command = "pwd",
        .help = "Print current directory",
        .hint = NULL,
        .func = &do_pwd,
        .argtable = NULL
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&ls_cmd));
    ESP_ERROR_CHECK(esp_console_cmd_register(&cd_cmd));
    ESP_ERROR_CHECK(esp_console_cmd_register(&pwd_cmd));
}

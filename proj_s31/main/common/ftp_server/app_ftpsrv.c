#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/_types.h>
#include <sys/stat.h>
#include <lwip/sockets.h>
#include "cc.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "app_ftpsrv.h"
#include "app_filemgr.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#define FTP_CMD_LINE_MAX   512
#define FTP_TASK_STACK     6144
#define FTP_TASK_PRIO      5
#define FTP_USER_MAX       32
#define FTP_ROOT_MAX       64
#define FTP_MAX_NUM        2
#define FTP_VPATH_MAX     256
#define FTP_REALPATH_MAX  (FTP_ROOT_MAX + FTP_VPATH_MAX)   // 320
#define FTP_NAME_MAX      255                              // FAT LFN / dirent d_name
#define FTP_ITEM_PATH_MAX (FTP_REALPATH_MAX + 1 + FTP_NAME_MAX)  // real + '/' + name
#define FTP_VPATH_DEPTH_MAX 16      // 虚拟路径堆栈深度最大值
#define FTP_XFER_BUF_SIZE  4096

static const char *TAG = "FTP_SERVER";

typedef enum _E_FtpSessionState
{
    FTP_ST_WAIT_USER = 0,       // 等待用户名
    FTP_ST_WAIT_PASS = 1,       // 等待密码
    FTP_ST_AUTHED = 2,          // 已认证
    FTP_ST_QUIT = 3,            // 退出
}E_FtpSessionState;

typedef struct _T_FTPSRV_CTX
{
    int listen_fd;
    int client_fd;
    TaskHandle_t task;
    SemaphoreHandle_t stop_sem;
    volatile bool stop_req;
    volatile bool running;
    uint16_t port;
    char user[FTP_USER_MAX];
    char pass[FTP_USER_MAX];
    char root[FTP_ROOT_MAX];
}T_FTPSRV_CTX;

typedef struct _T_FtpSession
{
    int ctrl_fd;
    int pasv_fd;
    E_FtpSessionState state; // 会话状态
    T_FTPSRV_CTX     *srv;
    char              cwd[FTP_VPATH_MAX];   // 当前虚拟目录，恒以 "/" 开头
}T_FtpSession;

// ftp cmd function
typedef void (*ftp_cmd_fn)(T_FtpSession *sess, const char *arg);
static void cmd_user(T_FtpSession *sess, const char *arg);
static void cmd_pass(T_FtpSession *sess, const char *arg);
static void cmd_quit(T_FtpSession *sess, const char *arg);
static void cmd_noop(T_FtpSession *sess, const char *arg);
static void cmd_help(T_FtpSession *sess, const char *arg);
static void cmd_list(T_FtpSession *sess, const char *arg);
static void cmd_pwd(T_FtpSession *sess, const char *arg);
static void cmd_cd(T_FtpSession *sess, const char *arg);
static void cmd_mkdir(T_FtpSession *sess, const char *arg);
static void cmd_rmdir(T_FtpSession *sess, const char *arg);
static void cmd_rm(T_FtpSession *sess, const char *arg);
static void cmd_rename(T_FtpSession *sess, const char *arg);
static void cmd_size(T_FtpSession *sess, const char *arg);
static void cmd_stor(T_FtpSession *sess, const char *arg);
static void cmd_retr(T_FtpSession *sess, const char *arg);
static void cmd_appe(T_FtpSession *sess, const char *arg);
static void cmd_dele(T_FtpSession *sess, const char *arg);
static void cmd_abor(T_FtpSession *sess, const char *arg);
static void cmd_site(T_FtpSession *sess, const char *arg);
static void cmd_stat(T_FtpSession *sess, const char *arg);
static void cmd_cwd(T_FtpSession *sess, const char *arg);
static void cmd_cdup(T_FtpSession *sess, const char *arg);
static void cmd_syst(T_FtpSession *sess, const char *arg);
static void cmd_type(T_FtpSession *sess, const char *arg);
static void cmd_pasv(T_FtpSession *sess, const char *arg);

/* ftp reply macros */
static int ftp_reply(int fd, const char *fmt, ...);

typedef struct _T_FtpCmd
{
    const char *name;               // 命令名称
    ftp_cmd_fn func;                // 命令处理函数
    E_FtpSessionState min_state;    // 最小允许状态
}T_FtpCmd;

static const T_FtpCmd s_ftp_cmds[] = 
{
    { "USER", cmd_user, FTP_ST_WAIT_USER },
    { "PASS", cmd_pass, FTP_ST_WAIT_PASS },
    { "QUIT", cmd_quit, FTP_ST_AUTHED },
    { "NOOP", cmd_noop, FTP_ST_AUTHED },
    { "PWD", cmd_pwd, FTP_ST_AUTHED },
    { "XPWD", cmd_pwd, FTP_ST_AUTHED },   // 别名：同一个 handler 注册两次
    { "CWD", cmd_cwd, FTP_ST_AUTHED },
    { "CDUP", cmd_cdup, FTP_ST_AUTHED },
    { "SYST", cmd_syst, FTP_ST_AUTHED },
    { "TYPE", cmd_type, FTP_ST_AUTHED },
    { "PASV", cmd_pasv, FTP_ST_AUTHED },
    { "LIST", cmd_list, FTP_ST_AUTHED },
    { "STOR", cmd_stor, FTP_ST_AUTHED },
    { "RETR", cmd_retr, FTP_ST_AUTHED },
    { "DELE", cmd_dele, FTP_ST_AUTHED },
    {"MKD", cmd_mkdir, FTP_ST_AUTHED },
    // { "NLST", cmd_nlst, FTP_ST_AUTHED },
};

T_FTPSRV_CTX *s_ftpsrv_tbl[FTP_MAX_NUM];

static bool vpath_normalize(const char *cwd, const char *arg, char *out, size_t out_len)
{
    char combined[FTP_VPATH_MAX];
    const char *segs[FTP_VPATH_DEPTH_MAX];
    int depth = 0;

    /* combine */
    if(arg == NULL)
    {
        snprintf(combined, sizeof(combined), "%s", cwd);
    }
    else if(arg[0] == '/')
    {
        snprintf(combined, sizeof(combined), "%s", arg);
    }
    else 
    {
        snprintf(combined, sizeof(combined), "%s/%s", strcmp(cwd, "/") == 0 ? "" : cwd, arg);
    }

    /* split */
    char *save = NULL;
    for(char *tok = strtok_r(combined, "/", &save); tok; tok = strtok_r(NULL, "/", &save))
    {
        if(strcmp(tok, ".") == 0)
        {
            continue;
        }
        
        if(strcmp(tok, "..") == 0)
        {
            if(depth > 0) depth--;      // stack empty, don't move
            continue;
        }

        if(depth >= FTP_VPATH_DEPTH_MAX)
        {
            return false;                // depth limit exceeded
        }

        segs[depth++] = tok;
    }

    /* reassemble */
    if(depth == 0)
    {
        snprintf(out, out_len, "/");
        return true;
    }

    size_t used = 0;
    out[0] = '\0';
    for(int i = 0; i < depth; i++)
    {
        int n = snprintf(out + used, out_len - used, "/%s", segs[i]);
        if(n > (int)(out_len - used))
        {
            return false;                // total length limit exceeded
        }
        used += n;
    }
    return true;
}

static void vpath_to_real(const T_FtpSession *sess, const char *vpath, char *out, size_t len)
{
    snprintf(out, len, "%s%s", sess->srv->root, vpath);
}

static void ftp_parse_cmd(char *line, char **verb, char **arg)
{
    *verb = line;
    *arg  = NULL;

    char *sp = strchr(line, ' ');
    if (sp != NULL) 
    {
        *sp = '\0';
        sp++;
        while (*sp == ' ') 
        {
            sp++;
        }
        if (*sp != '\0') 
        {
            *arg = sp;
        }
    }
}

static int ftp_reply(int fd, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (len < 0) 
    {
        return len;
    }
    if (len >= sizeof(buf)) 
    {
        len = sizeof(buf) - 1;
    }

    int sent = 0;
    while(sent < len)
    {
        int n = send(fd, buf + sent, len - sent, 0);
        if(n < 0)
        {
            return -1;
        }
        sent += n;
    }
    return sent;
}

static int ftp_recvline(int fd, char *buf, size_t len)
{
    size_t i = 0;
    while(i < len - 1)
    {
        char c;
        int n = recv(fd, &c, 1, 0);
        if(n <= 0)
        {
            return -1;
        }

        if(c == '\n')
        {
            break;
        }
        if(c != '\r')
        {
            buf[i++] = c;
        }
    }
    buf[i] = '\0';
    return i;
}

static int ftp_data_accept(T_FtpSession *sess)
{
    if(sess->pasv_fd < 0)
    {
        ftp_reply(sess->ctrl_fd, "425 Use PASV first\r\n");
        return -1;
    }

    int fd = accept(sess->pasv_fd, NULL, NULL);   // max 10s (set above)
    if(fd < 0)
    {
        close(sess->pasv_fd);
        sess->pasv_fd = -1;
        ftp_reply(sess->ctrl_fd, "425 Cannot open data connection\r\n");
        return -1;
    }
    return fd;
}

static void ftp_data_close(T_FtpSession *sess, int data_fd)
{
    if(data_fd >= 0) close(data_fd);
    if(sess->pasv_fd >= 0)
    {
        close(sess->pasv_fd);
        sess->pasv_fd = -1;
    }
    return;
}


static void cmd_user(T_FtpSession *sess, const char *arg)
{
    if(arg == NULL)
    {
        ftp_reply(sess->ctrl_fd, "501 Missing username\r\n");
        return;
    }
    if(strcmp(arg, sess->srv->user) == 0)
    {
        ftp_reply(sess->ctrl_fd, "331 Password required\r\n");
        sess->state = FTP_ST_WAIT_PASS;
    }
    else 
    {
        ftp_reply(sess->ctrl_fd, "530 Unknown user\r\n");
        sess->state = FTP_ST_WAIT_USER;
    }
    return;
}

static void cmd_pass(T_FtpSession *sess, const char *arg)
{
    if(sess->state == FTP_ST_AUTHED)
    {
        ftp_reply(sess->ctrl_fd, "230 Already logged in\r\n");
        return;
    }
    if(sess->state != FTP_ST_WAIT_PASS)
    {
        ftp_reply(sess->ctrl_fd, "503 Login with USER first\r\n");
        return;
    }
    if(arg != NULL && strcmp(arg, sess->srv->pass) == 0)
    {
        ftp_reply(sess->ctrl_fd, "230 Login successful\r\n");
        sess->state = FTP_ST_AUTHED;
    }
    else
    {
        ftp_reply(sess->ctrl_fd, "530 Wrong password\r\n");
        sess->state = FTP_ST_WAIT_USER;
    }
    return;
}

static void cmd_quit(T_FtpSession *sess, const char *arg)
{
    (void)arg;
    ftp_reply(sess->ctrl_fd, "221 Goodbye\r\n");
    sess->state = FTP_ST_QUIT;
    return;
}

static void cmd_noop(T_FtpSession *sess, const char *arg)
{
    (void)arg;
    ftp_reply(sess->ctrl_fd, "200 OK\r\n");
    return;
}

static void cmd_pwd(T_FtpSession *sess, const char *arg)
{
    (void)arg;

    ftp_reply(sess->ctrl_fd, "257 \"%s\" is current directory\r\n", sess->cwd);
}

static void cmd_cwd(T_FtpSession *sess, const char *arg)
{
    if(arg == NULL)
    {
        ftp_reply(sess->ctrl_fd, "501 Missing directory\r\n");
        return;
    }

    char vpath[FTP_VPATH_MAX], real[FTP_REALPATH_MAX];
    if(!vpath_normalize(sess->cwd, arg, vpath, sizeof(vpath)))
    {
        ftp_reply(sess->ctrl_fd, "550 Path too long\r\n");
        return;
    }

    vpath_to_real(sess, vpath, real, sizeof(real));
    struct stat st;
    if(stat(real, &st) != 0 || !S_ISDIR(st.st_mode))
    {
        ftp_reply(sess->ctrl_fd, "550 Not a directory\r\n");
        return;
    }

    strncpy(sess->cwd, vpath, sizeof(sess->cwd) - 1);
    sess->cwd[sizeof(sess->cwd) - 1] = '\0';
    ftp_reply(sess->ctrl_fd, "250 CWD to \"%s\"\r\n", sess->cwd);
}

static void cmd_cdup(T_FtpSession *sess, const char *arg)
{
    (void)arg;
    cmd_cwd(sess, "..");            // CDUP is alias for CWD ..
}

static void cmd_syst(T_FtpSession *sess, const char *arg) 
{
    (void)arg;
    ftp_reply(sess->ctrl_fd, "215 UNIX Type: L8\r\n");
}

static void cmd_type(T_FtpSession *sess, const char *arg)
{
    if(arg && (arg[0] == 'I' || arg[0] == 'i' || arg[0] == 'A' || arg[0] == 'a'))
    {
        ftp_reply(sess->ctrl_fd, "200 Type set to %c\r\n", arg[0]);
    }
    else 
    {
        ftp_reply(sess->ctrl_fd, "504 Unsupported type\r\n");
    }
}

static void cmd_pasv(T_FtpSession *sess, const char *arg)
{
    (void)arg;

    /* if pasv is already active, close it */
    if(sess->pasv_fd >= 0)
    {
        close(sess->pasv_fd);
        sess->pasv_fd = -1;
    }

    /* create new pasv socket */
    sess->pasv_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(sess->pasv_fd < 0)
    {
        goto fail;
    }

    // accept timeout 10s
    struct timeval tv = { .tv_sec = 10, .tv_usec = 0 };
    setsockopt(sess->pasv_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // bind port 0 = kernel allocate free temporary port
    struct sockaddr_in addr =
    {
        .sin_family = AF_INET,
        .sin_port = htons(0),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    if(bind(sess->pasv_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        goto fail;
    }

    if(listen(sess->pasv_fd, 1) < 0)
    {
        goto fail;
    }

    // get sock name for port
    struct sockaddr_in pasv_addr;
    socklen_t len = sizeof(pasv_addr);
    getsockname(sess->pasv_fd, (struct sockaddr *)&pasv_addr, &len);
    uint16_t port = ntohs(pasv_addr.sin_port);

    // get ip from control connection local address
    struct sockaddr_in local;
    len = sizeof(local);
    getsockname(sess->ctrl_fd, (struct sockaddr *)&local, &len);
    uint32_t ip = ntohl(local.sin_addr.s_addr);

    ftp_reply(sess->ctrl_fd, "227 Entering Passive Mode (%u,%u,%u,%u,%u,%u)\r\n",
              (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF,
              (port >> 8) & 0xFF, port & 0xFF);
    return;
fail:
    if(sess->pasv_fd >= 0)
    {
        close(sess->pasv_fd);
        sess->pasv_fd = -1;
    }
    ftp_reply(sess->ctrl_fd, "425 Cannot open passive connection\r\n");
    return;
}

static void cmd_list(T_FtpSession *sess, const char *arg)
{
    (void)arg;   // simplify: ignore LIST path parameter, only list current directory (M6 extensible)
    
    char real[FTP_REALPATH_MAX];
    vpath_to_real(sess, sess->cwd, real, sizeof(real));

    DIR *dir = opendir(real);
    if(dir == NULL)
    {
        ftp_reply(sess->ctrl_fd, "550 Cannot open directory\r\n");
        return;
    }

    int data_fd = ftp_data_accept(sess);
    if(data_fd < 0)
    {
        closedir(dir);
        return;
    }
    ftp_reply(sess->ctrl_fd, "150 Opening data connection\r\n");

    char line[FTP_CMD_LINE_MAX], item_path[FTP_ITEM_PATH_MAX], timebuf[32];
    struct dirent *ent;
    while((ent = readdir(dir)) != NULL)
    {
        /* stat to get size/type/time */
        int pn = snprintf(item_path, sizeof(item_path), "%s/%s", real, ent->d_name);
        if(pn < 0 || pn >= (int)sizeof(item_path))
        {
            continue;
        }
        struct stat st;
        if(stat(item_path, &st) != 0) continue;

        struct tm *tm = localtime(&st.st_mtime);
        strftime(timebuf, sizeof(timebuf), "%b %d %H:%M", tm);

        /* Unix ls -l style: type+permissions link count owner group size time name */
        int n = snprintf(line, sizeof(line), "%crw-r--r-- 1 0 0 %8lu %s %s\r\n",
                         S_ISDIR(st.st_mode) ? 'd' : '-',
                         (unsigned long)st.st_size, timebuf, ent->d_name);
        int sent = 0;
        while(sent < n)
        {
            int w = send(data_fd, line + sent, n - sent, 0);
            if(w < 0) goto done;
            sent += w;
        }
    }

done:
    closedir(dir);
    ftp_data_close(sess, data_fd);
    ftp_reply(sess->ctrl_fd, "226 Transfer complete\r\n");
    return;
}

static void cmd_retr(T_FtpSession *sess, const char *arg)
{
    if(arg == NULL)
    {
        ftp_reply(sess->ctrl_fd, "501 Missing filename\r\n");
        return;
    }

    // get real path
    char vpath[FTP_VPATH_MAX], real[FTP_REALPATH_MAX];
    if(!vpath_normalize(sess->cwd, arg, vpath, sizeof(vpath)))
    {
        ftp_reply(sess->ctrl_fd, "550 Path too long\r\n");
        return;
    }
    vpath_to_real(sess, vpath, real, sizeof(real));
    
    FILE *fp = fopen(real, "rb");
    if(fp == NULL)
    {
        ftp_reply(sess->ctrl_fd, "550 File not found\r\n");
        return;
    }

    int data_fd = ftp_data_accept(sess);
    if(data_fd < 0)
    {
        fclose(fp);
        return;
    }

    struct timeval tv = { .tv_sec = 30 };
    setsockopt(data_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    ftp_reply(sess->ctrl_fd, "150 Opening data connection\r\n");

    char *buf = malloc(FTP_XFER_BUF_SIZE);   // use heap instead of stack to avoid stack overflow
    bool ok = (buf != NULL);
    size_t total = 0;
    
    if(ok)
    {
        size_t n;
        while((n = fread(buf, 1, FTP_XFER_BUF_SIZE, fp)) > 0)
        {
            size_t sent = 0;
            while(sent < n)
            {
                int w = send(data_fd, buf + sent, n - sent, 0);
                if(w < 0) 
                { 
                    ok = false; 
                    break; 
                }
                sent += w;
            }
            if(!ok) break;
            total += n;
        }
        free(buf);
    }
    fclose(fp);
    ftp_data_close(sess, data_fd);

    if(ok)
    {
        ftp_reply(sess->ctrl_fd, "226 Transfer complete, %u bytes\r\n", (unsigned)total);
    }
    else 
    {
        ftp_reply(sess->ctrl_fd, "426 Transfer aborted\r\n");
    }

    ESP_LOGI(TAG, "free heap: %lu, min: %lu",
        (unsigned long)esp_get_free_heap_size(),
        (unsigned long)esp_get_minimum_free_heap_size());
}

static void cmd_stor(T_FtpSession *sess, const char *arg)
{
    if(arg == NULL)
    {
        ftp_reply(sess->ctrl_fd, "501 Missing filename\r\n");
        return;
    }

    // get real path
    char vpath[FTP_VPATH_MAX], real[FTP_REALPATH_MAX];
    if(!vpath_normalize(sess->cwd, arg, vpath, sizeof(vpath)))
    {
        ftp_reply(sess->ctrl_fd, "550 Path too long\r\n");
        return;
    }
    vpath_to_real(sess, vpath, real, sizeof(real));
    
    FILE *fp = fopen(real, "wb");
    if(fp == NULL)
    {
        ftp_reply(sess->ctrl_fd, "550 File not found\r\n");
        return;
    }

    int data_fd = ftp_data_accept(sess);
    if(data_fd < 0)
    {
        fclose(fp);
        return;
    }

    struct timeval tv = { .tv_sec = 30 };
    setsockopt(data_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ftp_reply(sess->ctrl_fd, "150 Opening data connection\r\n");

    char *buf = malloc(FTP_XFER_BUF_SIZE);   // use heap instead of stack to avoid stack overflow
    bool ok = (buf != NULL);
    size_t total = 0;
    
    if(ok)
    {
        ssize_t n;
        while ((n = recv(data_fd, buf, FTP_XFER_BUF_SIZE, 0)) > 0) 
        {
            if (fwrite(buf, 1, n, fp) != n) 
            { 
                ok = false; 
                break; 
            }   // disk full etc.
            total += n;
        }
        if(n < 0)
        {
            ok = false;
        }
        free(buf);
    }

    
    fclose(fp);
    ftp_data_close(sess, data_fd);
    if(!ok)
    {
        remove(real);
    }

    if(ok)
    {
        ftp_reply(sess->ctrl_fd, "226 Transfer complete, %u bytes\r\n", (unsigned)total);
    }
    else 
    {
        ftp_reply(sess->ctrl_fd, "426 Transfer aborted\r\n");
    }

    ESP_LOGI(TAG, "free heap: %lu, min: %lu",
        (unsigned long)esp_get_free_heap_size(),
        (unsigned long)esp_get_minimum_free_heap_size());
}

static void cmd_dele(T_FtpSession *sess, const char *arg)
{
    if(arg == NULL)
    {
        ftp_reply(sess->ctrl_fd, "501 Missing filename\r\n");
        return;
    }

    // get real path
    char vpath[FTP_VPATH_MAX], real[FTP_REALPATH_MAX];
    if(!vpath_normalize(sess->cwd, arg, vpath, sizeof(vpath)))
    {
        ftp_reply(sess->ctrl_fd, "550 Path too long\r\n");
        return;
    }
    vpath_to_real(sess, vpath, real, sizeof(real));

    struct stat st;
    if(stat(real, &st) != 0 || !S_ISREG(st.st_mode))
    {
        ftp_reply(sess->ctrl_fd, "550 Not a file\r\n");
        return;
    }
    
    if(remove(real) != 0)
    {
        ftp_reply(sess->ctrl_fd, "550 Failed to delete file\r\n");
        return;
    }
    
    ftp_reply(sess->ctrl_fd, "250 File deleted successfully\r\n");
    return;
}

static void cmd_mkdir(T_FtpSession *sess, const char *arg)
{
    if(arg == NULL)
    {
        ftp_reply(sess->ctrl_fd, "501 Missing directory name\r\n");
        return;
    }

    char vpath[FTP_VPATH_MAX], real[FTP_REALPATH_MAX];
    if(!vpath_normalize(sess->cwd, arg, vpath, sizeof(vpath)))
    {
        ftp_reply(sess->ctrl_fd, "550 Path too long\r\n");
        return;
    }
    vpath_to_real(sess, vpath, real, sizeof(real));
    
    if(mkdir(real, 0775) != 0)
    {
        ftp_reply(sess->ctrl_fd, "550 Failed to create directory\r\n");
        return;
    }
    
    ftp_reply(sess->ctrl_fd, "250 Directory created successfully\r\n");
    return;
}

static void ftp_session_handle(T_FTPSRV_CTX *srv_ctx)
{
    T_FtpSession sess = 
    {
        .pasv_fd = -1,                   // 0 是 stdin，不能当“无 socket”
        .cwd     = "/",
        .ctrl_fd = srv_ctx->client_fd,
        .state   = FTP_ST_WAIT_USER,     // 初始状态
        .srv     = srv_ctx,
    };

    ftp_reply(sess.ctrl_fd, "220 ESP32 FTP Server ready\r\n");
    ESP_LOGI(TAG, "FTP session started");

    char line[FTP_CMD_LINE_MAX];
    while (sess.state != FTP_ST_QUIT && !srv_ctx->stop_req)
    {
        int n = ftp_recvline(srv_ctx->client_fd, line, sizeof(line));
        if(n < 0)  break;       // error or disconnect
        if(n == 0) continue;    // empty line, ignore

        char *verb, *arg;
        ftp_parse_cmd(line, &verb, &arg);
        // ESP_LOGI(TAG, "cmd: %s, %s", verb, arg ? arg : "");

        // search cmd in table
        const T_FtpCmd *cmd = NULL;
        for(int i = 0; i < sizeof(s_ftp_cmds)/sizeof(s_ftp_cmds[0]); i++)
        {
            if(strcasecmp(verb, s_ftp_cmds[i].name) == 0)
            {
                cmd = &s_ftp_cmds[i];
                break;
            }
        }

        if(cmd == NULL)
        {
            ftp_reply(sess.ctrl_fd, "500 Unknown command\r\n");
        }
        else if(sess.state < cmd->min_state)
        {
            ftp_reply(sess.ctrl_fd, "530 Not logged in\r\n");
        }
        else
        {
            cmd->func(&sess, arg);
        }
    }

    close(sess.ctrl_fd);
    if(sess.pasv_fd >= 0) close(sess.pasv_fd);
    srv_ctx->client_fd = -1;
    ESP_LOGI(TAG, "FTP session ended");
}

static void ftp_server_task(void *arg)
{
    T_FTPSRV_CTX *srv_ctx = (T_FTPSRV_CTX *)arg;
    struct sockaddr_in addr =
    {
        .sin_family = AF_INET,
        .sin_port = htons(srv_ctx->port),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    srv_ctx->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(srv_ctx->listen_fd < 0)
    {
        ESP_LOGE(TAG, "Failed to create socket: %s", strerror(errno));
        goto exit;
    }

    int reuse = 1;
    /* SO_REUSEADDR: allow reusing the address even if it is already in use */
    setsockopt(srv_ctx->listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    if(bind(srv_ctx->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        ESP_LOGE(TAG, "Failed to bind socket: %s", strerror(errno));
        goto exit;
    }

    if(listen(srv_ctx->listen_fd, 1) < 0)
    {// only one connection at a time
        ESP_LOGE(TAG, "Failed to listen: %s", strerror(errno));
        goto exit;
    }
    srv_ctx->running = true;
    ESP_LOGI(TAG, "FTP server listening on port %u, root %s", srv_ctx->port, srv_ctx->root);

    while(!srv_ctx->stop_req)
    {
        int fd = accept(srv_ctx->listen_fd, NULL, NULL);
        if(fd < 0)
        {
            if(srv_ctx->stop_req)
            {
                break;
            }
            continue;
        }

        /* stop 用回环自连唤醒 accept 时，丢掉这次假连接 */
        if(srv_ctx->stop_req)
        {
            close(fd);
            break;
        }

        if(srv_ctx->client_fd >= 0)
        {
            ftp_reply(fd, "421 Server busy, please try again later\r\n");
            close(fd);
            continue;
        }

        srv_ctx->client_fd = fd;
        ftp_session_handle(srv_ctx);
    }

exit:
    if(srv_ctx->listen_fd >= 0)
    {
        close(srv_ctx->listen_fd);
        srv_ctx->listen_fd = -1;
    }
    srv_ctx->running = false;
    xSemaphoreGive(srv_ctx->stop_sem);
    vTaskDelete(NULL);
}

esp_err_t ftp_server_start(T_AppFtpSrvCfg *cfg)
{
    int index = 0;
    if(cfg == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    for(index = 0; index < FTP_MAX_NUM; index++)
    {
        if(s_ftpsrv_tbl[index] == NULL)
        {
            break;
        }
    }

    if(index >= FTP_MAX_NUM)
    {
        return ESP_ERR_NO_MEM;
    }

    T_FTPSRV_CTX *srv_ctx = calloc(1, sizeof(T_FTPSRV_CTX));
    if(srv_ctx == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    srv_ctx->port = cfg->port ? cfg->port:APP_FTPSRV_DEFAULT_PORT;
    strncpy(srv_ctx->user, cfg->user ? cfg->user:APP_FTPSRV_DEFAULT_USER, sizeof(srv_ctx->user) - 1);
    strncpy(srv_ctx->pass, cfg->pass ? cfg->pass:APP_FTPSRV_DEFAULT_PASS, sizeof(srv_ctx->pass) - 1);
    strncpy(srv_ctx->root, cfg->root ? cfg->root:APP_FILEMGR_MOUNT_PATH, sizeof(srv_ctx->root) - 1);

    srv_ctx->listen_fd = -1;
    srv_ctx->client_fd = -1;
    srv_ctx->stop_sem = xSemaphoreCreateBinary();
    if(srv_ctx->stop_sem == NULL)
    {
        free(srv_ctx);
        srv_ctx = NULL;
        return ESP_ERR_NO_MEM;
    }

    if(xTaskCreate(ftp_server_task, cfg->name ? cfg->name:"ftp_server", FTP_TASK_STACK, srv_ctx, FTP_TASK_PRIO, &srv_ctx->task) != pdPASS)
    {
        vSemaphoreDelete(srv_ctx->stop_sem);
        free(srv_ctx);
        srv_ctx = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_ftpsrv_tbl[index] = srv_ctx;
    cfg->srv_idx = index;

    return ESP_OK;
}

/* lwIP 上 shutdown(listen_fd) 不能唤醒 accept；回环自连可以，且空闲时不占 CPU。 */
static void ftp_wake_accept(T_FTPSRV_CTX *srv_ctx)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0)
    {
        return;
    }

    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr =
    {
        .sin_family = AF_INET,
        .sin_port = htons(srv_ctx->port),
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
    };
    connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    close(fd);
}

esp_err_t ftp_server_stop(uint16_t srv_idx)
{
    if(srv_idx >= FTP_MAX_NUM)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if(s_ftpsrv_tbl[srv_idx] == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    T_FTPSRV_CTX *srv_ctx = s_ftpsrv_tbl[srv_idx];
    srv_ctx->stop_req = true;

    /* 有会话时打断控制连接上的 recv */
    if(srv_ctx->client_fd >= 0)
    {
        shutdown(srv_ctx->client_fd, SHUT_RDWR);
    }

    /* 空闲堵在 accept 时，用回环连接唤醒（不要依赖 shutdown(listen_fd)） */
    ftp_wake_accept(srv_ctx);

    xSemaphoreTake(srv_ctx->stop_sem, portMAX_DELAY);
    vSemaphoreDelete(srv_ctx->stop_sem);
    free(srv_ctx);
    s_ftpsrv_tbl[srv_idx] = NULL;
    return ESP_OK;
}

bool ftp_server_is_running(void)
{
    return true;
}

void ftp_server_init(void)
{
    extern esp_err_t ftpsrv_cmd_init(void);
    ESP_ERROR_CHECK(ftpsrv_cmd_init());
}

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
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
    E_FtpSessionState state; // 会话状态
    T_FTPSRV_CTX     *srv;
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
static void cmd_type(T_FtpSession *sess, const char *arg);
static void cmd_stor(T_FtpSession *sess, const char *arg);
static void cmd_retr(T_FtpSession *sess, const char *arg);
static void cmd_appe(T_FtpSession *sess, const char *arg);
static void cmd_dele(T_FtpSession *sess, const char *arg);
static void cmd_abor(T_FtpSession *sess, const char *arg);
static void cmd_site(T_FtpSession *sess, const char *arg);
static void cmd_stat(T_FtpSession *sess, const char *arg);

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
};

T_FTPSRV_CTX *s_ftpsrv_tbl[FTP_MAX_NUM];

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
        if(n == 0)
        {
            return 0;
        }

        if(n < 0)
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

static void ftp_session_handle(T_FTPSRV_CTX *srv_ctx)
{
    T_FtpSession sess = 
    {
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
        ESP_LOGI(TAG, "cmd: %s, %s", verb, arg ? arg : "");

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

        if(srv_ctx->client_fd >= 0)
        {// 
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
    if(srv_ctx->listen_fd >= 0)
    {
        shutdown(srv_ctx->listen_fd, SHUT_RDWR);
    }

    if(srv_ctx->client_fd >= 0)
    {
        shutdown(srv_ctx->client_fd, SHUT_RDWR);
    }
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

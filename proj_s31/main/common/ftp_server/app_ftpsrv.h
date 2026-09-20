#ifndef __APP_FTPSRV_H__
#define __APP_FTPSRV_H__

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define APP_FTPSRV_DEFAULT_PORT 21
#define APP_FTPSRV_DEFAULT_USER "esp32"
#define APP_FTPSRV_DEFAULT_PASS "esp32"

typedef struct _T_AppFtpSrvCfg
{
    uint16_t    port;
    uint16_t    srv_idx;
    const char *user;
    const char *pass;
    const char *root;
    const char *name;
}T_AppFtpSrvCfg;

typedef struct _T_FtpSrv
{
    int index;
    char *name;
}T_FtpSrv;

esp_err_t ftp_server_start(T_AppFtpSrvCfg *cfg);
esp_err_t ftp_server_stop(uint16_t srv_idx);
bool ftp_server_is_running(void);
void ftp_server_init(void);

#endif
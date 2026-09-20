# FTP Server 实施计划

> 项目：`proj_s31`（目标芯片 `esp32s31`）
> 日期：2026-09-20
> 状态：待实施

## 1. 背景与目标

当前项目已经具备以下基础能力：

- **文件系统**：FATFS 挂载在 `/data`（`storage` 分区，12 MB，带磨损均衡），见 `main/common/filemgr/app_filemgr.c`
- **网络**：WiFi STA（`bsp_wifimgr`）+ 以太网（YT8531 PHY），均可通过 DHCP 获取 IP
- **控制台**：串口 REPL（`cmdmgr`），已注册 system / nvs / wifi / iperf / ethernet 等命令

目标：实现一个 FTP server，让 PC 端可以通过标准 FTP 客户端（FileZilla、Windows `ftp` 命令、资源管理器等）对设备 `/data` 分区进行**文件上传、下载、删除、建目录**等操作，方便产线/调试场景下读写设备文件。

### 1.1 方案选型

| 方案 | 评估 | 结论 |
|---|---|---|
| 第三方组件 `cezap/simple_ftp_server` | C 语言、VFS 后端，但 manifest 的 supported targets（esp32/s2/s3/c2/c3/c6/h2/p4）**不含 esp32s31**（本 fork 自定义 preview target），组件管理器解析依赖会失败 | 放弃 |
| 第三方组件 `espp/ftp` | C++ 实现、无认证、5 个依赖，与项目纯 C 风格不符 | 放弃 |
| **自研 FTP server 组件** | 纯 C、lwIP socket、无外部依赖，适配 esp32s31，风格与现有 `common/` 组件一致，完全可控 | **采用** |

### 1.2 已确认的关键决策

1. **实现方式**：自研 FTP server 组件，放在 `main/common/ftp_server/`
2. **认证方式**：固定用户名/密码，默认 `esp32` / `esp32`，可配置
3. **启动方式**：**不开机自启**，仅在串口控制台输入 `ftp start` 启动，可随时 `ftp stop`

## 2. 总体架构

```
PC (FileZilla / ftp 命令)
        │
        │ TCP 21 (控制连接) + TCP 临时端口 (数据连接, PASV)
        ▼
┌─────────────────────────────────────────────┐
│  ftp_server task (app_ftpsrv.c)             │
│  ├─ listen socket :21                       │
│  ├─ 命令解析/分发 (USER/PASS/LIST/RETR/...) │
│  ├─ PASV 数据通道 (临时端口 listen/accept)  │
│  └─ 路径 jail: FTP "/" → VFS "/data"        │
├─────────────────────────────────────────────┤
│  cmd_ftp.c  →  esp_console REPL             │
│  ftp start [--user u] [--pass p] [--port n] │
│  ftp stop / ftp status                      │
├─────────────────────────────────────────────┤
│  VFS FATFS (/data, storage 分区, WL)        │
└─────────────────────────────────────────────┘
```

设计要点：

- **控制连接**：TCP 端口 21，**单客户端**会话。设备资源有限，同时只服务一个控制连接；新连接在会话期间收到 220 后立即要求登录或排队拒绝（采用简单策略：已有会话时新连接直接拒绝并关闭）
- **数据连接**：仅支持**被动模式（PASV）**。每次传输（LIST/RETR/STOR）前，服务器在临时端口（bind 端口 0，由内核分配）监听，把 IP+端口通过 227 应答告知客户端，客户端主动连入。被动模式对 NAT/防火墙最友好，是所有现代 FTP 客户端的默认模式；主动模式 `PORT` 回复 502 不支持
- **文件根目录**：FTP 的 `/` 映射到 VFS 的 `/data`（`APP_FILEMGR_MOUNT_PATH`）。路径按 `/` 分段规范化，`.` 跳过、`..` 弹栈（最多弹到根），保证无法逃逸出 `/data`
- **本地 IP 获取**：PASV 应答中的 IP 通过 `getsockname()` 取控制连接的本端地址，WiFi STA 和以太网任一接口拿到 IP 均可正常工作，**无需注册任何 IP 事件回调**

## 3. 文件清单

### 3.1 新增文件

| 文件 | 说明 |
|---|---|
| `main/common/ftp_server/app_ftpsrv.h` | 公开 API 头文件（见 4.1） |
| `main/common/ftp_server/app_ftpsrv.c` | FTP 协议核心，约 600 行（server task、accept 循环、命令分发、PASV 数据通道、路径 jail、文件传输） |
| `main/common/ftp_server/cmd_ftp.h` | 控制台命令注册接口 `register_ftp_cmd()` |
| `main/common/ftp_server/cmd_ftp.c` | `ftp start/stop/status` 命令实现，argtable3 解析参数（风格参照 `cmd_nvs.c`） |

### 3.2 修改文件

| 文件 | 改动 |
|---|---|
| `main/common/CMakeLists.txt` | `COMMON_SRCS` 增加 `common/ftp_server/app_ftpsrv.c`、`common/ftp_server/cmd_ftp.c`；`COMMON_INCLUDE_DIRS` 增加 `common/ftp_server` |
| `main/CMakeLists.txt` | `PRIV_REQUIRES` 增加 `lwip`（`lwip/sockets.h`） |
| `main/common/cmd_system/cmdmgr.c` | `cmdmgr_init()` 中调用 `register_ftp_cmd()` |

## 4. 接口设计

### 4.1 公开 API（app_ftpsrv.h）

```c
#define APP_FTPSRV_DEFAULT_PORT 21
#define APP_FTPSRV_DEFAULT_USER "esp32"
#define APP_FTPSRV_DEFAULT_PASS "esp32"

typedef struct {
    uint16_t port;         // 控制端口，0 -> 21
    const char *user;      // 登录用户名，NULL -> "esp32"
    const char *pass;      // 登录密码，  NULL -> "esp32"
    const char *root_path; // FTP "/" 映射的 VFS 路径，NULL -> "/data"
    uint32_t stack_size;   // server task 栈，0 -> 默认 6144
    uint32_t prio;         // server task 优先级，0 -> 默认 5
} app_ftpsrv_config_t;

esp_err_t app_ftpsrv_start(const app_ftpsrv_config_t *cfg); // cfg 为 NULL 时全用默认值
esp_err_t app_ftpsrv_stop(void);   // 阻塞直到 server task 完全退出
bool      app_ftpsrv_is_running(void);
```

### 4.2 控制台命令

```
ftp start [--user <name>] [--pass <pwd>] [--port <n>]
ftp stop
ftp status
```

- `start`：不带参数时使用默认值（端口 21，账号 `esp32/esp32`）
- `stop`：停止服务器，断开当前会话，同步等待任务退出
- `status`：打印运行状态、端口、根目录、当前是否有客户端连接

## 5. 协议实现细节

### 5.1 支持的 FTP 命令

| 类别 | 命令 | 说明 |
|---|---|---|
| 登录/会话 | `USER` | 应答 331（要求密码）或 230（直接通过） |
| | `PASS` | 校验成功 230，失败 530 |
| | `QUIT` | 221 并关闭控制连接 |
| | `NOOP` | 200 |
| | `SYST` | 215 UNIX Type: L8 |
| | `FEAT` | 211 列出支持扩展（SIZE、MDTM） |
| | `OPTS` | 200（忽略参数） |
| | `TYPE` | `I`/`A` 均接受（统一按二进制传输），其他 504 |
| 目录 | `PWD`/`XPWD` | 257 返回当前虚拟路径 |
| | `CWD` | 250；目标不存在或非目录 550 |
| | `CDUP` | 250 返回上级（等效 `CWD ..`） |
| | `LIST` | Unix 风格列表：`-rw-r--r-- 1 0 0 <size> <Mon dd HH:MM> <name>` |
| | `NLST` | 仅文件名列表 |
| | `MKD`/`XMKD` | 257 创建目录，失败 550 |
| | `RMD`/`XRMD` | 250 删除空目录，失败 550 |
| 文件 | `RETR` | 下载：150 → 数据连接发送 → 226；文件不存在 550 |
| | `STOR` | 上传：150 → 数据连接接收 → 226（覆盖写） |
| | `APPE` | 追加上传 |
| | `SIZE` | 213 返回文件大小 |
| | `MDTM` | 213 返回修改时间（`YYYYMMDDHHMMSS`，UTC） |
| | `DELE` | 250 删除文件，失败 550 |
| | `RNFR`/`RNTO` | 重命名（RNFR 350 → RNTO 250） |
| 数据通道 | `PASV` | 227 应答 `(h1,h2,h3,h4,p1,p2)`，IP 取控制连接本端地址，端口 bind 0 由内核分配 |
| | `PORT` | 502 不支持（主动模式） |

未识别的命令回复 500/502。

### 5.2 回复码（RFC 959）

`220` 服务就绪 · `221` 再见 · `226` 传输完成 · `227` 进入被动模式 · `230` 登录成功 · `250` 操作成功 · `257` 路径名 · `331` 需要密码 · `350` 需要后续命令 · `421` 超时/服务关闭 · `425` 无法打开数据连接 · `426` 传输中断 · `500`/`502` 命令错误/未实现 · `503` 命令顺序错误 · `530` 未登录/认证失败 · `550` 文件操作失败

### 5.3 路径 jail

```
虚拟路径（客户端所见）      真实 VFS 路径
/                      →  /data
/dir1/file.txt         →  /data/dir1/file.txt
/../etc/passwd         →  /data/etc/passwd   （.. 在根处被弹栈抑制）
```

规范化算法：

1. 参数以 `/` 开头则从根开始，否则拼接当前 `cwd`
2. 按 `/` 分段处理：空段和 `.` 跳过；`..` 弹出末尾一段（已到根则忽略）；其余压栈（单段长度校验）
3. 重新拼成 `/seg1/seg2/...`，最终真实路径 = `root_path + 虚拟路径`

由于 `..` 永远无法弹出根以下，真实路径必然位于 `/data` 之内。

### 5.4 会话状态机

```
accept → 220 → USER → 331 → PASS → 230 (authed)
                          ↘ 530 (密码错, 可重试)
authed 状态下才允许 PWD/CWD/LIST/RETR/STOR/... 等命令，否则 530
任何时刻: QUIT → 221 关闭; 空闲 5 分钟 → 421 关闭; ftp stop → 强制断开
```

### 5.5 数据通道（PASV）流程

以 `RETR` 为例：

1. 客户端发 `PASV` → 服务器创建 `pasv_listen_fd`（bind 端口 0），回复 `227 Entering Passive Mode (192,168,1,100,p1,p2)`
2. 客户端发 `RETR file.txt` → 服务器 `fopen("/data/file.txt", "rb")`，失败则 550
3. 服务器 `accept()` 数据连接（10 秒超时，失败 425），回复 `150 Opening data connection`
4. 4 KB 堆缓冲循环 `fread` → `send`
5. 完毕回复 `226 Transfer complete`，关闭数据连接和 `pasv_listen_fd`

`STOR`/`APPE` 方向相反（`recv` → `fwrite`），`LIST`/`NLST` 则把格式化文本写入数据连接。

### 5.6 超时与停止

| 场景 | 超时/行为 |
|---|---|
| 控制连接空闲 | `SO_RCVTIMEO` 300 s，超时回 421 并关闭会话，回到 accept |
| 数据连接 accept | 10 s，失败回 425 |
| 数据连接收发 | 30 s，失败回 426 |
| `ftp stop` | 置 `stop_req` 标志 → `shutdown()` listen_fd/client_fd 打断阻塞 → server task 自行 close 所有 fd → 信号量通知 → 删除任务 |

> 注意：不直接从 `ftp stop` 所在任务 close socket（lwIP 跨任务 close 有竞态），而是 `shutdown()` 唤醒后由 server task 自己清理。

### 5.7 资源占用

- server task 栈：6144 字节
- 堆：命令行缓冲 512 B + 路径缓冲若干 + 传输缓冲 4 KB（task 启动时一次性分配）
- 同时仅 1 个控制客户端 + 1 条数据连接

## 6. 构建系统集成

`main/common/CMakeLists.txt`：

```cmake
set(COMMON_SRCS
    ...
    "common/ftp_server/app_ftpsrv.c"
    "common/ftp_server/cmd_ftp.c"
)

set(COMMON_INCLUDE_DIRS
    ...
    "common/ftp_server"
)
```

`main/CMakeLists.txt` 的 `PRIV_REQUIRES` 增加 `lwip`。

`cmdmgr.c` 的 `cmdmgr_init()` 中在 `register_nvs()` 后增加 `register_ftp_cmd();`。

## 7. 验证步骤

1. **编译**：`idf.py build`（target esp32s31）零警告通过
2. **运行**：烧录后控制台等待 WiFi STA 拿到 IP（或插网线走以太网）
3. **启动**：控制台 `ftp start`，确认日志显示监听 21 端口；`ftp status` 查看状态
4. **PC 端功能测试**（`ftp <板子IP>` 或 FileZilla，账号 `esp32/esp32`）：
   - [ ] 错误密码 → 530 拒绝
   - [ ] `put test.txt` 上传 → `ls` 可见 → 设备 `/data/test.txt` 存在
   - [ ] `get test.txt` 下载 → 与源文件内容一致（可用 md5 比对）
   - [ ] 传输大文件（数 MB）完整无截断
   - [ ] `mkdir dir` / `cd dir` / `put` / `cd ..` / `del` / `rmdir` 目录操作正常
   - [ ] `ftp stop` 时若有会话进行中，能干净断开；`ftp start` 可重新启动
5. **回归**：`help` 中可见 `ftp` 命令，其余控制台命令不受影响

## 8. 后续可扩展项（不在本期范围）

- 多客户端并发会话（每客户端独立 task）
- 主动模式 `PORT`
- TLS（FTPS，需 mbedTLS，资源开销大）
- 账号写入 NVS/JSON 配置文件，支持控制台修改
- 开机自启开关（Kconfig 配置项）

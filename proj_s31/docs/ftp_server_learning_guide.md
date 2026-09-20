# FTP Server 学习式实施指南

> 项目：`proj_s31`（目标芯片 `esp32s31`）
> 日期：2026-09-20
> 目标：**以学习 FTP 协议为目的**，自己动手实现一个 FTP server。
> 本文档是执行方案：按里程碑（M0~M6）推进，每个里程碑包含【协议知识点】→【要实现什么】→【如何验证】→【常见坑】，不包含完整代码。
> 总体架构设计见姊妹篇 [ftp_server_plan.md](ftp_server_plan.md)。

---

## 0. 前置知识：FTP 协议十分钟入门

FTP（RFC 959）是 1971 年诞生的古老协议，和现代 HTTP 风格差异很大，理解下面 5 个概念就掌握了它的骨架：

### 0.1 双连接模型（FTP 最核心、最独特的特征）

FTP **用两条 TCP 连接**干活：

- **控制连接**（control connection）：客户端连服务器的 **21 端口**。全程只走**文本命令和应答**，一行一条，以 `\r\n` 结尾。登录、切目录、发起传输都走这里。它在整个会话期间保持不断开。
- **数据连接**（data connection）：每次传文件内容、传目录列表时**临时新建一条**，传完就关。LIST 的输出、RETR 的文件内容、STOR 的上传内容都走这里。

> 学习要点：这是 FTP 与 HTTP 最大的不同。你写 server 时会同时管理两个 socket，务必分清每条命令的回应该发到哪个 socket 上。

### 0.2 数据连接谁来建？主动 vs 被动

- **主动模式（PORT）**：客户端告诉服务器"你来连我"，服务器用 **20 端口**主动回连客户端。客户端在 NAT/防火墙后面时几乎必挂 → 现代基本不用。
- **被动模式（PASV）**：客户端发 `PASV`，服务器**新开一个临时端口监听**，把 IP+端口告诉客户端，由客户端来连。这是现在所有客户端的默认模式。**你的 server 只需实现被动模式**。

PASV 应答格式（RFC 959 §4.1.3）：

```
227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)
```

- `h1.h2.h3.h4` 是服务器 IP 的 4 个字节
- 端口号 = `p1 * 256 + p2`（高低字节拆开传输——这是 1970 年代的编码方式，注意理解为什么）

### 0.3 回复码（三位数字）

每条命令服务器回一行 `数字 说明文字`。第一位数字含义：

| 第一位 | 含义 | 例子 |
|---|---|---|
| 1xx | 肯定预备应答：已开始，等下一条消息 | `150 Opening data connection`（传输开始） |
| 2xx | 成功完成 | `226 Transfer complete`、`230 Login successful` |
| 3xx | 中间状态，需要更多信息 | `331 Need password`（USER 之后要 PASS） |
| 4xx | 临时失败，可重试 | `425 Can't open data connection` |
| 5xx | 永久失败 | `530 Not logged in`、`550 File not found` |

第二位数字细分（0 语法 / 1 信息 / 2 连接 / 3 认证 / 5 文件系统）。完整列表见 RFC 959 §4.2 和附录。

**传输类命令的固定套路**（RETR/STOR/LIST 都一样）：

```
150  → 传输开始（先建立好数据连接）
...数据在数据连接上传输...
226  → 传输成功完成（或 426 中断 / 550 失败）
```

### 0.4 会话状态机

```
连接建立 → 服务器主动发 220（欢迎语）
客户端: USER esp32   → 331（用户名OK，要密码）
客户端: PASS esp32   → 230（登录成功） / 530（失败）
登录后才允许 PWD/CWD/LIST/RETR/STOR...，否则回 530
QUIT → 221 再见，关闭控制连接
```

### 0.5 推荐阅读

- RFC 959 原文：`https://www.rfc-editor.org/rfc/rfc959`（重点读 §4.1 命令、§4.2 应答、§5 声明式规范、§6 状态图——附录里的状态图非常有助于写代码）
- 不必通读，边做边查

---

## 1. 工程骨架（每个里程碑共用）

在 `proj_s31/main/common/ftp_server/` 下新建：

| 文件 | 职责 |
|---|---|
| `app_ftpsrv.h` | 公开 API：`app_ftpsrv_start(cfg)` / `app_ftpsrv_stop()` / `app_ftpsrv_is_running()` + `app_ftpsrv_config_t`（port/user/pass/root_path，可缺省） |
| `app_ftpsrv.c` | FTP 协议核心（本指南的主角，全部由你实现） |
| `cmd_ftp.h` / `cmd_ftp.c` | 控制台命令 `ftp start/stop/status`，参照 `cmd_nvs.c` 的 argtable3 写法 |

构建改动：`main/common/CMakeLists.txt` 加源文件与头文件目录；`main/CMakeLists.txt` 的 `PRIV_REQUIRES` 加 `lwip`；`cmdmgr.c` 里注册命令。

代码风格对齐现有工程：纯 C、`ESP_LOGx` 打印、`esp_err_t` 返回值、头文件 guard 用 `__APP_FTPSRV_H__` 风格。

**建议先写的几个底层工具函数**（后续每个里程碑都会用到）：

```
ftp_send_fmt(ctrl_fd, "220 Hello\r\n")      // 格式化发送应答（封装 vsnprintf + send 全量循环）
ftp_recv_line(ctrl_fd, buf, len)            // 收一行控制命令（直到 \r\n），返回去掉行尾的字符串
```

> 思考：为什么 send 要写"全量循环"？（提示：`send()` 返回值可能小于你给的长度。查 `man send` 的返回值语义。）

---

## 2. 里程碑

### M0 — 骨架：能连上、能看到欢迎语

**协议知识点**
- 服务器 accept 到新连接后**主动**发 `220`（RFC 959 §4.1.1：220 = service ready）
- 每个应答都必须以 `\r\n` 结尾，少一个字符客户端就会挂起等数据——这是新手最常踩的坑

**要实现**
1. server task：`socket()` → `bind(0.0.0.0:21)` → `listen()` → `accept()` 循环
2. accept 后立即发 `220 ESP32 FTP Server ready\r\n`
3. 收到的任何命令原样打日志，统一回 `502 Command not implemented\r\n`
4. 客户端断开（recv 返回 0）后关闭连接，回到 accept

**验证**
```
# PC 上（不需要 ftp 客户端，用 telnet/nc 手动模拟协议——强烈建议全程用它来学习）
telnet <板子IP> 21
# 期望看到: 220 ESP32 FTP Server ready
# 手输入: HELLO
# 期望收到: 502 Command not implemented
```

**常见坑**
- `lwip/sockets.h` 与 POSIX socket API 基本一致；记得 `PRIV_REQUIRES` 加 `lwip`
- task 栈建议 6 KB 起步；`accept`/`recv` 是阻塞调用，要在独立 task 里跑

---

### M1 — 登录：USER / PASS / QUIT 状态机

**协议知识点**
- `USER` 成功 → `331`（需要密码）；`PASS` 正确 → `230`，错误 → `530`
- 命令顺序错误（比如没发 USER 直接发 PASS）→ `503 Bad sequence of commands`
- 体会"会话状态"：server 需要给每个连接记一个 `authed` 标志（虽然你是单客户端，也请把它放进 session 结构体里，为多客户端扩展留好形状）
- `QUIT` → `221` 然后**由服务器端关闭**控制连接

**要实现**
1. 命令分发表：`{命令名, 处理函数, 是否需要登录}`，循环里查表分发
2. session 结构体：`ctrl_fd`、`authed`、`cwd` 等字段
3. 命令：`USER`、`PASS`（比对配置的用户名/密码）、`QUIT`、`NOOP`（回 200）
4. 未识别命令保持 `502`；未登录就发别的命令回 `530`

**验证**
```
telnet <板子IP> 21
USER esp32        → 331
PASS wrong        → 530
PASS esp32        → 230
NOOP              → 200
QUIT              → 221 且连接被关闭
再用 ftp <板子IP> 命令行客户端试一次完整登录
```

**常见坑**
- FTP 命令**大小写不敏感**：`user`/`USER`/`User` 都要能识别（`strcasecmp`）
- 命令参数可能为空（`USER` 不带参数）——想好怎么回（500 语法错误）

---

### M2 — 目录漫游：PWD / CWD / CDUP 与路径 jail

**协议知识点**
- FTP 客户端看到的是一套**虚拟路径**（`/`、`/dir1`），server 负责映射到真实文件系统（`/data`、`/data/dir1`）
- `PWD` 的应答格式是 `257 "<路径>"`——路径**带双引号**（RFC 959 §5.4）
- **安全问题**：必须处理 `..`，否则 `CWD ../../../../` 就能访问整个文件系统（路径穿越攻击，对应 CVE 在真实 FTP server 里屡见不鲜）

**要实现**
1. 路径规范化函数：拼接 cwd + 参数 → 按 `/` 分段 → 处理 `.`（跳过）和 `..`（弹栈，到根即止）→ 拼回绝对虚拟路径
2. 虚拟路径 → 真实路径：`"/data" + 虚拟路径`
3. 命令：`PWD`/`XPWD`、`CWD`（用 `stat()` 确认目标是存在的目录）、`CDUP`（等价 `CWD ..`）
4. `SYST`（回 `215 UNIX Type: L8`）、`TYPE`（`I` 和 `A` 都回 200，统一按二进制处理）

**验证**
```
ftp <板子IP> 登录后:
pwd            → 257 "/"
cd /           → 250
cd ..          → 250（仍在根，不报错）
cd 不存在的目录  → 550
```
再用 `stat()` 手动在板子上 `mkdir` 几个目录（或先用 M5 的 MKD）交叉验证。

**常见坑**
- 这一段**纯字符串操作、不涉及数据连接**，是练手状态机的好机会
- 规范化函数单独写、单独测（可以先用 PC 上的单元测试思路手算几个用例：`/a/b/../../c` → `/c`）

---

### M3 — 第一个数据连接：PASV + LIST

**这是整个项目最关键的里程碑**，过了这关 FTP 就懂了大半。

**协议知识点**
- `PASV` 流程：server 新建 socket → `bind` 端口 0（内核分配临时端口）→ `listen` → `getsockname` 拿到端口号 → 回复 `227 (h1,h2,h3,h4,p1,p2)`
- 应答里的 IP 从哪来？`getsockname(控制连接)` 拿**本端地址**——这样 WiFi/以太网哪个接口都通用，不用挂 IP 事件
- `LIST` 的数据（目录列表文本）是**通过数据连接发的，不是控制连接**；控制连接上只回 150/226
- LIST 输出惯例是 Unix `ls -l` 风格：`-rw-r--r-- 1 owner group 1234 Sep 20 10:00 filename`（FileZilla 按这个格式解析）

**要实现**
1. `PASV` 命令处理（如上流程）；只允许同时存在一个 PASV 监听（新 PASV 先关旧的）
2. `LIST` 命令：
   - `accept()` 数据连接（设 10 秒超时，超时回 425）
   - 控制连接回 `150`
   - `opendir/readdir/stat` 遍历 cwd，每行以 `\r\n` 结尾写入数据连接
   - 关闭数据连接，控制连接回 `226`
3. `NLST`（只发文件名）作为简化版一并实现

**验证**
```
ftp <板子IP> 登录后:
ls        → 能看到 /data 下的文件列表
```
用 Wireshark 抓包（过滤器 `ftp || ftp-data`），对照观察：控制连接上的 PASV→227、LIST→150→226，以及独立数据连接上的列表文本——**强烈建议抓一次包，这是理解双连接最好的方式**。

**常见坑**
- 150 必须在数据连接 **accept 成功之后**发；顺序错了客户端会卡住
- 目录列表**每一行**都要 `\r\n`（不是 `\n`）
- FATFS 的 `stat` 有 `st_mode`/`st_size`/`st_mtime`，格式化时间用 `strftime("%b %d %H:%M")`

---

### M4 — 下载：RETR

**协议知识点**
- `RETR <file>`：150 → 数据连接发送文件内容 → 226；文件不存在 550
- 文件内容**原样字节流**，不做任何转换（这就是 `TYPE I` 二进制的意义；FTP 历史上 ASCII 模式会做换行转换，现代统一二进制）

**要实现**
1. `RETR`：解析路径 → `fopen(rb)`（失败 550）→ accept 数据连接 → 150 → 4 KB 缓冲循环 `fread`/`send` → 226
2. 数据收发设 30 秒超时（防客户端挂死拖死 server）
3. 传输中要关注 `send()` 部分发送的处理

**验证**
```
ftp> get test.txt
```
下载后与源文件做 md5 比对。试一个大文件（几 MB）确认完整无截断。

---

### M5 — 上传：STOR / APPE

**协议知识点**
- `STOR` 方向与 RETR 相反：数据连接上 server 是**收**的一方
- 客户端关闭数据连接 = 文件传完（recv 返回 0）——FTP 数据连接不携带长度信息，**靠关连接表示结束**，这是与 HTTP Content-Length 截然不同的设计，值得体会
- `APPE` = 追加模式打开（`"ab"`），其余与 STOR 相同

**要实现**
1. `STOR`：150 → `fopen(wb)` → 循环 `recv`/`fwrite` → recv 返回 0 → 226
2. `APPE`：同上，打开方式不同
3. 磁盘写满等 fwrite 失败 → 426 并删除写了一半的文件（想想为什么）

**验证**
```
ftp> put 本地文件.txt
ftp> ls            # 能看到新文件
```
回控制台确认 `/data` 下文件存在且内容正确（可以 get 回来再比对）。

---

### M6 — 完善与健壮性

**协议/工程知识点**
- 常用辅助命令：`DELE`（删文件 250）、`MKD`/`RMD`、`RNFR`→`RNTO`（两步重命名，RNFR 回 350 记住房子的"中间态"）、`SIZE`（213）、`MDTM`（213，时间格式 `YYYYMMDDHHMMSS` UTC）
- `FEAT`（211 多行应答：`211-xxx` 开头、`211 End` 结尾——学习 FTP 多行应答格式）
- 控制连接空闲超时：`SO_RCVTIMEO` 300 秒，超时回 421 关闭
- 已有会话时新连接的处理策略（简单起见：直接拒绝）
- `ftp stop` 的实现：跨任务**不要直接 close socket**（lwIP 竞态），而是 `shutdown()` 唤醒阻塞的 accept/recv，由 server task 自己 close 并退出，用信号量同步

**验证**
- FileZilla 连接，点界面上的新建目录/删除/重命名/刷新按钮，全部正常
- `ftp stop` 在传输进行中执行，能干净断开、可重新 `ftp start`
- 空闲 5 分钟自动断开

---

## 3. 调试工具箱

1. **telnet / nc 手动会话**：M0~M2 不需要 ftp 客户端，手敲命令最能理解协议
2. **Wireshark**：过滤器 `ftp || ftp-data`，M3 必用
3. **ftp 命令行客户端**：`ftp -d <ip>` 开调试模式，能看到每条命令与应答原文
4. **FileZilla**：M6 用它做完整功能回归（它的消息日志面板也会显示全部协议交互）
5. **板端日志**：`ESP_LOGI` 打出每条收到的命令和状态迁移

## 4. 建议的推进节奏

| 里程碑 | 预计耗时 | 收获 |
|---|---|---|
| M0 | 0.5 天 | socket/task 骨架、220 应答 |
| M1 | 0.5 天 | 命令分发、会话状态机、认证 |
| M2 | 0.5 天 | 路径规范化与 jail（安全思维） |
| M3 | 1 天 | **双连接模型、PASV——核心收获** |
| M4/M5 | 0.5 天 | 文件传输、流式收发、关连接语义 |
| M6 | 1 天 | 健壮性、完整命令集、工程收尾 |

每完成一个里程碑抓一次包对照 RFC，比直接读 RFC 理解快得多。遇到协议疑问随时来问，代码卡住可以让我 review 你的实现（而不是替你写）。

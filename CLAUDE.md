# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 仓库概况

这是 Espressif ESP-IDF（IoT Development Framework）的主仓库（master 分支），包含芯片支持包、组件库、构建系统、示例与测试框架。注意：本 fork 在 `PREVIEW_TARGETS` 中包含 `esp32s31`（见 [tools/idf_py_actions/constants.py](tools/idf_py_actions/constants.py)），根目录下的 [proj_s31/](proj_s31/) 是当前活跃开发的应用项目（源自 ethernet/iperf 示例，目标 esp32s31，**未纳入 git 跟踪**），结构见下文「proj_s31 应用结构」。

## 环境准备

每次新开 shell 使用 ESP-IDF 前必须先 source 环境脚本：

```bash
source ./export.sh        # 首次使用需先运行 ./install.sh 安装工具链
```

`idf.py` 命令仅在 export 之后可用；未 export 的 shell 里构建会失败。

## 常用命令

所有构建命令都在**项目目录**（如 `proj_s31/` 或 `examples/get-started/hello_world/`）下执行，而不是在仓库根目录：

```bash
idf.py set-target esp32s31   # 设置目标芯片（不带参数可列出支持的目标）
idf.py menuconfig            # Kconfig 文本配置菜单
idf.py build                 # 构建 app + bootloader + 分区表
idf.py -p PORT flash monitor # 烧录并打开串口监视器（Ctrl-] 退出）
idf.py app app-flash         # 只构建/烧录应用部分
idf.py size                  # 固件体积分析（也可直接用 tools/idf_size.py）
```

### 测试

- 测试框架是 pytest + pytest-embedded（见 [pytest.ini](pytest.ini)、[conftest.py](conftest.py)），测试脚本命名必须为 `pytest_*.py`。
- 运行单个测试（需要连接目标板）：

```bash
cd <test_dir>   # 例如 examples/get-started/hello_world
pytest --target esp32s31 pytest_hello_world.py
pytest --target esp32s31 -k "test_case_name"   # 按用例名过滤
```

- 不带硬件时可用 `--target linux` 跑 host 测试（部分组件有 linux port），或用 `qemu` env marker 跑 QEMU 测试。
- 测试用 sdkconfig 变体放在项目目录下的 `sdkconfig.ci.*` 文件中。
- [tools/test_apps/](tools/test_apps/) 与 [tools/test_build_system/](tools/test_build_system/) 是针对构建系统自身的测试。

### 代码风格

- Python：ruff（配置 [ruff.toml](ruff.toml)），pre-commit 钩子见 [.pre-commit-config.yaml](.pre-commit-config.yaml)，运行 `pre-commit run --all-files`。
- C/C++：astyle，通过 [tools/format.sh](tools/format.sh) 调用（规则在 [tools/ci/astyle-rules.yml](tools/ci/astyle-rules.yml)）。
- CI 检查脚本集中在 [tools/ci/](tools/ci/)（如 `check_public_headers.py`、`check_kconfigs.py`、`check_copyright_config.yaml` 等），改动组件头文件/Kconfig 时注意相关检查。

## 架构要点

### 组件（Component）模型

- 一切代码都组织为 component：每个 [components/](components/) 子目录是一个组件，通过各自 `CMakeLists.txt` 里的 `idf_component_register(SRCS ... INCLUDE_DIRS ... REQUIRES ...)` 声明源码、头文件目录与依赖。构建系统（[tools/cmake/](tools/cmake/)）据此自动推导组件依赖图，依赖传递基于 `include/` 公共头目录。
- 应用项目（如 `proj_s31/`）的 `main/` 目录本身就是一个组件；项目根 `CMakeLists.txt` 用 `include($ENV{IDF_PATH}/tools/cmake/project.cmake)` 引入构建系统。
- 核心组件分两层（详见 [components/README.md](components/README.md)）：
  - **G0（硬件层）**：`soc`（寄存器定义、芯片能力 `SOC_*_SUPPORTED`）、`hal`（硬件抽象，`*_ll_*` 低层函数 → `*_hal_*` 例程）、`esp_rom`、`riscv`/`xtensa`（架构层）、`esp_common`。只允许组内相互依赖，尽量不对上依赖。
  - **G1（系统层）**：`esp_hw_support`、`esp_system`、`esp_libc`、`spi_flash`、`freertos`（ESP-IDF 定制版 FreeRTOS，SMP 支持）、`log`、`heap`。
  - 驱动分新旧两套：`driver/` 是旧版驱动，`esp_driver_*/`（如 `esp_driver_gpio`、`esp_driver_uart`）是新版驱动，配套 `esp_hal_*` HAL 组件。

### 多目标支持

- 芯片相关代码按 `esp32/`、`esp32s3/`、`esp32s31/` 等子目录组织在组件内部（如 `soc/esp32s31/`、`esp_hw_support/port/esp32s31/`），由 CMake 按 `IDF_TARGET` 选择编译。改动跨芯片代码时，注意所有目标目录下的对应实现。
- 芯片能力用 `soc_caps.h` 中的 `SOC_*` 宏表达，写条件代码优先用能力宏而非芯片型号判断。

### 配置与链接

- Kconfig：各组件的 `Kconfig` 文件汇总成 menuconfig；构建产物是 `sdkconfig` 与 `sdkconfig.h`。`sdkconfig.rename` 用于配置项改名兼容。
- 链接：组件通过 `linker.lf`（linker fragment，由 [tools/ldgen/](tools/ldgen/) 处理）控制段放置，如把代码放入 IRAM/flash。
- Bootloader：独立的小型项目（[components/bootloader/](components/bootloader/)），不支持大部分组件。

### proj_s31 应用结构

当前主要开发工作在 `proj_s31/` 内，其 `main/` 组件用**拆分的 include 式 CMakeLists** 组织（`main/CMakeLists.txt` include `common/CMakeLists.txt` 与 `bsp/CMakeLists.txt`，这两个文件只设置 `COMMON_SRCS`/`BSP_SRCS`/`*_INCLUDE_DIRS` 变量，**不调用** `idf_component_register`，新增源文件要加到对应变量里）：

- `main/common/`：通用模块
  - `filemgr/`：FATFS 挂载到 `/data`（`storage` 分区，12 MB，带磨损均衡，分区表见 `proj_s31/partitions.csv`）
  - `cmd_system/`：控制台 REPL（`cmdmgr_init()` 基于 `esp_console_new_repl_uart`，prompt `esp32>`，历史存 `/data/history.txt`）。**新控制台命令的模式**：写一个 `register_xxx()` 用 `esp_console_cmd_register()` 注册（参数解析用 argtable3，参照 `cmd_nvs.c`），然后在 `cmdmgr_init()` 或 `app_main()` 中调用
  - `cmd_nvs/`、`ftp_server/`（实现中的 FTP server，设计文档在 `proj_s31/docs/`）
- `main/bsp/bspwifi/`：WiFi 管理。**ops 表模式**：`bsp_wifimgr.c` 通过 `get_wifi_{sta,ap,apsta}_ops()` 取模式对应的函数表（start/stop/change_cfg/on_event），STA/AP/APSTA 三个实现文件各自注册 ops；WiFi 配置持久化在 `/data/wifi_info.json`（cJSON 解析）
- `main/Kconfig` + `main/common/Kconfig`：项目级配置项（如 `CONFIG_FTP_SERVER_SUPPORT`），子 Kconfig 用 `source` 引入时需保证文件存在（不存在会 kconfgen 报错，可用 `orsource`）
- 第三方依赖走组件管理器：`main/idf_component.yml` 声明（cjson、ethernet_init、iperf-cmd、wifi-cmd），锁定在 `dependencies.lock`，下载到 `managed_components/`
- `proj_s31/docs/`：功能设计与实施计划 markdown（wifi_sta、ftp_server 等）

### 工具链目录速查

- [tools/idf.py](tools/idf.py)：构建前端，action 实现在 [tools/idf_py_actions/](tools/idf_py_actions/)。
- [tools/idf_tools.py](tools/idf_tools.py)：工具链下载管理（install.sh/export.sh 的核心）。
- [tools/kconfig_new/](tools/kconfig_new/)：menuconfig 实现；[tools/cmakev2/](tools/cmakev2/)：下一代构建系统（实验性）。
- [tools/mocks/](tools/mocks/)：host 测试用 mock。
- [docs/](docs/)：Sphinx 文档源码；[examples/](examples/)：按外设/协议分类的示例项目。

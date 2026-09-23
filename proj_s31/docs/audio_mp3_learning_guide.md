# ESP32-S31 麦克风录音 → WAV → MP3 学习指南

> 目标板：ESP32-S31 Function-Coreboard-1 V1.0（板载 ES8311 音频 codec）
> 学习方式：分步执行，每步自己动手写代码，通过检查点后进入下一步
> 最终效果：控制台 `audio_rec -t 10` 录音 → `/data/wav/rec_<时间戳>.wav` → 自动转码 → `/data/mp3/rec_<时间戳>.mp3`，用 FTP 取回 PC 播放

---

## 整体架构

```
麦克风(模拟) ──→ ES8311 codec ──I2S(PCM 数据)──→ ESP32-S31 ──→ /data/wav/xxx.wav
                     ↑                                                    │
                  I2C(配置寄存器)                                    Shine 编码
                     │                                                    ↓
              ESP32-S31 I2C master ────────────────────→ /data/mp3/xxx.mp3
```

**两条总线，各司其职**：
- **I2C**：只负责"控制"——给 ES8311 写寄存器（设采样率、增益、使能 ADC），不传输音频
- **I2S**：只负责"数据"——ES8311 把麦克风模拟信号转成 PCM，通过 I2S 源源不断送给 ESP32

---

## 第 0 步：理论学习

### 0.1 I2S 总线基础

| 信号线 | 名称 | 作用 |
|---|---|---|
| BCK (BCLK) | 位时钟 | 每个数据位跳变一次，频率 = 采样率 × 位深 × 声道数 |
| WS (LRCK) | 帧时钟 | 高低电平区分左/右声道，频率 = 采样率 |
| SD (DIN/DOUT) | 数据线 | 串行传输 PCM 采样值 |
| MCLK | 主时钟 | 给 codec 内部电路用，通常是采样率的 256 倍 |

- **Philips 标准格式**：WS 跳变后第二个 BCK 沿开始传数据，最高位先传
- **主从关系**：ESP32-S31 做 master（产生 BCK/WS/MCLK），ES8311 做 slave

### 0.2 ES8311 的角色

ES8311 是一颗「ADC + DAC」codec 芯片：
- **录音方向（ADC）**：麦克风模拟信号 → ES8311 内部 ADC → PCM → I2S SD 送出
- **放音方向（DAC）**：I2S SD 收 PCM → ES8311 内部 DAC → 耳机/喇叭
- ESP32 通过 **I2C**（7 位地址 `0x18`）配置它的寄存器

我们不用手写寄存器配置——乐鑫官方组件 `espressif/esp_codec_dev` 内置了 ES8311 驱动（`es8311_codec_new()`），统一抽象成 `esp_codec_dev_open/read/write` 接口。

### 0.3 PCM / WAV / MP3

- **PCM**：原始采样点序列。16bit 单声道 16kHz 时，1 秒 = 16000 × 2 = 32000 字节
- **WAV** = 44 字节 RIFF 文件头（描述采样率/位深/声道/数据长度）+ PCM 裸数据。无任何压缩
- **MP3**：有损压缩（心理声学模型丢弃人耳不敏感的成分），体积约为 PCM 的 1/10

### 0.4 为什么 MP3 编码选 Shine

乐鑫官方 `esp_audio_codec` 组件**只有 MP3 解码，没有编码**。嵌入式 MP3 编码的事实标准是 [Shine](https://github.com/toots/shine)：
- 定点运算，不依赖 FPU（ESP32 系列浮点慢）
- 参考移植：[fknrdcls/mp3_shine_esp32](https://github.com/fknrdcls/mp3_shine_esp32)
- ESP32-S3 上 48kHz 立体声实时编码约占单核 53% CPU；我们 16kHz 单声道离线转码毫无压力
- 核心 API：`shine_initialise()` → 循环 `shine_encode_buffer()` → `shine_flush()`

### 阅读材料（仓库内现成）

- 官方 ES8311 例程（本次开发的"模板"）：
  `examples/peripherals/i2s/i2s_codec/i2s_es8311/main/i2s_es8311_example.c`
- 录音写 WAV 参考（用 PDM 麦+SD卡，我们只借 WAV 头写法）：
  `examples/peripherals/i2s/i2s_recorder/main/i2s_recorder_main.c`
- WAV 头结构体定义：
  `examples/peripherals/i2s/i2s_examples_common/format_wav.h`

### ✅ 检查点

能回答：
1. 录音时 I2C 和 I2S 各自干什么？
2. 16kHz / 16bit / 单声道录 10 秒，PCM 数据多少字节？
3. WAV 和 PCM 的关系是什么？

---

## 第 1 步：跑通官方 ES8311 例程

**目标**：不写自己的代码，先编译烧录官方例程，验证硬件链路（麦克风 → ES8311 → ESP32）。

```bash
cp -r examples/peripherals/i2s/i2s_codec/i2s_es8311 ~/i2s_es8311_test
cd ~/i2s_es8311_test
idf.py set-target esp32s31
idf.py menuconfig   # 按原理图修改引脚：I2C_SDA/SCL、I2S_MCLK/BCK/WS/DIN/DOUT、PA
idf.py build flash monitor
```

- 例程默认 echo 模式：麦克风进、耳机出。对着麦克风说话应能听到回声
- **若板子没接耳机/喇叭**：把例程的 echo 循环改成读 100 个采样打印数值，对着麦克风吹气看数值是否剧烈变化（数值在 0 附近不动说明链路不通）

排错思路：
- I2C 不通 → 用 `i2c_master_probe(bus_handle, 0x18, 100)` 探测 ES8311 是否应答
- I2S 读到全 0 → 检查 WS/BCK 引脚是否接反、slot 格式是否匹配

### ✅ 检查点

听到回声（或看到采样值随声音变化）→ 硬件链路正常，进入下一步。

---

## 第 2 步：proj_s31 里搭 bspaudio 模块骨架（只初始化，不录音）

**目标**：按项目现有 bspwifi 的模式建模块，完成 codec 初始化，注册一个测试命令。

### 2.1 新建 `proj_s31/main/bsp/bspaudio/` 目录，四个文件

**`bsp_audiomgr.h`** — 对外接口（仿 `bsp_wifimgr.h` 的头文件风格）：
```c
#pragma once
#include "esp_err.h"
esp_err_t bsp_audiomgr_init(void);
```

**`bsp_audiomgr.c`** — 初始化流程，照抄第 1 步例程的两个函数并简化：
1. `i2s_driver_init()`：`i2s_new_channel()` 只建 RX（tx 传 NULL）→ `i2s_channel_init_std_mode()` → `i2s_channel_enable()`
   - slot 用 `I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)`
2. `es8311_codec_init()`：I2C master bus → `audio_codec_new_i2c_ctrl()` → `audio_codec_new_i2s_data()` → `es8311_codec_new()` → `esp_codec_dev_new()` → `esp_codec_dev_open()` → `esp_codec_dev_set_in_gain()`
   - 录音-only：`codec_mode = ESP_CODEC_DEV_WORK_MODE_ADC`，`pa_pin = -1`
   - 删去例程中 TX / echo / music 相关代码

**`bsp_audiocmd.c`** — 先只注册 `audio_info` 命令打印 codec 状态（仿 `bsp_wificmd.c` 的 argtable3 写法），在 `bsp_audiomgr_init()` 内调用 `audio_cmd_init()`。

**`Kconfig`** — `menu "bsp audio"`，以下全部做成配置项（默认占位值，按原理图改）：

| 配置项 | 类型 | 默认 |
|---|---|---|
| I2C_SDA / I2C_SCL | int | 占位 |
| I2S_MCLK / BCK / WS / DIN | int | 占位 |
| SAMPLE_RATE | int | 16000 |
| MIC_GAIN | int | 30 (dB) |

### 2.2 获取 esp_codec_dev 组件（直接下载 vendor 方式）

不走组件管理器（`idf_component.yml`），而是**直接下载组件包放到 IDF 的 components 目录**（`$IDF_PATH/components/esp_codec_dev`，IDF 构建系统会自动扫描该目录，无需配置）。好处：
- 离线可编译，不依赖网络拉取 `managed_components/`
- 版本完全自己掌控，想升级就重新下载新版替换

```bash
# 从组件注册表下载指定版本（完整版本列表见文末附录）
curl -L -o /tmp/esp_codec_dev.zip \
  https://components-file.espressif.com/components/espressif/esp_codec_dev/1.6.2/espressif__esp_codec_dev-v1.6.2.zip
unzip /tmp/esp_codec_dev.zip -d $IDF_PATH/components/esp_codec_dev
```

**版本选择建议**：
- 用 **1.x 版本**（如 1.6.2，无外部依赖，纯净）
- **不要用 2.0.0-beta 版**：它额外依赖 `usb_host_uac` 组件，离线 vendor 时还得再下载一层，没必要
- 想换版本：删 `$IDF_PATH/components/esp_codec_dev` 重新解压即可

> 注意事项：
> 1. vendor 后**不要**再在 `main/idf_component.yml` 里声明 `esp_codec_dev`，否则组件管理器会再下载一份到 `managed_components/`，造成重复。
> 2. 该方式会把源码放进 esp-idf git 仓库（显示为 untracked 文件），`git clean -fdx` 等操作会误删，注意备份；升级 IDF 版本前先确认该目录不受影响。

### 2.3 接线进工程（4 处改动）

1. `main/bsp/CMakeLists.txt`：`BSP_SRCS` 加 `"bsp/bspaudio/bsp_audiomgr.c"` 等，`BSP_INCLUDE_DIRS` 加 `"bsp/bspaudio"`
2. `main/bsp/Kconfig`：`if BSP_ENABLE` 块内加 `source "$IDF_PATH/proj_s31/main/bsp/bspaudio/Kconfig"`
3. `main/CMakeLists.txt`：`PRIV_REQUIRES` 加 `esp_driver_i2s esp_driver_i2c esp_codec_dev`
4. `app_main.c`：`app_filemgr_mount()` 之后调 `bsp_audiomgr_init()`

### ✅ 检查点

编译通过；启动日志出现 codec init 成功；控制台 `audio_info` 有输出。

---

## 第 3 步：录音存 WAV

**目标**：`audio_rec -t 10` 录 10 秒到 `/data/wav/xxx.wav`，FTP 拉回 PC 能播放。

### 3.1 新建 `bsp_audiorec.c`（录音任务）

流程：
1. `mkdir("/data/wav", 0775)`（已存在则忽略错误）
2. `fopen` 打开目标文件
3. **先写 44 字节占位 WAV 头**（参考 `format_wav.h` 的 `wav_header_t`；因为支持中途停止，数据长度未知，先填 0）
4. 循环：`esp_codec_dev_read()` → `fwrite()` → 累计字节数，达到 `采样率 × 2 × 秒数` 退出
5. 支持提前停止：循环里检查全局 `volatile bool s_rec_stop` 标志
6. 结束后 `fseek(f, 0, SEEK_SET)` 回填真实 WAV 头（3 个 size 字段：RIFF chunk size、data chunk size），`fclose`

### 3.2 命令

- `audio_rec -t <秒>` — 启动录音任务（任务优先级建议 10 左右，缓冲 4KB）
- `audio_stop` — 置停止标志

### 3.3 验证

```
esp32> audio_rec -t 10
# 对着麦克风说话
esp32> ls /data/wav     # 用现有 filemgr 命令确认文件存在
```

FTP 取回 WAV，播放器能播、时长 10 秒、声音清晰。再测 `audio_stop` 提前停止，文件依然能完整播放（说明头回填正确）。

### ✅ 检查点

WAV 可播放、时长正确、stop 后文件不损坏。

---

## 第 4 步：SNTP 对时 + 时间戳文件名

**目标**：文件名变成 `rec_20260923_153000.wav`。

### 4.1 新建 `proj_s31/main/common/app_sntp/`

`app_sntp_init()`：
```c
esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
esp_netif_sntp_init(&cfg);
setenv("TZ", "CST-8", 1);   // 东八区
tzset();
```
头文件 `esp_netif_sntp.h`。网络未通时 SNTP 自动重试，不用等 WiFi 事件。

### 4.2 接线

- `main/common/CMakeLists.txt` 加源文件与 include 目录
- `app_main.c`：WiFi start 之后调 `app_sntp_init()`

### 4.3 文件名生成

```c
time_t now = time(NULL);
struct tm t;
localtime_r(&now, &t);
if (t.tm_year + 1900 >= 2026) {
    strftime(name, ..., "/data/wav/rec_%Y%m%d_%H%M%S.wav", &t);
} else {
    // 未对时：回退 /data/wav/rec_uptime<ms>.wav，并打印提示
}
```

### ✅ 检查点

WiFi 连上后文件名是真实时间；不连 WiFi 用回退名且不 crash。

---

## 第 5 步：WAV 转 MP3（Shine 编码器）

**目标**：录完自动转码出 MP3。

### 5.1 vendor Shine 源码为本地组件

与 esp_codec_dev 一样放到 IDF 的 components 目录：

```bash
git clone https://github.com/toots/shine /tmp/shine
# 取 src/lib/ 下的源码（bitstream.c, huffman.c, l3bitstream.c, l3loop.c,
# l3mdct.c, l3subband.c, layer3.c, reservoir.c, tables.c + 头文件）
# 放进 $IDF_PATH/components/shine/，自己写 CMakeLists.txt：
#   idf_component_register(SRCS ... INCLUDE_DIRS "src/lib")
```

### 5.2 新建 `bsp_audioenc.c`（转码任务）

流程：
1. 打开 WAV，`fseek` 跳过 44 字节头
2. 配置 Shine：
```c
shine_config_t config;
shine_set_config_mpeg_defaults(&config.mpeg);
config.wave.channels   = 1;
config.wave.samplerate = 16000;
config.mpeg.bitr       = 48;    // Kconfig 可配
shine_t s = shine_initialise(&config);
int samples_per_pass = shine_samples_per_pass(s);
```
3. 循环：读 `samples_per_pass` 个采样 → `shine_encode_buffer(s, &pcm_ptr, &written)` → `fwrite`
4. `shine_flush()` 收尾 → `shine_close()`
5. 大块缓冲区用 `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`（板子已开 OCT PSRAM 250MHz）
6. 转码任务优先级放低（如 3），避免与 REPL 抢 CPU

### 5.3 命令

- `audio_rec` 录完自动启动转码任务
- `audio_enc -f <wav文件名>` — 手动转已有 WAV

### ✅ 检查点

MP3 能播放、时长与 WAV 一致、体积约为 WAV 的 1/5 ~ 1/10。

---

## 第 6 步：总结

在 `proj_s31/docs/` 补一篇 `audio_record.md`：架构图、I2S/ES8311 原理笔记、踩坑记录、与 FTP 联动的完整使用流程。

---

## 附：默认音频参数

| 项 | 默认 | 说明 |
|---|---|---|
| 采样率 | 16000 Hz | 语音够用，文件小（Kconfig 可调） |
| 位深 / 声道 | 16 bit / 单声道 | ES8311 ADC 数据在左声道 slot |
| MP3 码率 | 48 kbps | 语音质量足够 |
| 麦克风增益 | 30 dB | `esp_codec_dev_set_in_gain()` |
| 录音缓冲 | 4 KB（内部 RAM） | 编码大缓冲走 PSRAM |

## 附：参考资料

- 官方例程：`examples/peripherals/i2s/i2s_codec/i2s_es8311/`
- esp_codec_dev 组件：https://components.espressif.com/components/espressif/esp_codec_dev
- esp_codec_dev 所有版本及下载地址（JSON）：
  `curl -s https://components.espressif.com/api/components/espressif/esp_codec_dev`
- Shine 源码：https://github.com/toots/shine
- Shine ESP32 移植参考：https://github.com/fknrdcls/mp3_shine_esp32

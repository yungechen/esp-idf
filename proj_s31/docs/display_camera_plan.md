# ESP32-S31-Korvo-1 显示屏 + 摄像头学习计划

> 目标：在 ESP32-S31-Korvo-1 V1.1 开发板上学习 LCD 显示与摄像头开发。
> 路线：**先用官方 esp-bsp 快速点亮，再逐层深入裸驱动**；代码全部集成进 `proj_s31`，与现有 WiFi/FTP/控制台功能共生。

## 硬件事实速查表

来源：官方 user guide（docs.espressif.com esp32-s31-korvo-1）+ espressif/esp-bsp `esp32_s31_korvo_1` BSP 头文件。

| 项目 | 参数 |
|---|---|
| 屏 | 4.3" 800×480，16-bit RGB565，RGB 并行接口，ST7262E43（纯时序屏，无命令接口），背光板上常亮（GPIO_NC） |
| LCD 引脚 | PCLK=40, HSYNC=44, VSYNC=45, DE=43, DISP=38, D0-D15 = 8,9,10,11,12,13,14,15,16,17,18,19,33,34,35,36 |
| LCD 时序 | pclk 18MHz；hsync pulse 40 / bp 40 / fp 48；vsync pulse 23 / bp 32 / fp 13；pclk_active_neg=true |
| 触摸 | GT1151，I2C0（SDA=0, SCL=1），与音频 codec ES8389、摄像头 SCCB(addr 0x78) 共用总线 |
| 摄像头 | OV3660，8-bit DVP：XCLK=55, PCLK=54, VSYNC=56, HREF=57, D0-D7=46-53；XCLK 20MHz；板装旋转 180° |
| BSP 摄像头路径 | `esp_video` 组件（V4L2 API，~2.2），非旧版 esp32-camera |
| 内存 | ESP32-S31-WROOM-3：16MB Flash + 16MB Octal PSRAM（单帧 RGB565 framebuffer 768KB，须放 PSRAM） |

注意：GPIO19/20 与 USB/SD 复用（BSP 中 USB_NEG=19、SD_D0=20），调试时留意。

## 阶段 0：环境准备（离线 vendor 方案）

> 决策：**不使用 `idf_component.yml` 注册依赖**（避免构建时访问组件 registry）。
> 所有第三方组件一次性下载并 vendor 到 `proj_s31/thirdparty/` 下，之后完全离线编译。
> 根 `CMakeLists.txt` 已配置 `EXTRA_COMPONENT_DIRS = proj_s31/thirdparty`，组件放入即被自动发现（与 `ethernet_init`、`yt8531` 同机制）。

1. `source ./export.sh`（esp-idf 仓库根目录，提供带 pyyaml 的 python 环境）
2. 运行 vendor 拉取脚本（见文末附录 A：`proj_s31/tools/fetch_vendor.py`，需联网一次）：
   - 从 registry 下载各组件 zip（URL 格式：`https://components-file.espressif.com/components/<ns>/<name>/<ver>/<ns>__<name>-v<ver>.zip`）
   - 递归解析每个组件自带 `idf_component.yml` 的依赖 spec，自动选满足约束的最高版本
   - 解压到 `proj_s31/thirdparty/<name>/`
3. 依赖闭包（脚本自动解析，此处为预期清单，打 ✓ 的为本 IDF fork 已内置、无需下载）：
   - `esp32_s31_korvo_1` 1.0.1（BSP 本体，入口）
   - 显示链路：`esp_lvgl_port` ~2.9 → `lvgl/lvgl` ~9.6 → `freetype`、`libjpeg-turbo`、`libpng`（→`zlib`）、`lz4`
   - 触摸：`esp_lcd_touch_gt1151` ~1.1 → `esp_lcd_touch`
   - 摄像头链路：`esp_video` ~2.2 → `esp_cam_sensor`（→`esp_sccb_intf`）、`esp_h264`、`esp_ipa`、`usb_host_uvc`
   - BSP 杂项：`button` ~4（→`cmake_utilities`）、`led_indicator` ~2（→`cmake_utilities`、`led_strip`）、`usb`
   - ✓ `esp_codec_dev`：本 fork `components/` 已内置
4. 验证离线：`idf.py fullclean && idf.py build` 断网可过（组件管理器检测到无 manifest 变更不会触网；若仍触网可加 `IDF_COMPONENT_MANAGER=0` 构建）
5. 备选（脚本出问题时）：临时在 `idf_component.yml` 加 BSP 依赖 → `idf.py reconfigure` 让官方解析器下载到 `managed_components/` → 把 `managed_components/*` 复制进 `thirdparty/` → 还原 yml → 删除 `managed_components/`。版本一致性由官方解析器保证。

## 阶段 1：BSP 点亮屏幕（目标：当天出画面）

新建模块 `proj_s31/main/common/display/`（遵循项目拆分式 CMakeLists 惯例：把源文件加进 `main/common/CMakeLists.txt` 的 `COMMON_SRCS`）：

1. `bsp_display.c`：封装 `bsp_display_start()` 的薄初始化 + LVGL 示例 UI
   - `bsp_i2c_init()` → `bsp_display_start()` → `bsp_display_backlight_on()`
   - LVGL 操作前先 `bsp_display_lock()`（BSP 要求）
   - 最小 UI：全屏 label 显示 "Hello ESP32-S31" + 一个按钮验证触摸（GT1151 输入设备 BSP 已配好）
2. `app_main.c` 挂接；`idf.py menuconfig` 确认 BSP 的 I2C 编号等配置项
3. `idf.py -p PORT flash monitor` 验证：画面显示 + 触摸有反应
4. **阅读 BSP 源码**（managed_components 里的 `esp32_s31_korvo_1.c`）：`bsp_display_new()` 如何调 `esp_lcd_new_rgb_panel()`、`bsp_display_start()` 如何挂 esp_lvgl_port——为阶段 3 裸驱动做铺垫

## 阶段 2：BSP 摄像头视频上屏（目标：见到实时画面）

参考官方例子 `esp-bsp/examples/display_camera_video`（已确认支持本板，有 `sdkconfig.bsp.esp32_s31_korvo_1`）。新建 `proj_s31/main/common/camera/`：

1. `bsp_camera_start(NULL)` 初始化（内部 = esp_video DVP 设备 + OV3660 传感器探测）
2. 按 V4L2 流程取帧（照抄例子的 `app_video.c` 骨架）：
   `open(BSP_CAMERA_DEVICE)` → `VIDIOC_QUERYCAP` → `VIDIOC_S_FMT`（RGB565）→ `VIDIOC_REQBUFS`/`QUERYBUF`/`QBUF` → `VIDIOC_STREAMON` → 循环 `DQBUF` 取帧 / `QBUF` 归还
3. 帧数据 → LVGL `lv_image`/canvas 刷到屏上；若方向不对，用 `BSP_CAMERA_ROTATION`(180°) 或 V4L2 flip 控制
4. 验证：串口打印帧率/分辨率，屏上见到实时预览

## 阶段 3：裸 esp_lcd 驱动 RGB 屏（理解原理，替换 BSP 显示）

1. 参考 `examples/peripherals/lcd/rgb_panel/`（本 fork 明确支持 esp32s31）+ 阶段 1 读过的 BSP 源码
2. 在 `common/display/` 写 `lcd_rgb_raw.c`：`esp_lcd_rgb_panel_config_t` 填入速查表的引脚与时序，帧缓冲 `esp_lcd_rgb_panel_alloc_frame_buffer()` 分配在 PSRAM
3. 对比实验（学习要点）：
   - 单 FB vs 双 FB（`flags.fb_in_psram`、`num_fbs`）撕裂差异
   - `esp_lcd_panel_draw_bitmap()` 刷测试图（彩条/渐变）验证数据脚序（RGB vs BGR 元素序）
   - 触摸仍用 BSP 或裸 `esp_lcd_touch_gt1151`（I2C0, SDA=0/SCL=1）
4. `idf.py size` 观察 PSRAM/DRAM 占用，理解 768KB framebuffer 为何必须放 PSRAM

## 阶段 4：裸 esp_driver_cam 采集（理解原理，替换 BSP 摄像头）

1. 本地参考：`components/esp_driver_cam/test_apps/dvp/`（esp32s31 分支引脚与本板一致：SCCB=I2C0，XCLK=55，PCLK=54，VSYNC=56，DE=57，D0-7=46-53）
2. 传感器驱动 `esp_cam_sensor` 已在阶段 0 随 `esp_video` 依赖闭包 vendor 进 `thirdparty/`（含 OV3660）；**不用**旧 `esp32-camera` 组件
3. `common/camera/` 写 `cam_dvp_raw.c`：`esp_cam_new_csi_dvp_ctlr()` → 帧接收回调（PSRAM DMA 帧缓冲）→ 取帧
4. 打通：**裸采集帧 → `esp_lcd_panel_draw_bitmap()` 上屏**（摄像头与显示两章汇合）
5. 对比实验：分辨率/fps 扫描（QVGA→VGA→SVGA），YUV422 vs RGB565 输出格式

## 阶段 5：综合小项目（结合现有功能）

做一个"拍照"功能串起全链路，符合项目既有模式：

1. 控制台命令 `cam shot`（照 `main/common/cmd_nvs/` 的 argtable3 模式注册进 REPL）
2. 流程：取一帧 → `esp_driver_jpeg` 硬件编码为 JPEG → 存 `/data/shot_xxx.jpg`（复用现有 filemgr 挂载点）→ 可选：经现有 FTP server 传出
3. 加分项：LVGL 按钮 + 触摸触发拍照（BSP_CAPS_TOUCH 已具备）

## 关键文件

- 新建：`proj_s31/main/common/display/{bsp_display.c,lcd_rgb_raw.c}`、`proj_s31/main/common/camera/{cam_bsp.c,cam_dvp_raw.c}`、`proj_s31/main/common/cmd_cam/`（阶段 5）、`proj_s31/tools/fetch_vendor.py`（阶段 0 vendor 拉取脚本）
- 修改：`proj_s31/main/common/CMakeLists.txt`（COMMON_SRCS）、`proj_s31/main/app_main.c`（挂接初始化）
- 不修改 `main/idf_component.yml`（离线 vendor 方案）
- 参考：`examples/peripherals/lcd/rgb_panel/`、`components/esp_driver_cam/test_apps/dvp/`、esp-bsp 仓库 `bsp/esp32_s31_korvo_1/` 与 `examples/display_camera_video/`

## 验证方式

- 每阶段 `idf.py -p PORT flash monitor`
  - 阶段 1：看 LVGL 界面与触摸响应
  - 阶段 2/4：看串口帧率日志与屏上预览
  - 阶段 3：看彩条测试图
  - 阶段 5：在 `/data` 看到合法 JPEG（可用 `file` 或 python 验证文件头）
- 屏与摄像头均为目视验证，需实板；构建验证 `idf.py build` 无警告即过
- 可选：`examples/peripherals/lcd/rgb_panel` 自带 pytest（需夹具），本项目以人工验证为主

## 进度记录

- [ ] 阶段 0：环境准备（vendor 拉取脚本 + 离线验证）
- [ ] 阶段 1：BSP 点亮屏幕
- [ ] 阶段 2：BSP 摄像头视频上屏
- [ ] 阶段 3：裸 esp_lcd 驱动 RGB 屏
- [ ] 阶段 4：裸 esp_driver_cam 采集
- [ ] 阶段 5：综合拍照项目

## 附录 A：`proj_s31/tools/fetch_vendor.py` 设计要点

一次性联网运行的递归拉取脚本（stdlib + pyyaml，需在 `source ./export.sh` 后的环境运行）：

1. **入口**：从 `espressif/esp32_s31_korvo_1 == 1.0.1` 开始
2. **版本解析**：GET `https://components.espressif.com/api/components/<ns>/<name>` 取 `versions` 列表，按 spec（支持 `^`、`~`、`>=`、`*`、精确版本）选最高满足版本；记录到本地 lock（`proj_s31/thirdparty/vendor.lock.json`）保证可复现
3. **下载解压**：GET `versions[i].url`（zip），解压到 `proj_s31/thirdparty/<name>/`；若 zip 内为单一顶层目录则扁平化，确保 `<name>/CMakeLists.txt` 在组件根
4. **递归**：读取刚解压组件的 `idf_component.yml`，对其 `dependencies` 递归（跳过已在 ESP-IDF 内置的组件：检查 `$IDF_PATH/components/<name>` 存在则跳过，如 `esp_codec_dev`；跳过 `idf` 版本约束项）
5. **去重**：按组件名记忆化，冲突版本取先锁定者并打印警告
6. **断网验证**：完成后 `rm -rf managed_components build && idf.py reconfigure` 应全程无网络访问

预期 vendor 结果约 20 个组件目录（见阶段 0 清单）。

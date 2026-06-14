# m_proj Kconfig 配置指南

## 1. 目标

将项目所有的应用级 Kconfig 配置项统一放置在顶层菜单 **"My Application Settings"** 下，和 ESP-IDF 的 **"Component config"** 菜单平级。

同时，不同类型的配置项可以分散到不同路径的 Kconfig 文件中管理，例如：

- `main/app.kconfig`：主应用相关配置
- `bsp/bsp.kconfig`：板级支持包（BSP）相关配置

## 2. 目录结构

```
m_proj/
├── CMakeLists.txt
├── main/
│   ├── CMakeLists.txt
│   ├── app_main.c
│   ├── Kconfig.projbuild          # 唯一顶层入口，定义 "My Application Settings" 菜单
│   └── app.kconfig                # main 相关配置，只放 config，不写 menu
├── bsp/
│   ├── CMakeLists.txt             # bsp 注册为 component，参与编译
│   ├── app_led.c
│   ├── app_led.h
│   └── bsp.kconfig                # bsp 相关配置，只放 config，不写 menu
```

## 3. 关键原理

### 3.1 Kconfig 与 Kconfig.projbuild 的区别

| 文件名               | 收集位置                         | 菜单层级                              |
|----------------------|----------------------------------|----------------------------------------|
| `Kconfig`            | `Component config` 子菜单内      | 嵌套在 "Component config" 下面         |
| `Kconfig.projbuild`  | 根 Kconfig 的顶层 source 处      | 与 "Component config" 平级             |

因此，要让 "My Application Settings" 出现在顶层，必须将入口文件命名为 `Kconfig.projbuild`。

### 3.2 子目录 Kconfig 文件的加载

ESP-IDF 的构建系统只会自动扫描 **component 根目录** 下的 `Kconfig` 或 `Kconfig.projbuild`，不会递归扫描子目录。

因此，其他位置的 Kconfig 文件需要由根目录的入口文件通过 `source` 指令显式引入。

### 3.3 为什么 bsp 的配置不能放在 `bsp/Kconfig.projbuild`

如果 `bsp/` 根目录存在 `Kconfig.projbuild`，ESP-IDF 会自动把它收集为**独立的顶层菜单**，和 `My Application Settings` 平级。这样 bsp 的配置就不会出现在你的统一菜单里。

所以为了让 bsp 的配置也收进 `My Application Settings`：

- 不在 `bsp/` 根目录放 `Kconfig.projbuild`
- 把 bsp 的配置放在其他位置，例如 `bsp/bsp.kconfig`
- 由 `main/Kconfig.projbuild` 统一 source 进来

### 3.4 source 路径的解析基准

`source` 指令中的相对路径是相对于 **kconfgen 的运行目录** 解析的。默认情况下，`idf.py menuconfig` 在 `m_proj/build/` 目录下运行 kconfgen。

因此，从 `build/` 目录出发：

- `../main/app.kconfig` 指向 `m_proj/main/app.kconfig`
- `../bsp/bsp.kconfig` 指向 `m_proj/bsp/bsp.kconfig`

> 注意：如果你使用 `-B` 选项将 build 目录指定到项目外部（例如 `idf.py -B /tmp/build menuconfig`），上述相对路径会失效。常规使用无需担心此问题。

## 4. 配置步骤

### 4.1 注册 bsp 为 component

创建 `m_proj/bsp/CMakeLists.txt`：

```cmake
idf_component_register(SRCS "app_led.c"
                       INCLUDE_DIRS ".")
```

### 4.2 让 main 依赖 bsp

修改 `m_proj/main/CMakeLists.txt`：

```cmake
idf_component_register(SRCS "app_main.c"
                       REQUIRES spi_flash bsp
                       INCLUDE_DIRS "")
```

### 4.3 将 bsp 加入组件搜索路径

修改 `m_proj/CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

idf_build_set_property(MINIMAL_BUILD ON)

# 添加 bsp 到组件搜索路径
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/bsp")

project(hello_world)
```

### 4.4 创建顶层 Kconfig 入口

创建 `m_proj/main/Kconfig.projbuild`：

```kconfig
menu "My Application Settings"
    source "../main/app.kconfig"
    source "../bsp/bsp.kconfig"
endmenu
```

### 4.5 创建 main 的配置文件

创建 `m_proj/main/app.kconfig`：

```kconfig
config APP_LOG_ENABLE_DEBUG_LOG
    bool "Enable debug log output"
    default n
    help
        Print extra debug information at runtime
```

### 4.6 创建 bsp 的配置文件

创建 `m_proj/bsp/bsp.kconfig`：

```kconfig
config BSP_LED_GPIO_NUM
    int "LED GPIO number"
    default 8
    help
        GPIO number connected to the LED
```

### 4.7 清理旧的 Kconfig 文件

如果 `m_proj/Kconfig`（项目根目录下）仍然存在，请删除它。ESP-IDF 不会自动收集项目根目录的 Kconfig，保留它只会造成混淆。

```bash
rm m_proj/Kconfig
```

## 5. 验证

1. 清理构建缓存：

   ```bash
   idf.py fullclean
   ```

2. 打开 menuconfig：

   ```bash
   idf.py menuconfig
   ```

3. 在顶层菜单中找到 **"My Application Settings"**，展开后应能看到：

   - `Enable debug log output`
   - `LED GPIO number`

## 6. 扩展更多配置

如果需要新增一类配置（例如 `network.kconfig`），只需：

1. 在 `main/` 下创建新文件，例如 `network.kconfig`。
2. 在 `main/Kconfig.projbuild` 中添加一行：

   ```kconfig
   source "../main/network.kconfig"
   ```

3. 重新运行 `idf.py menuconfig`。

## 7. 常见问题

### 7.1 报错：`xxx/Kconfig' not found`

原因通常是修改了 Kconfig 文件位置或名称后，构建缓存未更新。

解决：

```bash
idf.py fullclean
idf.py menuconfig
```

### 7.2 配置项没有出现在 "My Application Settings" 下

检查子文件是否意外写了自己的 `menu`/`endmenu`。被 `source` 的子文件应只包含 `config` 定义，外层菜单由 `main/Kconfig.projbuild` 统一管理。

### 7.3 bsp 的源码没有被编译

检查：

- `bsp/CMakeLists.txt` 是否存在
- `main/CMakeLists.txt` 中是否通过 `REQUIRES bsp` 声明了依赖
- `m_proj/CMakeLists.txt` 中是否通过 `EXTRA_COMPONENT_DIRS` 将 `bsp` 加入了组件搜索路径

### 7.4 bsp 的配置出现在独立顶层菜单

检查 `bsp/` 根目录是否误放了 `Kconfig.projbuild`。如果是，请删除它，并将配置项放到 `bsp/xxx.kconfig`，由 `main/Kconfig.projbuild` 统一 source。

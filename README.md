# Memoria OS

**Memoria OS** 是一款基于 ESP32-S3（N16R8：16 MB Flash + 8 MB Octal PSRAM）自主设计的掌机操作系统。系统内核、窗口系统、文件系统、脚本引擎、BASIC 解释器与包管理均为从零自研，不依赖 Linux、LVGL 或任何现成 UI 框架。项目以开源复古计算与嵌入式系统学习为定位，面向希望理解完整嵌入式系统栈的开发者。

## 项目定位

Memoria OS 在单颗 ESP32-S3 上实现了一台完整的手持计算设备：开机进入图标桌面，通过功能键与摇杆在十一种内置模式间切换；用户可在设备内直接编写 BASIC 程序或 `.ms` 脚本并立即运行；同时支持音频、视频与图片播放、应用商店下载与 OTA 固件升级。

系统不使用外部操作系统，所有任务调度建立在 FreeRTOS 原语之上，所有界面绘制由自研窗口栈完成。

## 应用开发（软件的本质）

Memoria OS 上的"软件"本质是 **BASIC 源码**。`.msp` 安装包只是信封——50 字节头（魔数、包名、版本、CRC32）加上 BASIC 源码正文；应用商店分发的是包，设备上解释执行的是源码：

```
BASIC 源码（.bas 文本）
  → 打包为 .msp 安装包
  → 上架 manifest.json（软件源清单）
  → STORE 商店安装到 /mem_fat/scripts/
  → PRGM 模式解释运行
```

完整语法、屏幕绘图与硬件扩展 API（WiFi / GPIO / 串口 / 蓝牙）见 **`docs/BASIC-LANGUAGE.md`**（Memoria BASIC 语言手册）。

## 功能特性

- **内置模式**：PHOTO、MUSIC、VIDEO、CLOCK、NOTES、CALC、PRGM、STORE、SETUP、FMEM、SYS 共十一种，通过 F1~F6 功能键与摇杆导航切换。
- **键盘输入**：6×10 键矩阵，由两片 MCP23017（I2C 地址 0x20 / 0x21）扫描，支持 Shift 双层键位与拼音输入法。
- **现场编程**：内置文本编辑器、BASIC 解释器与 `.ms` 脚本引擎，编写完成后可直接运行。
- **多媒体**：支持 WAV、MP3、M4A（AAC）解码播放，MJPG 视频解码，以及 GB2312 中文点阵渲染。
- **应用商店与固件升级**：应用包具备 CRC 校验、断点续传与多源切换能力；固件支持 HTTPS OTA 升级。
- **桌面管理**：支持图标移动、文件夹嵌套，布局通过 NVS 持久化。
- **无线连接**：WiFi STA 联网，BLE 串口透传，可远程下发 Shell 命令。

## 系统架构

系统由八个 ESP-IDF 组件与一个主程序入口构成：

| 组件 | 职责 | 关键文件 |
|------|------|----------|
| `memoria_kernel` | 启动序列、模式管理、键盘扫描、拼音输入法、桌面与输入队列 | `kernel.cpp`、`mode_manager.cpp`、`ime.cpp`、`modes/` |
| `memoria_window` | 窗口栈、状态栏、GB2312 点阵字体渲染 | `window.cpp`、`gb2312_font.cpp` |
| `memoria_drivers` | 屏幕、TF 卡、音频、摇杆、电池采样、WiFi、BLE 等硬件驱动 | `ili9341.cpp`、`i2s_audio.cpp`、`sdcard.cpp`、`wifi_manager.cpp`、`include/drivers_config.hpp` |
| `memoria_fs` | 私有文件系统与 FAT 分区同步扫描 | `private_fs.cpp`、`fat_scanner.cpp` |
| `memoria_script` | `.ms` 脚本引擎（词法分析、语法分析、求值） | `src/tokenizer.cpp`、`src/parser.cpp`、`src/evaluator.cpp` |
| `memoria_basic` | BASIC 解释器 | `src/basic_interpreter.cpp` |
| `memoria_package` | 应用商店、包格式、OTA 与更新调度 | `package_manager.cpp`、`ota_updater.cpp` |
| `memoria_shell` | 交互式 Shell（REPL） | `shell.cpp` |

主程序入口为 `main/main.cpp`，负责按固定顺序启动上述组件。

## 构建指南

**环境要求**：ESP-IDF v6.1。

```powershell
# 编译
idf.py build

# 烧录并打开串口监视器（COMx 为实际串口号）
idf.py -p COMx flash monitor
```

构建基线保持全绿，编译选项启用 `-Werror`，当前零警告。固件二进制大小为 `0x1E0560`（1,967,456 字节，约 1.9 MB）。固件自研源码共 100 个文件（50 个 .cpp 实现文件，其余为 .h/.hpp 头文件；不含 `third_party/` 下的第三方解码库）。

**v1.1.0 新增**：BASIC v1.2 语句集（字符串变量、httpget/httpup/httpdl、record/play、wifi 托管 wifistat）、拼音输入法（F1~F4 选字 / Ctrl+Shift 中英切换 / ime_cn 环境变量）、系统设置网络管理页（WiFi 扫描连接 / 热点 / 蓝牙）、$ 字符串原生中文（UTF-8 存储，len 按字数）。掌机应用见 `sdcard/scripts/chat.bas`（chat 聊天 App，含语音收发），拼音码表见 `sdcard/ime/pinyin.bin`。

**分区表**（`partitions.csv`）：

| 分区 | 用途 |
|------|------|
| `nvs` | NVS 非易失存储（配置、桌面布局） |
| `phy_init` | WiFi 射频校准数据 |
| `factory` | 出厂固件分区 |
| `ota_0` | OTA 升级固件分区 |
| `mem_fat` | FAT 数据分区（媒体、字库、脚本） |
| `mem_priv` | SPIFFS 私有数据分区 |

**编译配置**（`sdkconfig.defaults`）：纯 C++ 工程，关闭异常与 RTTI；Flash 配置为 16 MB、QIO、80 MHz；启用 8 MB Octal PSRAM（80 MHz）。

**TF 卡准备**：将 `sdcard/` 目录下的全部内容复制到 TF 卡根目录（FAT32）。其中 `/font/gb2312_16.bin` 为 GB2312 点阵字库，系统启动时读入 PSRAM。未放置该字库时，中文将以方框显示。

## 硬件要求

| 模块 | 规格 | 说明 |
|------|------|------|
| 核心板 | ESP32-S3 N16R8（S3-Zero 类） | 16 MB Flash + 8 MB Octal PSRAM |
| 屏幕 | ILI9488 SPI，480×320 RGB565 | 默认面板，无触摸；可在 `drivers_config.hpp` 切换为 ILI9341 320×240 |
| 键盘 | 6×10 矩阵轻触键 | 两片 MCP23017 I2C 扫描，上下各一条 WS2812 灯带 |
| 摇杆 | KY-023 | X/Y 双 ADC + 按下按键 |
| 音频 | MAX98357A + 3 W 喇叭 | I2S 输出；INMP441 数字麦克风预留 |
| 存储 | TF 卡（FAT32） | 字库、音频、图片与脚本 |
| 电源 | 3.7 V 锂电 + 保护板 | TP4056 充电，ADC 分压采样电池电压 |

## 引脚分配

引脚定义的唯一数据来源为 `components/memoria_drivers/include/drivers_config.hpp`，修改硬件接线时应以该文件为准，不在其他位置硬编码 GPIO 编号。

| 功能 | GPIO |
|------|------|
| SPI SCLK / MOSI / MISO | 12 / 11 / 13 |
| LCD CS / DC / RST / BLK | 10 / 9 / 8 / 14 |
| 摇杆 X / Y / BTN | 7 / 5 / 38 |
| 电池电压（ADC1_CH3） | 4 |
| TF 卡 CS | 17 |
| I2S BCLK / WS / DOUT | 18 / 21 / 15 |
| 键盘 I2C SDA / SCL（0x20 / 0x21） | 1 / 6 |
| 键盘灯带 WS2812 | 46 |
| UART0 烧录 / 控制台 | 43 / 44 |

GPIO33~37 被 Octal PSRAM 内部占用，不可使用；ADC2 与 WiFi 冲突，已禁用。

## 目录结构

```
memoria-os/
├── README.md
├── LICENSE
├── .gitignore
├── CMakeLists.txt
├── partitions.csv
├── sdkconfig.defaults
├── components/                 固件组件（ESP-IDF 组件，含 third_party 解码库）
│   ├── memoria_kernel/
│   ├── memoria_window/
│   ├── memoria_drivers/
│   ├── memoria_fs/
│   ├── memoria_script/
│   ├── memoria_basic/
│   ├── memoria_package/
│   └── memoria_shell/
├── main/                       程序入口（app_main）
├── docs/
│   ├── CONTRIBUTING.md          开发约定与代码规范
│   ├── HARDWARE-BOM.md          硬件采购清单
│   ├── HARDWARE-DIMENSIONS.md   外壳与器件尺寸标准
│   └── RESOURCES.md             项目资源索引
├── examples/
│   └── demo.ms                  .ms 脚本示例
├── sample/                      测试媒体（mp3 / mjpg）与素材抓取脚本
├── sdcard/
│   └── font/gb2312_16.bin      GB2312 点阵字库（TF 卡根目录内容）
├── design/                      硬件工程图纸、外壳尺寸规划与 3D 模型
├── simulator/
│   └── simulator.html            单文件浏览器模拟器
└── tools/                       开发辅助脚本（字库生成、视频转码）
    ├── font_to_bin.py
    ├── gen_gb2312_font.py
    └── video2mjpg.ps1
```

## 示例与资源

- **`.ms` 脚本示例**：`examples/demo.ms`。将其复制到 TF 卡的 `/mem_fat/scripts/` 目录后，在 PRGM 模式中选中运行。
- **测试媒体**：`sample/` 目录提供 mp3 与 mjpg 测试文件，`sample/fetch_demo_m4a.py` 用于获取 AAC 测试素材。
- **浏览器模拟器**：`simulator/simulator.html` 为单文件页面，无需烧录即可在浏览器中体验主要交互流程。
- **硬件设计**：`design/` 目录包含主板、前面板、总装与外壳的工程图纸及外壳尺寸规划页，可直接用浏览器打开。

## 许可证

本项目以 MIT 许可证发布，详见根目录 `LICENSE`。

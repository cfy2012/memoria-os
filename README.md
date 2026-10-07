# Memoria OS

**Memoria OS v1.3.0** 是一款基于 ESP32-S3（N16R8：16 MB Flash + 8 MB Octal PSRAM）自主设计的掌机操作系统。系统内核、窗口系统、文件系统、脚本引擎、BASIC 解释器与应用商店均为从零自研，不依赖 Linux、LVGL 或任何现成 UI 框架。项目以开源复古计算与嵌入式系统学习为定位，面向希望理解完整嵌入式系统栈的开发者。

## 项目定位

Memoria OS 在单颗 ESP32-S3 上实现了一台完整的手持计算设备：开机进入图标桌面，通过功能键与摇杆在十一种内置模式间切换；用户可在设备内直接编写 BASIC 程序或 `.ms` 脚本并立即运行；同时支持音频/视频/图片播放、应用商店下载安装、语音录制收发与 OTA 固件升级。

系统不使用外部操作系统，所有任务调度建立在 FreeRTOS 原语之上，所有界面绘制由自研窗口栈完成。

## 应用开发（软件的本质）

Memoria OS 上的"软件"统一为 **`.msp` 应用包**——50 字节头（魔数 MSPACK、包名、版本、CRC32）加 payload。商店只认 `.msp`，不允许裸 `.bas` / `.c` 文件直下。v1.3.0 起应用包支持 **MBND 解包安装**：包内可携带 `app.bas`（BASIC 源码）、`manifest.json`（元信息）与任意资源文件，商店安装时自动展开落盘、卸载时连带清理。

```
BASIC 源码（.bas 文本）
  → PC 端应用打包工作台（tools/msp-packer）生成 .msp
  → 上架 manifest.json（软件源清单）
  → STORE 商店安装（.msp 单文件包直接运行 / MBND 包解包落盘）
  → PRGM 模式解释运行
```

完整语法、屏幕绘图、GUI 控件与硬件扩展 API（WiFi / GPIO / 串口 / 蓝牙）见 **`docs/BASIC-LANGUAGE.md`**（Memoria BASIC 语言手册）与 **`docs/BASIC-BOOK.md`**（语言教材）。

## 功能特性

- **内置模式**：PHOTO、MUSIC、VIDEO、CLOCK、NOTES、CALC、PRGM、STORE、SETUP、FMEM、SYS 共十一种，通过 F1~F6 功能键与摇杆导航切换；桌面图标网格分页展示，最后一行下滑自动翻页。
- **键盘输入**：6×10 键矩阵，由两片 MCP23017（I2C 地址 0x20 / 0x21）扫描，支持 Shift 双层键位与拼音输入法（F1~F4 选字 / Ctrl+Shift 中英切换）。
- **GUI 控件语句（v1.3.0 新增）**：`bkey`（按钮按下）、`lbl`/`llbl`/`lbld`（标签创建/改文本/删除）、`img`（内置 16×16 图标表）、`after`（非阻塞定时跳转）、text/lbl 样式参数（正常/反白/粗体），程序内直接搭界面。
- **现场编程**：内置文本编辑器、BASIC 解释器与 `.ms` 脚本引擎，编写完成后可直接运行；`dim` 数组、文件流读写、块 if、def fn、struct/class 扩展层设计见 `docs/BASIC-OOP.md`。
- **多媒体**：WAV、MP3、M4A（AAC）解码播放，MJPG 视频解码，GB2312 中文点阵渲染；INMP441 麦克风录音 + `httpup` 语音上传、`httpdl` 流式下载（chat.bas 语音聊天已落地）。
- **解方程（v1.2.1）**：`solve` 命令支持一元一次/二次、二元/三元方程组，CALC 键盘扩展 x/y/^/,。
- **应用商店**：仅认 `.msp` 应用包，CRC 校验、断点续传、多源切换；MBND 应用包解包安装（v1.3.0）。
- **固件 OTA**：`version.json` + bin 双文件结构，经 jsdelivr CDN 分发，设备端 `ota_url` / `ota_firmware` 命令即可在线升级。
- **桌面管理**：图标移动、文件夹嵌套，布局通过 NVS 持久化。
- **无线连接**：WiFi STA 联网、BLE 串口透传；射频按需启动（boot 期零射频，系统设置或 `wifistat()` 触发），程序禁止自行联网，网络凭据由系统环境变量注入。
- **系统监控**：`sys` 命令一次看全 CPU 主频/温度/双核负载/内存/射频/电池；配套 PC 端 `talk_gui` 仪表盘监视器。

## 系统架构

系统由九个 ESP-IDF 组件与一个主程序入口构成：

| 组件 | 职责 | 关键文件 |
|------|------|----------|
| `memoria_kernel` | 启动序列、模式管理、键盘扫描、拼音输入法、桌面与输入队列 | `kernel.cpp`、`mode_manager.cpp`、`ime.cpp`、`modes/` |
| `memoria_window` | 窗口栈、状态栏、GB2312 点阵字体渲染 | `window.cpp`、`gb2312_font.cpp` |
| `memoria_drivers` | 屏幕、TF 卡、音频、摇杆、电池采样、WiFi、BLE 等硬件驱动 | `ili9341.cpp`、`i2s_audio.cpp`、`sdcard.cpp`、`wifi_manager.cpp`、`include/drivers_config.hpp` |
| `memoria_fs` | 私有文件系统与 FAT 分区同步扫描 | `private_fs.cpp`、`fat_scanner.cpp` |
| `memoria_script` | `.ms` 脚本引擎（词法分析、语法分析、求值） | `src/tokenizer.cpp`、`src/parser.cpp`、`src/evaluator.cpp` |
| `memoria_basic` | BASIC 解释器 | `src/basic_interpreter.cpp` |
| `memoria_math` | 解方程求解器（一元一次/二次、方程组） | `math_solver.cpp` |
| `memoria_package` | 应用商店、`.msp`/MBND 包格式、OTA 与更新调度 | `package_manager.cpp`、`ota_updater.cpp` |
| `memoria_shell` | 交互式 Shell（REPL），全部命令真实实现 | `shell.cpp` |

主程序入口为 `main/main.cpp`，负责按固定顺序启动上述组件。自研源码共 108 个文件（53 个 .cpp 实现文件、45 个 .h/.hpp 头文件、10 个组件 CMakeLists；不含 `third_party/` 下的第三方解码库）。

## 构建指南

**环境要求**：ESP-IDF v6.1。

```powershell
# 编译
idf.py build

# 烧录并打开串口监视器（COMx 为实际串口号）
idf.py -p COMx flash monitor
```

构建基线保持全绿，编译选项启用 `-Werror`，当前零警告。v1.3.0 固件二进制约 1.84 MB（app 分区 40% 空闲）。

**v1.3.0 新增**：GUI 控件语句集（bkey / lbl / llbl / lbld / img / after / 样式参数）、store MBND 应用包解包安装、商店 .msp-only 政策、shell 错误路径误退出 panic 修复（#86）。

**v1.2.x 沿革**：BASIC v1.2 语句集（字符串变量、httpget/httpup/httpdl、record/play、wifi 托管 wifistat）、拼音输入法、系统设置网络管理页、memoria_math 解方程、RND 硬件真随机（esp_random，PC 模拟器逐位对齐）、I2C 键盘总线 panic 修复、CPU 240MHz。

**分区表**（`partitions.csv`）：

| 分区 | 用途 |
|------|------|
| `nvs` | NVS 非易失存储（配置、桌面布局） |
| `phy_init` | WiFi 射频校准数据 |
| `factory` | 出厂固件分区 |
| `ota_0` | OTA 升级固件分区 |
| `mem_fat` | FAT 数据分区（媒体、字库、脚本） |
| `mem_priv` | SPIFFS 私有数据分区 |

**编译配置**（`sdkconfig.defaults`）：纯 C++ 工程，关闭异常与 RTTI；Flash 配置为 16 MB、QIO、80 MHz；启用 8 MB Octal PSRAM（80 MHz）；BOD 欠压保护全程开启。

**TF 卡准备**：将 `sdcard/` 目录下的全部内容复制到 TF 卡根目录（FAT32）。其中 `/font/gb2312_16.bin` 为 GB2312 点阵字库（启动时读入 PSRAM，缺失时中文显示方框）、`/ime/pinyin.bin` 为拼音码表、`/scripts/` 内置 chat.bas（聊天+语音收发）等示例应用。

## OTA 固件升级

固件分发物随本仓库 `firmware/` 目录发布，经 jsdelivr CDN 可达：

- 固件 bin：`firmware/memoria_os_<版本>.bin`
- 版本清单：`firmware/version.json`（含 `version`、`url`、`changelog`、`major` 字段）

设备端升级路径：shell 执行 `ota_url <清单地址>` 设置源后 `ota_firmware` 一键升级；本地/远端版本比对取自 app_desc 版本串（`PROJECT_VER` 显式固定，不受编译时间戳污染）。

## 硬件要求

| 模块 | 规格 | 说明 |
|------|------|------|
| 核心板 | ESP32-S3 N16R8（S3-Zero 类） | 16 MB Flash + 8 MB Octal PSRAM |
| 屏幕 | ILI9488 SPI，480×320 RGB565 | 默认面板，无触摸；可在 `drivers_config.hpp` 切换为 ILI9341 320×240 |
| 键盘 | 6×10 矩阵轻触键 | 两片 MCP23017 I2C 扫描，上下各一条 WS2812 灯带 |
| 摇杆 | KY-023 | X/Y 双 ADC + 按下按键 |
| 音频 | MAX98357A + 3 W 喇叭 | I2S 输出；INMP441 数字麦克风录音 |
| 存储 | TF 卡（FAT32） | 字库、码表、音频、图片与脚本 |
| 电源 | 3.7 V 锂电 + 保护板 | TP4056 充电，ADC 分压采样电池电压 |

## 引脚分配

引脚定义的唯一数据来源为 `components/memoria_drivers/include/drivers_config.hpp`，修改硬件接线时应以该文件为准，不在其他位置硬编码 GPIO 编号。接线施工以根目录 **`WIRING.md`** 为准。

| 功能 | GPIO |
|------|------|
| SPI SCLK / MOSI / MISO | 12 / 11 / 13 |
| LCD CS / DC / RST / BLK | 10 / 9 / 8 / 14 |
| 摇杆 X / Y / BTN | 7 / 5 / 38 |
| 电池电压（ADC1_CH3） | 4 |
| TF 卡 CS | 17 |
| I2S BCLK / WS / DOUT | 18 / 21 / 15 |
| 麦克风 SD（INMP441，复用 I2S 时钟） | 16 |
| 键盘 I2C SDA / SCL（0x20 / 0x21） | 1 / 6 |
| 键盘灯带 WS2812 | 46 |
| UART0 烧录 / 控制台 | 43 / 44 |

GPIO33~37 被 Octal PSRAM 内部占用，不可使用；ADC2 与 WiFi 冲突，已禁用。

## PC 工具链

| 工具 | 说明 |
|------|------|
| `tools/msp-packer/` | 应用打包工作台（tkinter GUI）：选文件夹、填名称、选图标，一键生成 `.msp`；源码 + GitHub Release exe 双形态 |
| `tools/talk_gui.py` | 串口监视终端：六仪表实时监控（主频/温度/双核负载/内存/PSRAM/电池）+ 命令交互，语法高亮 |
| `tools/burnin_logger.py` | 42/72 小时挂测脚本：日志落盘、panic/电压异常标红、断线重连、总结报告（配套 `burnin.bat`） |
| `simulator/chat_proxy.py` | PC 模拟器聊天代理（winsock 真 HTTP） |
| `simulator/simulator.html` | 单文件浏览器模拟器，无需烧录体验主要交互 |

## 目录结构

```
memoria-os/
├── README.md
├── LICENSE
├── .gitignore
├── CMakeLists.txt
├── partitions.csv
├── sdkconfig.defaults
├── WIRING.md                   硬件接线账本（引脚变更唯一同步点）
├── components/                 固件组件（9 个，含 third_party 解码库）
│   ├── memoria_kernel/  memoria_window/  memoria_drivers/
│   ├── memoria_fs/      memoria_script/  memoria_basic/
│   ├── memoria_math/    memoria_package/ memoria_shell/
├── main/                       程序入口（app_main）
├── firmware/                   OTA 分发物（version.json + 各版本 bin）
├── docs/                       文档（见下节索引）
├── examples/                   .ms 脚本示例（demo.ms）
├── sdcard/                     TF 卡根目录内容（font/ ime/ scripts/）
├── simulator/                  浏览器模拟器 + PC 端 BASIC 解释器 + 聊天代理
├── design/                     硬件工程图纸、外壳尺寸规划与 3D 模型
├── changliao/                  chat 聊天服务端（PHP + JSON 存储，PWA 支持）
├── archive/                    历史版本源码（BASIC.cpp 单文件时代）
├── sample/                     测试媒体（mp3 / mjpg）与素材抓取脚本
└── tools/                      开发辅助工具（打包器、监视终端、挂测、字库/码表生成）
```

## 文档索引

| 文档 | 内容 |
|------|------|
| `docs/BASIC-LANGUAGE.md` | BASIC 语言手册：全语句语法、屏幕绘图、硬件扩展 API |
| `docs/BASIC-BOOK.md` | BASIC 语言教材（配套 docx 版随仓） |
| `docs/BASIC-OOP.md` | struct/class/容器扩展层设计 |
| `docs/MSP-DEPLOY.md` | 应用打包三步部署指南 |
| `docs/PROJECT-STATUS.md` | 项目当前状态快照（接手先读） |
| `docs/CONTRIBUTING.md` | 开发约定与代码规范 |
| `docs/WIRING.md` | 硬件接线账本 |
| `docs/HARDWARE-BOM.md` / `HARDWARE-DIMENSIONS.md` | 硬件采购清单 / 外壳与器件尺寸标准 |
| `docs/IME-SPEC.md` | 拼音输入法规格 |
| `docs/CHAT-PROTOCOL.md` | chat 聊天服务端协议 |
| `docs/CODE-AUDIT.md` / `docs/REGRESSION-CHECKLIST.md` | 源码审查记录 / 回归自检清单 |

## 示例与资源

- **BASIC 应用**：`sdcard/scripts/` 内置 chat.bas（文字+语音聊天）、solve.bas（解方程演示）、test_lang.bas（语句自检），拷卡后在 PRGM 模式运行。
- **`.ms` 脚本示例**：`examples/demo.ms`，复制到 TF 卡 `/mem_fat/scripts/` 后运行。
- **测试媒体**：`sample/` 提供 mp3 与 mjpg 测试文件。
- **硬件设计**：`design/` 包含主板、前面板、总装与外壳工程图纸（浏览器可直接打开）。

## 许可证

本项目以 MIT 许可证发布，详见根目录 `LICENSE`。

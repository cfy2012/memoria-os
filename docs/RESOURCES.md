# Memoria OS · 项目资源索引

> 本表登记仓库内全部资源（文档 / 源码 / 工具 / 素材）的路径与用途。
> 相对路径均以仓库根目录为基准。

## 一、文档

| 资源 | 路径 | 用途 |
|------|------|------|
| 项目说明 | `README.md` | 项目定位、构建与硬件概述 |
| 贡献规范 | `docs/CONTRIBUTING.md` | 代码规范与组件架构约定 |
| 许可 | `LICENSE` | MIT |

## 二、硬件文档

| 资源 | 路径 | 用途 |
|------|------|------|
| 采购清单 | `docs/HARDWARE-BOM.md` | 器件清单与参考价格 |
| 尺寸标准 | `docs/HARDWARE-DIMENSIONS.md` | 核心板定版、叠层核算与开孔表 |
| 外壳尺寸规划页 | `design/shell-design/Memoria-OS外壳尺寸规划.html` | 单页面板，浏览器直接打开 |
| 键盘按键布局表 | `design/键盘按键布局表.xlsx` | 6×10 键位布局 |
| 3D 模型 | `design/3D模型/外壳.123dx` | 外壳三维源文件 |

## 三、固件源码（components/ 与 main/）

| 组件 | 路径 | 职责 |
|------|------|------|
| 内核 | `components/memoria_kernel/` | 启动序列、11 模式、桌面、输入队列、拼音输入法、PRGM 模式 |
| 窗口 | `components/memoria_window/` | 窗口栈、状态栏、字体渲染（PSRAM 版字库加载） |
| 驱动 | `components/memoria_drivers/` | SPI / 屏幕 / TF 卡 / 电源 / I2S / 摇杆 / ADC / RTC / WiFi / BLE；引脚唯一源 `include/drivers_config.hpp` |
| 文件系统 | `components/memoria_fs/` | 私有文件系统 + FAT 同步扫描 |
| 包管理 | `components/memoria_package/` | 应用包 / OTA / 更新调度 |
| 脚本引擎 | `components/memoria_script/` | `.ms` 脚本引擎（tokenizer / parser / evaluator） |
| BASIC | `components/memoria_basic/` | BASIC 解释器 |
| Shell | `components/memoria_shell/` | REPL，BLE 可远程下发 |
| 主程序 | `main/` | `main.cpp` → 内核启动 |

## 四、解码与媒体

| 资源 | 路径 | 用途 |
|------|------|------|
| AAC 解码库 | `components/memoria_drivers/third_party/helix_aac/` | libhelix-aac（纯 C），由 `i2s_audio.cpp` 调用 |
| 解码器 | `components/memoria_drivers/src/i2s_audio.cpp` | WAV / MP3 / M4A(AAC) 解码与播放 |
| MP4 解复用 | `components/memoria_drivers/src/mp4_demux.cpp` | M4A / MP4 容器解复用 |
| 测试素材 | `sample/` | `sample_test.mp3`、`sample_*.mjpg`、`fetch_demo_m4a.py` |
| TF 卡字库镜像 | `sdcard/font/gb2312_16.bin` | GB2312 点阵字库，烧录后整目录拷至 TF 卡根目录 |

## 五、工具脚本（tools/）

| 类别 | 文件 | 用途 |
|------|------|------|
| 字库 | `tools/gen_gb2312_font.py` / `tools/font_to_bin.py` | 生成 GB2312 点阵并导出 bin |
| 视频 | `tools/video2mjpg.ps1` | 视频转 MJPG |

## 六、示例与模拟器

| 资源 | 路径 | 用途 |
|------|------|------|
| `.ms` 示例脚本 | `examples/demo.ms` | 拷至 TF 卡 `/mem_fat/scripts/` 后在 PRGM 模式运行 |
| 浏览器模拟器 | `simulator/simulator.html` | 单文件，浏览器直接打开 |

## 七、构建配置

| 资源 | 用途 |
|------|------|
| `partitions.csv` | 分区表（nvs / phy_init / factory / ota_0 / mem_fat / mem_priv） |
| `sdkconfig.defaults` | 默认配置：Octal PSRAM 80 MHz / NimBLE only / 关闭 RTTI |
| `CMakeLists.txt` | 顶层构建入口 |

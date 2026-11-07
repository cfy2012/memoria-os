# Memoria OS 重建核对笔记

> 2026-11-07 · I 盘物理损坏后，记录哪些东西有据可查、哪些只剩记录、哪些彻底没了。
> 只写能核对的事实与缺口，不写推测。

## 一、证据分级

| 级别 | 含义 | 本次覆盖 |
|------|------|----------|
| A 仓内可核 | 能在 `G:\Memoria OS` 的 git 对象里逐字节验证 | 1.4.0 `f2413394` 及其之前全部提交；v1.6.0 重建提交 |
| B 记录在案 | 有工具调用记录，能确认「改了哪个文件、改成什么值」，但被改的旧文件本身已不存在 | 2026-10-11、11-07 两次会话 |
| C 完全缺失 | 没有任何源码或 diff 可依 | 1.5.0 整版；仓库外的图纸与脚本 |

## 二、已逐项复核（2026-11-07）

| 项 | 内容 | 核实 |
|----|------|------|
| CHRG 引脚 | `drivers_config.hpp` 中 `CHRG_DET_GPIO = 42` | ✓ |
| SDMMC 三线 | `SDMMC_CLK 40 / CMD 39 / D0 41`，1-bit | ✓ |
| WIRING.md 引脚表 | 已含 39 / 40 / 41 / 42 四行 | ✓ |
| power.cpp 渐暗循环 | 187、188 行已改用 `LEDC_CHN_BACKLIGHT` 常量 | ✓ |
| 版本串 | `CMakeLists.txt`、`system_info.h`、`firmware/version.json` 三处同为 1.6.0 | ✓ |
| 构建默认配置 | `sdkconfig.defaults` 含 FreeRTOS trace facility 与 coreid 两行 | ✓ |
| OTA 分区 | `partitions.csv` 只有 `factory` + `ota_0`，无 `ota_1` | ✓（缺陷，见第五节） |

引脚余量的旧口径「已用 25 个、剩 47、48」不成立：47/48 已分给功放使能与次级电源 MOS，实际共 26 个 GPIO，无剩余普通 IO（以 README 引脚表为准）。

## 三、关键坑：SDMMC 没有自动回退

注释和文档都写「优先 SDMMC，失败回落 SPI」，但代码里没有这段逻辑：

- 开机 `components/memoria_kernel/kernel.cpp:185` 只调 `SdCard::init()`，走的是 SPI 通路
- `init_sdmmc()` 全仓唯一调用点是 shell 的 `sd mmc`（`components/memoria_shell/shell.cpp:305`）

改这块时不要照注释补优先级逻辑——那段本来就没写。要么把自动回退补上，要么把注释改成实话。

## 四、记录在案、文件已丢（B 级）

- 2026-10-11 的 `GPIO-ALLOC.md`（GPIO 账本，没进重建仓，内容已并入本文与 README 引脚表）——2026-11-08 复查：全仓无同名文件，确认未恢复
- 2026-10-11 对 `docs/硬件采购清单.docx` 的更新——**已在仓内**：现文件是 v4（2026-10-10，对齐 BOM v3 + 预算 200 + 焊坏备件），归档 zip 里那份是 v2（2026-10-03）。仓内版更新，不要拿归档版覆盖
- 1.5.0 期间除引脚定版与文档之外的固件改动：无记录，无法确认是否存在

## 五、已知未修

- **OTA 第二分区**：只有 `ota_0` 一个槽，设备升级一次后跑在 `ota_0` 上，再升级时 `esp_ota_get_next_update_partition()` 找不到可写目标，第二次 OTA 必失败。建议加 `ota_1` 并开 app rollback，从 `mem_fat` 腾约 1.9MB。
- **ILI9486 并口屏**：8bit 并口要吃 14 个 GPIO，引脚表已满；固件全链条按 ILI9488 SPI 写。
- **MCP23017 封装**：到手 SOIC-28 贴片，BOM 要 DIP-28。

## 六、彻底缺失、需要重做（C 级）

下表是原件缺失，不是同类替代品缺失。`design/` 里有功能相近的图纸（点到点电路原理图、电路接线图、硬件正面布局、总装工程图），但都不是下表这几份原件。

| 产物 | 原位置 | 说明 | 2026-11-08 复查 |
|------|--------|------|----------------|
| 原理图 HTML | `I:\Memoria OS\MemoriaOS原理图.html` | KiCad/EPLAN 风格，AMS1117 供电版 | 全仓无此文件；`design/` 的图纸是同题材另一份 |
| 自绘符号库 | `I:\Memoria OS\kicad_lib\Memoria_OS.kicad_sym` | 含 MT3608、TP4056，官方库与乐鑫库里没有 | 全仓无 `*.kicad_sym` |
| BASIC 游戏预览页 | `I:\Memoria OS\memoria-os\simulator\BASIC游戏预览.html` | 单文件 Canvas，复刻 5 个游戏 | `simulator/simulator.html` 是 480×320 键盘模拟器，不是它 |
| 完整接线表 docx | `I:\Memoria OS\Memoria OS完整接线表-打印版.docx` | 电源树 ASCII 流程 + SD 卡座 10 脚标注 | 全仓无 `*接线表*.docx` |
| 键盘矩阵接线表 docx | `I:\Memoria OS\键盘矩阵接线表-打印版.docx` | 二极管方向约定在内 | 全仓无 `*接线表*.docx` |
| 上述 docx 的生成脚本 | `I:\Memoria OS\电路\_tools\gen_full_docx.py` | | 全仓无此脚本 |

## 七、旧 KiCad 图已知错误（重画接线时避开）

2026-10-27 导出的网表与 `drivers_config.hpp` 比对，6 处确定对不上：

| 信号 | 旧图画成 | 源码实际 | 备注 |
|------|----------|----------|------|
| I2S_DOUT | GPIO45 | GPIO15 | 45 是 strapping 脚 |
| I2S_BCLK | GPIO39 | GPIO18 | 39 后来给了 SDMMC_CMD |
| MIC_SD | GPIO38 | GPIO16 | 38 是摇杆 SW |
| JOY_BTN | GPIO16 | GPIO38 | 与 MIC 那行对调 |
| CHRG_DET | GPIO13 | GPIO42 | 13 是 SPI_MISO |
| SUBMOS_EN | 画到 EN 复位脚 | GPIO48 | 位置错 |

另有 2 处未能定位到具体信号。旧原理图原件随 I 盘丢失，重画时以本表与 `drivers_config.hpp` 为准，不要沿用旧图。

## 八、只讨论、未落地

- VMware / x86 移植（源码盘点过，未编译、未出 ISO）
- ILI9486 并口驱动、屏型号取舍（9488 / 9481 / 9486）
- 两片 MCP23017 地址分配、DevKitC 板载供电 vs 纯模组保留 AMS1117
- 彩蛋 #33 / #35（计算器、文件与成就系统）
- 5 个 BASIC 游戏的 `.bas` 源文件：预览页有，源文件是否落过盘不确定

## 九、本段新增、1.4.0 里没有的

- `archive/snapshots/`：三份旧快照归档
- `tools/push_api.py`：Git Data API 推送工具（443 被重置时用），逐提交重建 + blob 逐字节校验，token 只读环境变量
- `firmware/memoria_os_1.6.1.bin`：1,884,384 B，SHA256 `59EC2755…4D28`（v1.6.0 那份已被 1.6.1 替换）
- `docs/HEALTH-REPORT.md` 第四节「2026-11-07 复查」

## 十、归档 zip 26 项核验（2026-11-08）

`archive/legacy-quoted-dirs-2026-11-07.zip`（888,962 B，26 项）是 I 盘损坏时唯一带出来的图纸与模型包。条目名被打成 UTF-8 字节的三位八进制分段，例如 `345/233/276` = `E5 9B BE` = 「图」。

**还原规则**（可复现）：每个以三位八进制开头的分段取前三位转一个字节，分段剩余字符按字面拼接；非八进制分段按 UTF-8 字面并补路径分隔；路径末位的 `_` 是转义残留，去掉。

**核验方式**：按上表规则解压到 `archive/legacy-quoted-dirs-2026-11-07/`（保留 `_design` / `_docs` / `_simulator` 三层原结构），再与 `design/`、`simulator/`、`docs/` 中现文件做 SHA-256 逐字节比对。

**结论：这批资产没有丢，全部在 `design/` 与 `simulator/` 里。**

| 结果 | 数量 | 明细 |
|------|------|------|
| 逐字节相同 | 22 | 8 张设计 HTML、键盘按键布局表.xlsx、3 个 .123dx/STL 组（8 个 STL）、外壳尺寸规划、启动CHAT代理.bat |
| 文本相同、仅样式字节不同 | 3 | 整机装配电路模型.html、同款_自包含版、洞洞板点位图.html（提取纯文本后 diff 为 0 行） |
| 仓内版更新 | 1 | `docs/硬件采购清单.docx`：仓内 v4（2026-10-10）> 归档 v2（2026-10-03） |

归档名与仓内现名的对应（建档时做过改名，内容未变）：

| 归档内 | 仓内现名 |
|--------|----------|
| `_design/3D/模型外壳.123dx` | `design/3D模型/外壳.123dx` |
| `_design/3D/模型外壳STL/前面板.stl`、`壳体`、`底仓壁`、`底壳`、`隔板①②③`、`顶盖` | `design/3D模型/外壳STL/` 同名去掉「模型外壳」前缀 |
| `_design/3D/模型外壳STL预览.html` | `design/3D模型/外壳STL预览.html` |
| `_design/3D/模型整机装配电路模型(_自包含版).html` | `design/3D模型/整机装配电路模型(_自包含版).html` |
| `_design/3D/模型核心板电路模型.html` | `design/3D模型/核心板电路模型.html` |
| `_design/3D/模型洞洞板布局与外壳尺寸.html` | `design/3D模型/洞洞板布局与外壳尺寸.html` |
| `_design/3D/模型洞洞板点位图.html` | `design/3D模型/洞洞板点位图.html` |
| `_design/shell-design/Memoria-OS/外壳尺寸规划.html` | `design/shell-design/Memoria-OS外壳尺寸规划.html` |
| `_docs/硬件采购清单.docx` | `docs/硬件采购清单.docx`（仓内版更新，不要覆盖） |
| `_simulator/启动CHAT代理.bat` | `simulator/启动CHAT代理.bat` |

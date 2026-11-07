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

- 2026-10-11 的 `GPIO-ALLOC.md`（GPIO 账本，没进重建仓，内容已并入本文与 README 引脚表）
- 2026-10-11 对 `docs/硬件采购清单.docx` 的更新（加 SD 大卡座、1N4148 ×60、洞洞板按 100×150）——仓里仍是 1.4.0 旧版
- 1.5.0 期间除引脚定版与文档之外的固件改动：无记录，无法确认是否存在

## 五、已知未修

- **OTA 第二分区**：只有 `ota_0` 一个槽，设备升级一次后跑在 `ota_0` 上，再升级时 `esp_ota_get_next_update_partition()` 找不到可写目标，第二次 OTA 必失败。建议加 `ota_1` 并开 app rollback，从 `mem_fat` 腾约 1.9MB。
- **ILI9486 并口屏**：8bit 并口要吃 14 个 GPIO，引脚表已满；固件全链条按 ILI9488 SPI 写。
- **MCP23017 封装**：到手 SOIC-28 贴片，BOM 要 DIP-28。

## 六、彻底缺失、需要重做（C 级）

| 产物 | 原位置 | 说明 |
|------|--------|------|
| 原理图 HTML | `I:\Memoria OS\MemoriaOS原理图.html` | KiCad/EPLAN 风格，AMS1117 供电版 |
| 自绘符号库 | `I:\Memoria OS\kicad_lib\Memoria_OS.kicad_sym` | 含 MT3608、TP4056，官方库与乐鑫库里没有 |
| BASIC 游戏预览页 | `I:\Memoria OS\memoria-os\simulator\BASIC游戏预览.html` | 单文件 Canvas，复刻 5 个游戏 |
| 完整接线表 docx | `I:\Memoria OS\Memoria OS完整接线表-打印版.docx` | 电源树 ASCII 流程 + SD 卡座 10 脚标注 |
| 键盘矩阵接线表 docx | `I:\Memoria OS\键盘矩阵接线表-打印版.docx` | 二极管方向约定在内 |
| 上述 docx 的生成脚本 | `I:\Memoria OS\电路\_tools\gen_full_docx.py` | |

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

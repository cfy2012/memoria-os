# Memoria OS 版本记录

> 一条规矩：**改一部分就推一版**。这个文件随每次发版更新，版本号只增不补。

## v1.6.1 — 2026-11-07 · 从轨迹补丁复原 10-11 批 GUI 改动

**背景**：v1.6.0 是按记忆重建的。10-11 那批 BASIC GUI 改动当年写在 I 盘的另一个目录树里，从未入库，随盘一起丢了。本次用 `docs/recovery/patches/*.edits.txt`（会话轨迹里的 Edit 原文，OLD/NEW 全文）逐处重放，把 1.4.0 里没有的这部分补回来。

**重放结果**：40 处 Edit 全部精确落位（`window_hpp` 1、`window_cpp` 6、`basic_host_hpp` 8、`basic_host_cpp` 25），代码增量 +341 / −105 行。应用工具 `tools/apply_edits.py`，按 OLD 唯一匹配、匹配不上即停。

| 类别 | 内容 |
|------|------|
| 显示层 | `draw_text_utf8` / `text_width_utf8` 新增 scale 重载，1~3 倍整数放大（中文最大 48×36，0 或负数夹到 1）；旧签名转调新签名，全仓调用点不用改 |
| BASIC 列表 | 列表改多实例 `list id,x,y,w,h`（旧写法视作 id=0）；行高 16px、最多展示 400 行、保留 300 行、最小换行宽 `max(40, w-6)` |
| BASIC 按钮 | `btn` 增加 icon / font 参数，按钮上限 8 → 16 |
| BASIC 图标 | 新增 `ICON id,"16 行 × 16 字符"`，id 100~139 共 40 个 |
| BASIC 彩蛋 | 新增 `EGG` / `EGGLIST` / `EGGSCROLL`，`EGGS[24]` 数据表（E01~E21 + C1~C3），图鉴行高 20px |
| 兼容 | `_list_add` 保留单参重载（默认写 id=0 列表），v1.4.0 的 HTTP 提示调用点无需改动 |

**未恢复**：`tools/pc/basic.cpp`（PC 模拟器）。补丁是"在已有文件上改 5 处"，而该文件本体从未入库，没有底稿，无法凭增量重建。

**验证**：`idf.py build` 零警告零错误；`memoria_os.bin` 1,884,384 B（0x1CC0E0），factory 分区余 39%，相对 v1.6.0 增 7,136 B。版本串同步 `CMakeLists.txt` / `system_info.h` / `firmware/version.json`。

## v1.6.0 — 2026-11-07 · 硬盘损毁后的重建基线

**背景**：本地工作盘（I 盘）物理磕坏，源码、构建产物、未推送的改动全部丢失。本次从
GitHub `cfy2012/memoria-os` 完整回落到 `G:\Memoria OS`，基线提交 `f2413394`（2026-10-10，v1.4.0，54 次提交）。

**版本号策略**：v1.5.0 的源码从未推送、已随盘丢失，**不补号、不复用**。版本号从 v1.6.0 重新起算，此后按"改一部分推一版"推进。

**本版内容**

| 类别 | 内容 |
|------|------|
| 引脚 | `CHRG_DET_GPIO` 49 → 42（49 让位给 SDMMC 段收口，42 与 47/48 相邻） |
| 引脚 | 新增 `SDMMC_CLK_GPIO=40` / `SDMMC_CMD_GPIO=39` / `SDMMC_D0_GPIO=41`（1-bit 大卡座） |
| 驱动 | `SdCard::init_sdmmc()`：SDMMC 1-bit 通路，与 SPI 共用 `/mem_fat`，优先 SDMMC、失败回落 SPI |
| Shell | 新增 `sd mmc` 手动挂 SDMMC 大卡；`sd` 状态行显示 `bus=SPI/SDMMC` |
| Shell | `info` 版本串改引 `MEMORIA_OS_VERSION` 宏，不再硬编码（根治发版漏改） |
| 版本串 | 统一 1.6.0：`CMakeLists.txt` / `system_info.h` / `shell.cpp` / `firmware/version.json` |
| 文档 | 旧快照归档至 `archive/snapshots/`；`WIRING.md` 补 39/40/41/42 四脚 |

**完好保留的既有能力（v1.4.0 起就在仓库里，本次未动）**

- 深睡眠整套：`power.cpp` 的 `boot_defaults()` / `amp_set_en()` / `subm_rail()` / NVS 关机标志 / `deep_sleep_now()`（EXT0 GPIO2 低电平唤醒）；`kernel.cpp` 七步关机序列
- AudioGuard 音频保护链：12Hz 一阶高通（去 DC 与次声波）+ 软限幅（阈值 0.85，-1.4dB 内直通，超出平滑压回）+ 热量保护，覆盖 WAV / MP3 / M4A / AAC / MJPEG 全部播放路径
- 低电量保命（3.5V 检测 + 倒计时）、侧面键长按关机、OTA 检查/升级、应用商店 .msp/.MBND 包管理

**构建链路修复（换机器/换盘必踩的坑）**

| 问题 | 修法 |
|------|------|
| 链接期 `undefined reference to uxTaskGetSystemState`（`shell.cpp` 145/200 两处） | `sdkconfig.defaults` 的 FreeRTOS 段补 `CONFIG_FREERTOS_USE_TRACE_FACILITY=y` 与 `CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID=y`。`sys` 命令列任务栈水位依赖该 trace facility，默认关闭时换块盘就链接失败 |
| 已存在的 `sdkconfig` 不会从 defaults 重新生成 | 同步把 `sdkconfig` 里对应两行置 `y`，保证本次与下次构建口径一致 |

**稳定性处理（本轮静态审查后改动）**

| 位置 | 改动 | 原因 |
|------|------|------|
| `power.cpp` 渐暗循环（两处） | 硬编码 `LEDC_CHANNEL_0` → `static_cast<ledc_channel_t>(LEDC_CHN_BACKLIGHT)` | 通道号由 `drivers_config.hpp` 唯一定义；硬编码会让"手动设亮度"与"自动渐暗"在改通道时打到不同通道 |

其余电源链（`boot_defaults()` 功放 EN=0 / 次级 P-MOS 低导通 / KEY_LED 拉低 / 等 50ms；`deep_sleep_now()` EXT0 GPIO2 低唤醒；NVS `pwr`/`shutdown` 标志）逐段复读后确认完好，未改。全仓 21 处 `xTaskCreate*` 的栈与优先级配置复查未见异常。

**验证**

- `idf.py build` 全绿：1261 步，`Project build complete`，退出码 0
- `build/memoria_os.bin` = **1,877,248 B**（0x1CA500），smallest app partition 0x2F0000，剩余 39%
- SHA256 `97FB4F0471E5FF3B587F7F0A793A5D93B07E0E1EB68BACA8426275400B4949FB`
- 分发包已随仓：`firmware/memoria_os_1.6.0.bin`（与上面同一份，逐字节复制）

**未随本版上线（需凭据）**：远端 `cfy2012/memoria-os` 仍是 `f2413394`；`cfy2012/memoria-ota` 的 `version.json` 仍是 1.4.0。本机 HTTPS token 已失效（401）、SSH 账号 `DualBrainOS` 对该仓库只有读权限（push 被 403 拒），推送待补凭据后进行。

---

## v1.5.0 — 缺失

源码未推送，随 I 盘损毁丢失。**不补号**，历史缺口保留在此处备查。

---

## v1.4.0 — 2026-10-10

- SETUP 模式网络管理界面（WiFi 扫描点选连接 / AP 热点自定义 / 蓝牙开关，按需射频）
- shell `wifi connect` 串口连网（safe 救场 + 无键盘裸板唯一通道）
- 模拟器 PLAY/RECORD 语句与版本串同步
- AudioGuard 音频保护链归档入仓

---

## v1.3.0 — 2026-10-07

- GUI 5 缺口语句（bkey / label / img / after / 字号）
- store 侧 MBND 应用包解包安装
- #86 shell 误退出 panic 修复（错误路径 return 1 → return 0）
- 真机烧板验收通过，boot 1.7s 全链零 panic

# Memoria OS 版本记录

> 一条规矩：**改一部分就推一版**。这个文件随每次发版更新，版本号只增不补。

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

**验证**：`idf.py build` 全绿（见本节下方构建记录）。

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

# Memoria OS · 项目现状与待办

> 生成：2026-11-07（v1.6.0 重建基线）· 上一版：2026-10-07（v1.3.0，已归档 `archive/snapshots/PROJECT-STATUS-2026-10-07-v1.3.0.md`）
> 本文件是项目「当前真实状态」的汇总快照，接手先读。详细账本仍以各原始文档为准。

## 一、项目一句话

ESP32-S3 N16R8 上的自研掌机操作系统：内核、窗口、文件系统、脚本引擎、BASIC 解释器、应用商店全部手写，不依赖任何 UI 框架。最终形态是一台能送人的小电脑——礼物背景见 README「项目定位」。

## 二、多线状态速览（2026-11-07）

| 线 | 状态 | 说明 |
|----|------|------|
| 仓库落地 | 🟢 完成 | I 盘物理损坏后，从 GitHub 完整回落到 `G:\Memoria OS`；基线 `f2413394`（v1.4.0，54 次提交） |
| 固件主线 | 🟢 **v1.6.0 编译全绿** | 1261 步构建通过，bin 1,877,248 B / SHA256 `97FB4F04…`，factory 分区余 39% |
| 版本账 | 🟢 厘清 | v1.5.0 源码从未推送、随盘丢失，**不补号**；1.6.0 起重新起算 |
| 引脚/接线 | 🟡 CHRG 与 SDMMC 已定 | CHRG 49→42；新增 SDMMC CLK40/CMD39/D0 41（1-bit）；已用 25 脚，剩 47、48 |
| OTA 分发 | 🔴 现网仍是 1.4.0 | 本地已备 `firmware/memoria_os_1.6.0.bin` + `version.json`(1.6.0)，未上传 |
| 远端仓库 | 🔴 未更新 | 远端 `cfy2012/memoria-os` 仍停在 `f2413394`；推送缺有效写凭据（详见六） |
| 硬件到位 | 🟡 卡在两件料 | 到手屏是 ILI9486 8bit 并口，固件走 ILI9488 SPI；MCP23017 到手 SOIC-28，BOM 要 DIP-28 |
| 上板实测 | 🟡 进行中 | 烧录/boot/命令行已验；键盘、TF 卡、音频、外设待上 |
| 稳定性 | 🟢 本轮一轮静态处理 | 见 `docs/CHANGELOG.md` v1.6.0「稳定性处理」与「构建链路修复」两节 |

## 三、已闭环事项（不用再碰）

| # | 事项 | 结果 |
|---|------|------|
| 1 | GPIO 引脚迁移（A 案） | 7 处全改、双背书全绿、账本回写，正式关闭 |
| 2 | 解码线（MP3/M4A） | AAC 静默 bug 修复 + 最终背书，三路播放全绿 |
| 3 | 字库 P1.6 移 TF 卡 | 三重静默 bug 修复（半量数据/顺序错位/越界）+ 固件瘦身 231KB |
| 4 | 键盘方案定版 | 2× MCP23017（TCA8418 采购不可得弃用） |
| 5 | 摇杆方案定版 | 甲案 KY-023（杆帽穿顶，整机 ≈40.1mm） |
| 6 | ESP32 形态定版 | S3-Zero 类核心板（裸模组弃用） |
| 7 | 外壳尺寸标准 | HARDWARE-DIMENSIONS v7 + 叠层核算 + 尺寸规划 HTML |
| 8 | 三方协作基建 | AI-BRIDGE.md 桥协议生效；开工帖→干活→完工帖两段式纪律固化 |
| 9 | v1.2.1 固件全链 | memoria_math 解方程 + RND esp_random + I2C trans_queue_depth panic 修复 + CPU 240MHz + CALC 键盘扩展 |
| 10 | BASIC PC 模拟器 | basic.cpp 全功能重造（winsock 真 HTTP）；RND 与固件逐位对齐；教材整册重写 |
| 11 | GitHub 全量上云（首次） | 远程 main = 本地全量（Git Data API 快照推送，逐字节校验 SNAPSHOT-OK） |
| 12 | GUI 打包器 msp-packer v1.0.0 | tools/msp-packer/（tkinter 零依赖）+ Release exe + MSPACK 格式/CRC 与固件逐位对齐 |
| 13 | 掌机部署文档 | docs/MSP-DEPLOY.md 三步部署指南 |
| 14 | chat 建房补丁 | api.php login 自动建房；php -l 0 错 + 本地全链冒烟 PASS |
| 15 | BASIC v1.2 语句集 + 输入法 | 字符串变量、httpget/httpup/httpdl、record/play、dim/文件流/块 if/def fn、拼音输入法、网络管理页 |
| 16 | GUI 5 缺口 + MBND（v1.3.0） | bkey/lbl/llbl/lbld/img/after/style + store MBND 解包安装 |
| 17 | #86 shell 误退出 panic 修复 | 四处 `return 1` 被主循环误读为退出 → 改 `return 0`，真机复验通过 |
| 18 | talk_gui 串口监视终端 | 端口轮询 + 六仪表实时监控 + 命令交互 |
| 19 | 应用包统一 .msp 政策 | 商店仅认 .msp；桌面图标分页；PC 打包器升级「应用打包工作台」 |
| 20 | 挂测基建 | burnin_logger.py + burnin.bat：日志落盘/异常标红/电压曲线/断线重连 |
| 21 | 深睡眠整套（v1.4.0 起） | `power.cpp` boot_defaults/amp_set_en/subm_rail/NVS 关机标志/deep_sleep_now（EXT0 GPIO2）；`kernel.cpp` 七步关机 |
| 22 | AudioGuard 音频保护链（v1.4.0 起） | 12Hz 一阶高通 + 软限幅（阈值 0.85，-1.4dB 内直通）+ 热量保护，覆盖 WAV/MP3/M4A/AAC/MJPEG 全播放路径；`docs/audio-guard/` 存 48 首前后对比 |
| 23 | 硬盘损毁后重建（v1.6.0） | 全量回落 + 构建可复现修复 + 引脚收口 + 26 条带引号脏路径清理 + 旧快照归档 |

## 四、待办任务表

> 优先级：P0 = 卡住进度的最关键一步；P1 = 主流程；P2 = 增强。责任人 = 谁必须动手/拍板。

| # | 优先级 | 任务 | 责任人 | 依赖 | 状态 |
|---|--------|------|--------|------|------|
| 1 | **P0** | 补 GitHub 写凭据并推送 v1.6.0（HTTPS token 已 401；SSH 账号 DualBrainOS 无写权限） | 用户 | cfy2012 账号的 PAT，或把 DualBrainOS 加为协作者 | 🔴 |
| 2 | **P0** | ILI9486 并口屏与固件 ILI9488 SPI 驱动的适配抉择（换屏 or 加并口驱动分支） | 用户拍板 + 固件改 | 屏已到手 | 🔴 |
| 3 | **P0** | MCP23017 封装换货（SOIC-28 → DIP-28，或改用转接板） | 用户 | 商家换货 | 🔴 |
| 4 | **P0** | TF 卡整目录拷 `sdcard/`（字库/码表/chat.bas） | 用户 | 卡 | ⚪ |
| 5 | P1 | I2S / TF / MIC 引脚两套口径定版（见六「待定」） | 用户 | 实物接线确认 | 🟡 |
| 6 | P1 | 摇杆/电池/喇叭/灯带逐项上板验证 | 用户 | 接线 | ⚪ |
| 7 | P1 | 外壳 3D 建模（卡尺实测后开工） | — | 器件到货 | ⚪ |
| 8 | P1.5 | 麦克风 INMP441 上板验证 | — | 上板稳定后 | ⚪ |
| 9 | P2 | OTA 真机验证（等 1.6.0 上线 jsdelivr 后） | 用户 | #1 | ⚪ |
| 10 | P2 | 42 小时挂测 + 72 小时出货验收 | 用户 | 上板稳定 | ⚪ |
| 11 | P2 | 三复位源排查（BOD 欠压 / task_wdt / StoreProhibited panic），`HEALTH-REPORT.md` 列为最高优先级 | 固件 | 上板复现日志 | ⚪ |

## 五、产物现状（2026-11-07）

| 产物 | 值 | 备注 |
|------|-----|------|
| `build/memoria_os.bin` v1.6.0 | 1,877,248 B / SHA256 `97FB4F0471E5FF3B587F7F0A793A5D93B07E0E1EB68BACA8426275400B4949FB` | 1261 步全绿，factory 分区余 39% |
| `firmware/memoria_os_1.6.0.bin` | 同上，逐字节复制 | 待随 OTA 上线 |
| `firmware/version.json` | version 1.6.0 / url 指向 `memoria_os_1.6.0.bin` / major false | 本地已改，远端仍是 1.4.0 |
| 本地 git | `main` = `d4f3ebf`（2026-11-07） | 远端 `f2413394`，差 1 提交 |
| OTA 远端 | `memoria-ota` firmware 目录：1.0.0 / 1.1.0 / 1.2.1 / 1.4.0 四个 bin + version.json | version.json 实测 1.4.0 |
| 旧快照归档 | `archive/snapshots/` 三份（MEMORY-SNAPSHOT v1.1.2 / PROJECT-STATUS v1.3.0 / HEALTH-REPORT 10-06） | 原文件保留未删 |
| 脏目录留档 | `archive/legacy-quoted-dirs-2026-11-07.zip`（26 文件 / 888,962 B） | 三个 `_design`/`_docs`/`_simulator` 已删除 |

## 六、风险与缺口

- **推送凭据是当前唯一硬卡点**：`github.com:443` HTTPS 被重置（`WinError 10054`）；本机 credential 里的 `ghp_` token 对 API 返回 401；SSH key 认证为 `DualBrainOS`，对该仓库 `ls-remote` 可读、`push` 被 403 拒。`api.github.com` 可达（可读仓库内容，不能写）。缺一个对 `cfy2012` 有 `repo` 权限的凭据。
- **显示驱动与实物不符**：到手 ILI9486 是 8bit 并口，固件全链条按 ILI9488 SPI 写。这不是改引脚能解决的，要么换屏，要么加一套并口驱动。
- **MCP23017 封装错**：到手 SOIC-28 贴片，BOM 写的是 DIP-28。贴片不能直插洞洞板。
- **I2S/TF/MIC 引脚两套口径未定**：代码 `drivers_config.hpp` 是 BCLK18 / WS21 / DOUT15 / MIC16 / TF CS17；记忆里的「最终定版」是 DOUT16 / MIC17 / TF CS18 / BCLK19 / WS22。除 CHRG 外无文档硬证据，本轮只动了 CHRG 与新增 SDMMC，未擅自改这套。
- **SPI_MISO 不一致**：代码写 13，`docs/WIRING.md` 旧版写 15。本轮 WIRING.md 表格以代码为准保留 13，待实物接线时二次确认。
- **三复位源未处理**：BOD 欠压 / 看门狗 / StoreProhibited panic 仍挂在 `HEALTH-REPORT.md` 最高优先级，需要上板复现日志才能定位。
- **v1.6.x 其余改动清单已随盘丢失**：当前 1.6.0 的边界是「v1.4.0 基线 + 记忆清单增量」，不是原 1.6.0 的全貌。

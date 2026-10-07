# Memoria OS · 项目现状与待办

> 生成：2026-10-07 · 来源：AI-BRIDGE.md（三方协作桥）、问题看板、GPIO-ALLOC.md、WIRING.md、真机 boot 日志
> 本文件是项目「当前真实状态」的汇总快照，接手先读。详细账本仍以各原始文档为准。

## 一、项目一句话

ESP32-S3 N16R8 上的自研掌机操作系统：内核、窗口、文件系统、脚本引擎、BASIC 解释器、应用商店全部手写，不依赖任何 UI 框架。最终形态是一台能送人的小电脑——礼物背景见 README「项目定位」。

## 二、多线状态速览（2026-10-07）

| 线 | 负责人 | 状态 | 说明 |
|----|--------|------|------|
| 固件主线 | GLM | 🟢 **v1.3.0 烧板验收通过** | GUI 5 缺口语句 + store MBND 应用包 + #86 修复；bin 81490cd1 / 1,835,936 B；真机 boot 1.7s 全链零 panic |
| 解码线 | 豆包-2 | 🟢 闭环 | WAV / MP3 / M4A 三路播放全绿；AAC 返回值 bug 已修 |
| 桥务/外壳 | 豆包-1 | 🟡 进行中 | 模拟器 GUI 高完成度（CHAT 应用/新建房间闭环）；3D 建模待料到手实测尺寸 |
| 聊天服务端 | GLM | 🟢 补丁就绪 | chat（PHP+JSON）全功能；建房自动创建补丁已验，部署包待用户上传 |
| 发布/生态 | GLM | 🟢 已上云 | GitHub 全量源码仓 + OTA jsdelivr CDN 链路 + msp-packer Release |
| 上板实测 | **用户** | 🟡 进行中 | 烧录/boot/命令行已验；键盘接线、TF 卡、外设待上 |

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
| 9 | v1.2.1 固件全链 | memoria_math 解方程 + RND esp_random 硬件真随机 + I2C trans_queue_depth panic 修复 + CPU 240MHz + CALC 键盘扩展；真机 boot 全链零 panic |
| 10 | BASIC PC 模拟器 | basic.cpp 全功能重造（winsock 真 HTTP）；RND 与固件逐位对齐；教材整册重写 |
| 11 | GitHub 全量上云 | 远程 main = 本地全量（Git Data API 快照推送，逐字节校验 SNAPSHOT-OK）；OTA jsdelivr CDN 实测可达 |
| 12 | GUI 打包器 msp-packer v1.0.0 | tools/msp-packer/（tkinter 零依赖）+ GitHub Release exe + MSPACK 格式/CRC 与固件逐位对齐 |
| 13 | 掌机部署文档 | docs/MSP-DEPLOY.md 三步部署指南（打包→上传→store 安装） |
| 14 | chat 建房补丁 | api.php login 自动建房；php -l 0 错 + 本地全链冒烟 PASS |
| 15 | **BASIC v1.2 语句集 + 输入法** | 字符串变量（$ 中文原生）、httpget/httpup/httpdl、record/play、dim 数组/文件流/块 if/def fn、拼音输入法（F1~F4 选字/Ctrl+Shift 切换）、系统设置网络管理页 |
| 16 | **GUI 5 缺口 + MBND（v1.3.0，豆包-2）** | bkey/lbl/llbl/lbld/img/after/style 控件语句 + store MBND 应用包解包安装（app.bas/manifest/资源，卸载连带清理）；编译两次全绿 |
| 17 | **#86 shell 误退出 panic 修复（GLM）** | shell.cpp 四处错误路径 return 1 被主循环误读为退出 → abort 重启；改 return 0 后真机复验通过（`scripts` 不再炸机） |
| 18 | **talk_gui 串口监视终端** | 端口选择窗 5 秒轮询 + 六仪表实时监控 + 命令交互，深色主题，真窗口程序 |
| 19 | **应用包统一 .msp 政策** | 商店仅认 .msp，裸 .bas/.c 禁止直下；桌面图标分页（末行下滑翻页）；PC 打包器升级"应用打包工作台" |
| 20 | **挂测基建** | burnin_logger.py + burnin.bat：日志落盘/异常标红/电压曲线/断线重连/总结报告；42h 基础关 + 72h 出货验收两级标准 |

## 四、待办任务表

> 优先级：P0 = 卡住进度的最关键一步；P1 = 主流程；P2 = 增强。责任人 = 谁必须动手/拍板。

| # | 优先级 | 任务 | 责任人 | 依赖 | 状态 |
|---|--------|------|--------|------|------|
| 1 | **P0** | MCP23017 键盘接线（WIRING.md 为准：GPIO33~37 禁接、A0 地址配置、二极管方向） | 用户（真机） | 板上排线 | ⚪ |
| 2 | **P0** | TF 卡整目录拷 `sdcard/`（字库/码表/chat.bas）→ SD/中文/脚本全解锁 | 用户 | 卡到手 | ⚪ |
| 3 | **P0** | chat-deploy.zip FTP 上传虚拟主机 → `?a=ping` 返回 `pong chat v1` 即生效 | 用户 | zip 已重打 | ⚪ |
| 4 | P0 | 摇杆/电池/喇叭/灯带逐项上板验证（GPIO-ALLOC §5 清单打勾） | 用户 | 接线 | ⚪ |
| 5 | P1 | **TF 卡批次真机验收**：chat.bas 文字+语音收发、GUI 5 缺口程序级验证、MBND 安装闭环、中文显示（基线 bin 81490cd1 口径） | 用户+豆包-2 | #2/#3 | ⚪ |
| 6 | P1 | 真机跑《猜数字.bas》三连复跑（与 PC 模拟器 RND 行为对照） | 用户 | TF 卡就位 | ⚪ |
| 7 | P1 | 外壳 3D 建模（料到手卡尺实测后开工；纸模校验 + 小样试打见 SHELL-CHECKLIST.md） | 豆包-1 | 器件 ~10-15 到货 | ⚪ |
| 8 | P1.5 | 麦克风 INMP441 接入上板验证（驱动已含 GPIO16 SD 线，复用 I2S 时钟） | GLM/豆包-1 定人 | 上板稳定后 | ⚪ |
| 9 | P2 | OTA 真机验证（掌机在线升级吃 jsdelivr 链路，v1.3.0 分发物已随仓） | 用户+GLM | 上板后 | ⚪ |
| 10 | P2 | 全量源码审查（GLM 固件 / 豆包-2 解码 / 豆包-1 驱动） | 三方 | 排期 | ⚪ |
| 11 | P2 | 42 小时挂测开跑（burnin_logger，命令行界面驻留取电压曲线）→ 72 小时出货验收 | 用户 | 上板稳定后 | ⚪ |
| 12 | P2 | GitHub 临时 token 用户手动撤销（对话中明文出现过，10-13 自动过期） | 用户 | — | ⚪ |

## 五、产物现状（2026-10-07）

| 产物 | 值 | 备注 |
|------|-----|------|
| memoria_os.bin v1.3.0 | 81490cd1d07e2618 / 1,835,936 B @14:16:46 | GUI 5 缺口 + MBND + #86 修复；真机烧录 Hash verified |
| OTA 结构 | firmware/memoria_os_<版本>.bin + version.json | GitHub 仓库 + jsdelivr CDN 双可达；v1.3.0 随源码仓发布 |
| GitHub 仓库 | cfy2012/memoria-os，main | 源码全量 + 文档 + PC 工具源码；exe 走 Release 不入库 |
| GUI 打包器 | tools/msp-packer/ 源码随仓 + Release exe | 打包工作台：选文件夹/填名称/选图标一键出包 |
| 串口监视终端 | tools/talk_gui.py（打包 exe 走 Release） | 六仪表 + 命令交互 + 端口轮询 |
| chat 部署包 | changliao/dist（zip 不入库） | 含建房补丁，待用户 FTP 上传 |
| TF 卡镜像 | `sdcard/` 整目录 | 字库 + 拼音码表 + 三个内置应用 |

## 六、风险与缺口

- **键盘/SD/字库三项硬件依赖未到位**：MCP23017 未接线（boot 报 0x108 无应答，init 返回值检查按设计工作不崩机）、TF 卡未插（SD mount 失败 + GB2312 MISSING 均为裸板预期），插上即恢复。
- **5 缺口与 MBND 程序级验证受限**：裸板无 TF/键盘，BASIC 程序入口不可用，本轮只验到"固件健康启动 + shell 可用"层；插卡后按豆包-2 验收单跑闭环。
- **真机外设实测未开始**：boot/命令行已验，音频播放/显示中文/键盘输入需卡和线到位。
- **10-08 死线复核**：输入法/网络管理页（v1.2.0）、语音语句与 chat.bas v2（已落地）均已完成编码，待 TF 卡批次真机验收收口。
- **.m4a 测试素材**：仍待用户拷卡。
- **token 安全**：GitHub 临时 token 已在对话明文出现，用完即撤（10-13 自动过期）。

# HANDOFF — Memoria OS 多AI协作协议（规则层）

> 新任 GLM 必读。本文件是稳定规则，时事状态看 MEMORY-SNAPSHOT.md，最新动态看桥。

## 身份与分工
- 用户：多啦（桥长，拍板人）
- 你（GLM）：固件主线 / 源码审查 / 发布线（OTA）
- 豆包-1：外壳 / 打包 / 商店线；豆包-2：音视频 / 驱动补强线
- 署名规则：桥上发言必须标 [GLM] / [豆包-1] / [豆包-2]，只追加、不改别人条目

## 桥（AI-BRIDGE.md @ _internal/）
- 发言前必须重读（不准用缓存）；每条标注状态（通报 / 待回复 / 待拍板）
- 干活前上桥报备，改完上桥闭环（小变更改完报；大变更改前报备+改后报）
- 一次只允许一方执行 build（目录锁冲突）

## 看板（_internal/）
- ISSUE-BOARD.html：问题台账，炸/险/味三级；谁发现谁录入并桥上通报；新编号接最大号；build 全绿才算闭环
- PROJECT-BOARD.html：进度总览；改完报
- 编辑纪律：**只用编辑工具逐处改，禁用 shell 重写**（UTF-8/引号会碎）；条目只插在数据数组闭合符 `"}];` 之前，绝不能碰后面的渲染逻辑
- ISSUE-BOARD 渲染 ST 状态表合法值仅：done / wait / doing / todo / hold —— 写其他值直接白屏

## 硬约束（部分）
- 完整约束见 memory/项目记忆；高频几条：
- C 盘禁止下载/安装任何东西（软件、包、临时文件都算）
- GPIO 配置唯一来源 components/memoria_drivers/include/drivers_config.hpp；引脚改动同步 WIRING.md
- build 全绿 ≠ 完成：结报 + 看板翻状态 + 桥通报，三步缺一不可
- BASIC 官方风格：关键字全小写；注释：简短工业中文，@file + @brief
- 服务器端仅 PHP（虚拟主机），HTTP 明文可接受，部署用二级域名

## 构建与烧录
- IDF v6.1 @ D:\ESP_IDF\.espressif\v6.1\esp-idf；激活脚本 D:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1（dot-source）
- venv python：D:\Espressif\tools\python\v6.1\venv\Scripts\python.exe（系统 python 无 pyserial）
- idf.py stdout 常被吞（CLIXML）：错误信息落盘再 Read；编译成败看 build/ 下 bin 的 mtime + SHA
- 烧录 COM5 460800；换分区表必须 `idf.py -p COM5 -b 460800 erase_flash flash` 全片重烧
- 验收：_build/serial_snap.py（DTR/RTS 复位 + 15s 抓取 → _build/boot_snap.txt）

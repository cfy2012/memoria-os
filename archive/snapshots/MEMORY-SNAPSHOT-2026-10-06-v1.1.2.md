# MEMORY-SNAPSHOT — GLM 记忆快照（2026-10-06 晚固化）

> 时事状态层。配套 HANDOFF.md（规则层）。数据以桥/看板/代码为准，本文件是接手捷径。

## 权威数字
- **权威 bin：build/memoria_os.bin = 0x1E3740（1,980,224 B），SHA256 前 16 位 B8CBC2929B4B064A，版本 v1.1.2，app 分区余 36%**
- 分区表（源码 = 板上已归位）：nvs 0x9000/0x6000 · phy 0xF000/0x1000 · factory 0x10000/0x2F0000 · ota_0 0x300000/0x2F0000 · mem_fat 0x5F0000/0x620000 · mem_priv 0xC10000/0x300000（无 ota_1 / coredump）
- PROJECT-BOARD：整体 87%，真机线 30%；ISSUE-BOARD：83 条（#81/#82 done，**#83 todo**，其余 done）
- app_desc version 垃圾串 "11:22:30 Oct 4 2026"（ccache 缓存 main.cpp `__TIME__`）→ PROJECT_VER 显式 set 挂账
- shell 版本串 v1.1.2（info 命令）

## 10-06 大事记
1. **修复批次收官**（四问题，真机验收 6/6）：
   - #81 炸：SD mount 出参传 nullptr → IDF 6.1 vfs_fat_sdmmc.c L377 校验直接 INVALID_ARG，插不插卡都挂。修：`&card` 出参存 `_card` 成员 + poll 重试 quiet + 新增 reformat()
   - #82 险：shell getchar 非阻塞忙等 → memoria> 刷屏 + task_wdt。修：EOF 时 vTaskDelay(10ms)
   - 板上固件勘误：**85E09F25（v1.1.1-hotfix）从未在板上运行过**——板上旧分区表 factory 0x1E0000 装不下 0x1E3250，bootloader 静默回退 ota_0 老 bin（铁证：bin 内 banner `(type 'help')` ≠ 板上输出 `Typpe "HELP"`）。erase_flash 全片重烧归位
   - SAFE 应急模式上线：无 TF 卡 → shell 进 `safe>` 态，命令 sd mount / sd format，挂载成功自动出 SAFE
2. **桥通报**：fix4（定案+开工）→ fix5（闭环）→ fix6（虚账更正+看板补账+BLE 证据移交+OTA v1.1.2 复认催办）→ fix7（changliao 安全修复）→ fix8（#83 通报）
3. **豆包动态**：豆包-2 回归认账 L32I 定案；豆包-1+2 曾拍板推 OTA v1.1.1（85E09F25）→ 前提被勘误推翻，GLM 催复认 v1.1.2；BLE 重烧验证（豆包-2 认领）已随全片重烧顺带闭环——boot_snap.txt：`NimBLE init OK` + `advertising started` + `KERNEL: BLE: ESP_OK`
4. **changliao 安全修复**：config.php 硬编码真实面板账号密码（sec270472238666 / 密码已清）+ 部署包同带密码 + .gitignore 无防护 → 重写 config.php 为仓库版（环境变量 CL_DB_* > config.local.php 覆盖，无默认密码回退）；新增 config.local.php（真实凭据，仅本地）+ config.local.example.php 模板；.gitignore 补 changliao/server/config.local.php + changliao/dist/；部署包重打 SECRET-CLEAN 销毁旧副本

## 板上现状（B8CBC292）
- 与源码树完全归位；验收 6/6：SAFE banner / safe> 提示符 / 刷屏 0 / watchdog 0 / panic 0 / 新分区表
- SD 无卡错误 = INVALID_RESPONSE（真触卡后正常无卡错误）；**残留噪音：0x108 + `gpio: conflict found for GPIO[17]` 每 ~5s poll 刷一遍（#83 待修，不阻塞插卡测试）**
- #83 疑点：CS17 被 spi master add_device 与 sdspi cs_gpio 双重声明，或 init 失败路径未卸设备重复 add

## 挂起事项（接手先看）
1. OTA v1.1.2 推送——等豆包-1/2 桥上复认；85E09F25 不可作推送对象；推 firmware/memoria_os_1.1.2.bin + version.json→1.1.2（jsdelivr CDN），仓内保留 1.1.0
2. #83 修复（sdcard.cpp 失败清理路径 + CS17 归属二选一 + 拉长 poll 间隔）——下个 build 窗口
3. PROJECT_VER 显式 set（ccache 缓存 __TIME__），@豆包-2 可选认领，不阻塞 OTA
4. changliao 服务器重部署（用户侧：上传新 zip + 建 config.local.php；建议 easypanel 改面板密码）；api.php md5 密码迁移 salted hash 另行立项
5. 用户侧：插 TF 卡 → `sd mount` → 测 chat.bas 文字+语音（httpup/httpdl）
6. 死线：10-08 输入法落地 + 掌机语音语句 + 系统设置网络管理界面；10-08 后 BOM 18 项采购下单，~10-15 到货后焊板/全面验证/画壳

## 踩坑档案（新增，旧档案看 memory/项目记忆）
- IDF 6.1 `esp_vfs_fat_sdspi_mount` 出参不可传 nullptr——从日志永远猜不到（INVALID_ARG 看似参数错实则出参空），**要读 IDF 源码定案**
- 板上固件与源码不同步时：bin 内 banner 字符串可作指纹；"build 全绿"≠"板上运行的就是它"；换分区表必 erase_flash 全片重烧；静默回退会让历史"上板验证"结论全部失效
- C++ 前向声明 `sdmmc_card_t` 与 IDF 头定义 language linkage 冲突（extern "C" 里怎么写都撞）→ 直接 `#include "sd_protocol_types.h"`（sdmmc 组件 include 路径已在编译命令行）
- sdspi_host_init() 在 IDF 6.1 是空操作，共享 SpiBus 总线不会重复初始化，放心挂设备
- shell stdin 实际非阻塞：`getchar()` 立即返 EOF，REPL 忙等会饿死 IDLE0 触发 task_wdt
- ISSUE-BOARD 的 ST 状态表没有的值会白屏（本条即 #83 差点用 "open" 踩雷）
- PowerShell：引号路径调用要 `&`；python stdout 被吞时改写文件再 Read；PHP 本机没有，语法只能人工核对

## 文件指针
- 桥：_internal/AI-BRIDGE.md（只追加；fix4~fix8 在尾部）
- 看板：_internal/PROJECT-BOARD.html（87% / 30%）· _internal/ISSUE-BOARD.html（83 条）
- changliao：server/config.php（仓库版无密）+ server/config.local.php（真实凭据，勿入库勿外发）+ server/config.local.example.php；部署包 changliao/dist/changliao-server.zip（api.php + config.php + 模板，SECRET-CLEAN）
- 验收证据：_build/boot_snap.txt（B8CBC292 15s 快照）；验收脚本 _build/serial_snap.py；桥报脚本 _build/bridge_fix4~8.py
- chat.bas（掌机聊天 v2 语音版）：sdcard/scripts/chat.bas，L89 `httpup url$, "/mem_fat/vr.wav", r$` 为发语音上传语句

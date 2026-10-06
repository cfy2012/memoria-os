# Memoria OS · 源码审查报告（W6：script/basic/prgm/shell/modes + W4/W5 三方合并）

> 生成：2026-10-01 · 审查人：豆包-1（W6/W6b 工作包，W7 三方收口）
> 格式：`文件:行号 | 问题 | 严重度(炸/险/味) | 建议修法`
> 严重度：**炸** = 运行期必错/功能造假（用户可感知）；**险** = 边界/竞态/静默错值；**味** = 坏味道非错误。
> 范围：W6（memoria_script/memoria_basic/memoria_kernel modes + shell）§一~§三、W6b（memoria_package）§六、W4（GLM：kernel/window/drivers/fs）§七、W5（豆包-2：解码线）§八。
> 状态：W4 已 build 全绿放行（bin 1,936,112B 12:14）；豆包-2 复跑背书进行中；豆包-1 完整 build 待接力。修复归属已标注。

## 一、炸（运行期必错 / 功能造假）

| # | 位置 | 问题 | 修法 |
|---|------|------|------|
| 1 | `memoria_script/src/tokenizer.cpp:76-87` | **read_number 字符集含 `+ - e E _`**：无空格表达式 `1+2` 被吞成单 token `"1+2"`，evaluator 的 `strtol` 全串校验失败**静默返回 0**（`1e3` 科学计数法同样归 0） | number 字符集限 `isdigit` + `.`；科学计数法仅在 `e/E` 之后跟 `+/-/digit` 才允许，且置 is_float |
| 2 | `memoria_script/src/tokenizer.cpp:164` | 未知/单字符符号（`& | ^ ~ @ #` 等）返回 **Eof token**：后续所有代码被静默丢弃，程序后半不执行 | 未知字符返回错误 token（如 Kind::Error）或抛异常 |
| 3 | `memoria_script/src/evaluator.cpp:136-139` | IntLit/FloatLit 全串校验失败**返回 0**（与 #1 联动） | 解析失败置错误状态，不再静默 0 |
| 4 | `memoria_script/src/parser.cpp:118-127` + `evaluator.cpp:169-175` | **`+=`/`-=` 生成 Assign 节点，Assign 分支不区分操作符**：`x += 2` 实际变成 `x = 2`（直接覆盖）；Binary 分支里的 `+=/-=` 处理是死代码 | Assign 分支按 `n.value` 处理：`=` 直接赋；`+=` → 旧值+rhs；`-=` → 旧值-rhs |
| 5 | `memoria_kernel/modes/prgm_mode.cpp:225-229` | **LINE 命令只画两个端点**（1×1 像素×2），不是画线 | 实现 Bresenham 或调用画线原语 |
| 6 | `memoria_kernel/modes/prgm_mode.cpp:230-235` | **CIRCLE 用 fill_rect 画实心方框**（2r×2r 正方形），不是圆 | 实现画圆算法（中点圆） |
| 7 | `memoria_kernel/modes/sys_mode.cpp:120-131` | **自检是假的**：按 Enter 只翻步骤文字（"① 屏幕亮度 OK"…），不做任何实际检测 | 实现真实检测（读亮度/摇杆/输出测试音），或明确标注"演示" |
| 8 | `memoria_kernel/modes/notes_mode.cpp:82-93` | **查看笔记 _body 从不从 PrivateFS 读取**：打开任何笔记永远显示占位文本"（无内容 · 真机读取 Note 数据）" | 按 _names[_view] 从 PrivateFS 读内容再渲染 |

## 二、险（边界 / 静默错值 / 资源）

| # | 位置 | 问题 | 修法 |
|---|------|------|------|
| 9 | `memoria_script/src/evaluator.cpp:189-190` | **除零静默返回 0**（`op=="/" && b!=0` 才计算） | b==0 时报错/返回 NaN 标记 |
| 10 | `memoria_basic/src/basic_interpreter.cpp:622-633` | **GOTO 同行后续语句仍执行**：`GOTO 100: PRINT "X"` 在 exec_statements 循环里不检查 `_jumped`，PRINT 照跑 | 循环内检查 `_jumped` 并 break |
| 11 | `memoria_basic/src/basic_interpreter.cpp:204-220` | **load_file 用 fgets 128B 缓冲**：超过 127 字节的行被分段解析（第二段行号解析为 0 → `_program[0]`），程序被静默破坏 | 改用 getline 或拼接缓冲 |
| 12 | `memoria_basic/src/basic_interpreter.cpp:673-678` | **GOTO 目标行不存在时 lower_bound 静默就近跳**到下一行，不报错 | GOTO 前检查行号存在，缺失报错 |
| 13 | `memoria_kernel/modes/prgm_mode.cpp:337-350` | **`_run_out_buf` 无限增长**：10 万步长输出（每步 PRINT）内存膨胀 | 裁剪上限（如 64KB 丢弃旧段） |
| 14 | `memoria_kernel/modes/store_mode.cpp:253-269` | **安装整包读入内存**（`std::string data` 装整个 .msp）+ 无大小/超时限制 | 流式下载直写 TF + 大小上限 |
| 15 | `memoria_kernel/modes/store_mode.cpp:261` | **安装文件名 `it.name` 未校验**：manifest 恶意/错误 name（如 `../x`）可写越权路径 `/mem_fat/x` | name 白名单 `[A-Za-z0-9_.-]` |
| 16 | `memoria_kernel/modes/store_mode.cpp:126-135` | **卸载只删文件不校验来源**：用户自己拷的 .bas 也会被"卸载"删除 | 仅 .msp 包可卸载，或加安装标记 |
| 17 | `memoria_shell/shell.cpp:137-141` | **`script <name>` 路径未校验**：`script ../x` 可读任意路径 | 拒绝 `..` / 白名单目录 |
| 18 | `memoria_kernel/modes/fmem_mode.cpp:53-61` | **stats 语义与 UI 标签疑似错配**：`stats(used,total,ue,te)` 的 `ue` 显示为"TF %u 项"，若 ue 实为私有区条目数则信息错误 | 核对 private_fs::stats 语义后改标签 |
| 19 | `memoria_script/src/evaluator.cpp:151-159` | **continue_pending 未在块循环检查**（只查 return/break）：continue 语义错误（与无循环实现并存） | 块循环补 continue_pending 检查 |

## 三、味（坏味道 / 占位 / 未实现）

| # | 位置 | 问题 | 备注 |
|---|------|------|------|
| 20 | `memoria_script/src/parser.cpp` | **while/for 关键字 parser 未实现**（tokenizer 有关键字，parser 当 Ident）——.ms 脚本引擎无循环 | 若 .ms 设计不需要可关关键字；需要则实现 |
| 21 | `memoria_script/src/evaluator.cpp:68-70` | `rand()` 未播种，每次复位序列一致 | 播种一次 |
| 22 | `memoria_script/src/evaluator.cpp:203-211` | Binary 分支的 `+=/-=` 是死代码（parser 不生成该类型） | 随 #4 一并清理 |
| 23 | `memoria_basic/src/basic_interpreter.cpp:590-597` | PRINT/LET/GOTO/GOSUB/RETURN/SYSTEM/END/INCLUDE **无边界检查**：`PRINTX`/`GOTOY` 前缀误判 | 补 `ul.size()==n \|\| 空白` 判断（FOR/NEXT/WHILE/IF 已有） |
| 24 | `memoria_basic/src/basic_interpreter.cpp:469-510` | FOR 循环体无条件先执行一次（经典 BASIC 语义，但 start>end 也会跑一次 body） | 确认是否要"先检查"语义 |
| 25 | `memoria_kernel/modes/notes_mode.cpp:95-111` | 新建写死示例笔记（"今天在操场拍了很多照片…"）——占位功能 | 真机字符表输入接入后替换 |
| 26 | `memoria_kernel/modes/store_mode.cpp:190-200` | pick_u32 不支持 `0X` 大写前缀（isxdigit 不含 X）→ crc 解析为 0 | 补 'X' 或按 strtoul 前缀处理 |
| 27 | `memoria_kernel/modes/sys_mode.cpp:104-105` | "大块"显示的是总空闲堆而非最大连续块 | 用 heap_caps_get_largest_free_block |
| 28 | `memoria_kernel/modes/modes_bootstrap.cpp:5` | 注释"注册 10 个模式"实际注册 11 个（含 STORE） | 改注释 |
| 29 | `memoria_shell/shell.cpp:130-135` | `scripts` 命令占位（"directory listing omitted in host build"）——真机也不列目录 | 实现 readdir 列出 |
| 30 | `memoria_kernel/modes/sys_mode.cpp:95-101` | 系统信息页缺键盘条目（2× MCP23017 已定版） | 补一行 |

## 四、待合并（W4/W5）→ 已并入 §七/§八（2026-10-01 W7 收口）

- W4（GLM）：kernel 核心链 / window / drivers / fs —— 见 §七（首批/二批/三批/尾批 4 批全量并入）
- W5（豆包-2）：i2s_audio / helix / minimp3 / mp4_demux / photo/music/video/clock modes —— 见 §八

## 五、建议修复顺序

1. **先修炸级**（#1-8）：其中 #1/#4 影响 .ms 脚本正确性，建议 GLM 或豆包-1 尽快修；#5/#6/#7/#8 是用户可感知功能错误（LINE/CIRCLE/自检/记事），上板前必修。
2. 险级 #10/#11/#12（BASIC 解释器健壮性）随 BASIC 线修复。
3. 其余按优先级排入待办。

## 六、W6b：memoria_package 审查（2026-10-01 补充）

> 真空区 12 文件（6 hpp + 6 cpp）全推完。重点：网络错误分支 / CRC 语义 / 路径校验 / NVS 读写边界。

### 炸

| # | 位置 | 问题 | 修法 |
|---|------|------|------|
| 31 | `package_format.cpp:65-66` | **verify_package 用 h.payload_size 直接 vector 分配，无上限**：损坏/恶意 .msp 声明 payload_size=4GB → 内存耗尽崩溃 | payload_size 上限（如 1MB）+ 与文件实际大小匹配校验 |

### 险

| # | 位置 | 问题 | 修法 |
|---|------|------|------|
| 32 | `package_manager.cpp:261` | **check_updates 的 e.name 未校验 → 路径穿越**（`/mem_fat/scripts/` + `../x` 可写任意路径，同 store #15 同款） | 包名白名单 `[A-Za-z0-9_.-]` |
| 33 | `package_manager.cpp:248` | **check_updates 不比对本地版本**（注释自认"简化"）：manifest 全部包每次全量下载重写，即使已是最新 | 按 name 读本地 .msp 版本比对，不一致才下 |
| 34 | `json_fetcher.cpp:54` | **skip_cert_common_name_check=true**：HTTPS 只验证书链不验主机名，中间人可注入 manifest/包（OTA 固件走 esp_https_ota 不受影响，包更新线被劫持） | 改 cert bundle 白名单/恢复 CN 校验 |
| 35 | `json_fetcher.cpp:88-92` | **下载无大小上限**：恶意服务器可无限发数据耗尽内存（Content-Length 伪造时 reserve 失效） | 加 max_size（如 4MB）超限断开 |
| 36 | `package_format.cpp:76` | verify_package 的 h.name **无 null 终止保护**：std::string 从栈缓冲区构造可能读越界（小端下通常停在 payload_size 高位 0，属 UB） | 按 32B 显式拷贝 + 检查 null |
| 37 | `update_scheduler.cpp:58` | **自动更新窗口硬编码 `t.hour < 6`**：用户设 23 点永不触发（配置 0-23 与窗口矛盾） | 窗口判断用配置值（或文档限定 0-5） |
| 38 | `update_scheduler.cpp:60-76` | **run_nightly_update 不自动连 WiFi**（注释宣称"到点自动连"）：未连即跳过，且 fired_today 已置位 → 当天失败不再重试 | 到点主动 connect（有保存的凭据时），失败再置位 |

### 味

| # | 位置 | 问题 | 备注 |
|---|------|------|------|
| 39 | `package_manager.cpp:76-82` | set_mirror_url/set_auto_* 不检查 nvs_set 返回值（NVS 满静默失败但函数返回 true） | 检查并返回 false |
| 40 | `package_manager.cpp:199-209` | pick_u32 不支持 `0X` 大写前缀（同 store #26，crc 解析 0 → 包被拒装） | 补 'X' |
| 41 | `package_manager.cpp:260-263` | manifest 缺 crc 字段时 e.crc=0 → 所有包 CRC 不匹配全被拒装（静默） | 缺 crc 应跳过校验或标注 |
| 42 | `package_manager.cpp:180-223` | parse_manifest 字段顺序敏感（name 必须出现在 version 前才解析全） | 按 key 独立扫描 |
| 43 | `package_format.cpp:44` | create_package 的 name 无长度/字符校验（32B 静默截断） | 入参校验 |
| 44 | `firmware_update.cpp:37-53` | json_find 极简解析：changelog 含转义引号截断；firmware_manifest_url 拼接依赖 mirror 是目录型 URL（TF 卡路径换源时不适用） | 记录限制 |

### W6b 修复建议

- **先修 #31（内存崩溃）与 #32（路径穿越）**——真机可触发，且 #32 与我已修的 store #15 同款修法，可直接套用。
- #33（全量重下）属行为问题，与 store 的 `_reload_remote` 语义相关，建议与商店线一起定版本比对方案。
- #34/#35（TLS/大小上限）网络健壮性，上板联网测试前修。

## 七、W4：kernel/window/drivers/fs（GLM 审查，2026-10-01 并入）

> 审查人：GLM。范围：main.cpp + kernel(24) + window(4) + drivers 除 i2s_audio/mp4_demux(9) + fs(4) = 42 文件（精读 41，剩 pinyin_dict/app_manager 细节只读推进）。状态：Z1/X1/X2 三修 + ADC 共享 + ime 高亮已落盘，**build 全绿 12:14（bin 1,936,112B）**；GLM 已冻结 drivers/kernel 修改直至两轮 build 结束。

### 炸

| # | 位置 | 问题 | 修法/状态 |
|---|------|------|------|
| 45 | `drivers/ili9341.cpp:255-262` + `spi_bus.cpp:68` | **Z1 死锁**：flush() 先 Take(mutex) 再 _set_window→_write_cmd 二次 Take 同一把锁——spi_bus 用非递归锁，同任务重复取=死锁→看门狗复位。kernel init 第 3 步 fill+flush 上电即触发：**烧录后必无限重启循环** | ✅已修：CreateMutex→CreateRecursiveMutex，_write_cmd/_write_data/_set_window 内 take/give 全换 Recursive API（7 处） |
| 46 | `drivers/battery_adc.cpp` + `joystick.cpp` | **ADC1 单元二次申请冲突**：两驱动各自 `adc_oneshot_new_unit(ADC_UNIT_1)`——IDF v6.1 对同单元二次申请返回 ESP_ERR_NOT_FOUND（源码 L98 s_adc_unit_claim 实锤）→ 后初始化者必失败：电池永远 0% 或摇杆全死 | ✅已修：新建 `adc_share.hpp`（ADC1 共享句柄 inline 单例），两驱动改取共享句柄 |

### 险

| # | 位置 | 问题 | 修法/状态 |
|---|------|------|------|
| 47 | `window.cpp` 全类 + `kernel.cpp:170-203` | **X1 无锁双任务竞争**：joystick 采样任务 publish→kernel 订阅 lambda→dispatch_nav 遍历 _stack 并 push/pop；主任务 main_loop 同时 render_all 遍历同一 _stack → 切模式瞬间迭代器失效/UAF，真机必现崩溃；ili9341 framebuffer 亦被双任务写 | ✅已修：EventBus publish 改队列投递（16 深）/ pump 主循环统一消费，joystick 任务只生产不消费；WindowManager 从此单任务访问 |
| 48 | `drivers/power.cpp:66-78+95-99` | **X2 背光覆盖设定值**：set_backlight 覆盖 _backlight 成员，渐暗循环打到 15/255 后 _dimmed=true；touch_event 恢复用 _backlight=15 → 约 6% 亮度≈全黑，用户以为死机只能重启 | ✅已修：新增 _user_brightness 保存期望值，渐暗只压 LEDC 不动成员，恢复 set_backlight(_user_brightness) |
| 49 | `drivers/ili9341.cpp:255-280` | flush 全屏 300KB @26MHz ≈ 92ms 忙等，render_all 每帧全量重绘 → 帧率上限 ~10fps，拖图标必卡 | P2 备案：脏矩形局部推屏（上板验证后再做） |
| 50 | `drivers/sdcard.cpp:40-53` + `spi_bus.cpp:57-65` | SD 走 sdspi 框架自管 device，spi_bus.cpp 还手动 add_device 同 CS17 的 sd handle（从未使用）→ 同 CS 双 device 脏设计 | 修：删 spi_bus.cpp L57-65 的 sd 段（sdspi 内部自 add）【待落盘】 |
| 51 | `main/main.cpp:22-24` | 启动失败仅静默死循环，注释宣称"灯屏红色"但无实现，用户面对黑砖无信息 | 修：ESP_LOGE + 屏已初始化时画错误码 + esp_restart()【待落盘】 |
| 52 | `kernel/event_bus.hpp:46-50` | publish 在采样任务上下文同步执行全部 handler（持锁渲染?），事件处理耗时拖慢采样节拍 | ✅随 X1 队列化闭环 |
| 53 | `modes/mode_manager.cpp:130-139` + `desktop_manager.cpp:45-47` | LauncherWindow 渲染模式名/文件夹名用 draw_text（仅 ASCII）——中文 UTF-8 全部字节 >0x7E 被跳过，桌面只显示编号无名字 | 修：两处 draw_text → draw_text_utf8（字库已 TF 化）【待落盘】 |
| 54 | `kernel/kernel.cpp:209-214` | backlight_init 失败会 MEMORIA_CHECK 中止 boot，此时 WiFi/BLE/调度器已起，中止成本高 | 修（低险）：keyboard 三件套失败降级日志不中止（同 SD 策略）【待落盘】 |
| 55 | `modes/ble_manager.cpp:123-144` | 广播启动放 NimBLE host task 里直接调 ble_gap_adv_start 且不查返回值——host 未 sync 时静默失败 → BLE 永远不广播 | W7 修：挂 ble_hs_cfg.sync_cb 里 adv_start（需真机验证，本轮不动） |
| 56 | `modes/mode_manager.cpp:71-76` | enter() 先 pop+reset 再 push——对象析构与 render_all 遍历重叠即 UAF | ✅已随 X1 闭环（enter/back_to_menu 只在主循环 pump 内执行） |

### 味

| # | 位置 | 问题 | 状态 |
|---|------|------|------|
| 57 | `kernel/event_bus.hpp:31-34` | Event 默认 type=JoystickNav(0)，漏设 type 的事件被当导航消费 | 修：Event 构造强制传 type |
| 58 | `kernel/kernel.cpp:163-170` | EventBus::subscribe 在 modes boot 之后——boot 阶段依赖摇杆/电量事件会漏收（当前时序碰巧安全） | 修：subscribe 提前到 modes boot 前 |
| 59 | `modes/ime.cpp:164` | 候选条高亮每候选步进 3*FONT_W，实际"空格+数字"只占 2 → 高亮逐位右漂 | ✅已修：改 2*FONT_W |
| 60 | `modes/ime.cpp:45` | s_cand[9][4] 对 4 字节 UTF-8（emoji）越界写 1 字节（pinyin_dict 全 GB2312 3 字节，实际不可达） | W7 顺手改 [5] |
| 61 | `modes/wifi_manager.cpp:104/124/141` | _cb 在 WiFi 事件任务上下文直调——当前无人注册无风险；setup_mode 注册时必须走 EventBus publish 勿直接刷 UI | 记录 |
| 62 | `modes/rtc_clock.cpp:111` | ntp_sync 阻塞等 15s——现由独立任务调用无碍，勿在主任务直调 | 记录 |
| 63 | `window.hpp:37/136` | 注释漂移："3秒" vs 实际 1500ms；"50ms/20Hz" vs 实际 20ms | 改注释 |
| 64 | `window.cpp:126-138` | StatusBar::update 死代码（static 缓存从未被 on_render 读取） | 删 |
| 65 | `window.cpp:306-323` | draw_rect 忽略 thickness、fill_rounded_rect 忽略 radius、fill_hgradient 忽略 c2——API 名与行为不符 | 修实现或改文档 |
| 66 | `drivers/ili9341.cpp:36-54` | _write_cmd/_write_data 不查 polling_transmit 返回值，SPI 失败静默黑屏无日志 | 查返回 + 日志 |
| 67 | `drivers/power.cpp:73` | idle 计时 uint32 ms 49.7 天溢出（礼物机低风险） | 记录 |
| 68 | `drivers/joystick.cpp:41,58-94` | static handle 取地址传任务 + lambda 内 static self，能用但丑 | 记录 |
| 69 | `modes/mode_manager.cpp:54-57/246` | push_top 与 enter/back_to_menu 双重 pop 逻辑重复（绕但自洽）；注释"内容变暗"实际无遮罩 | 记录 |
| 70 | `desktop_manager.cpp:83` | _decode 未校验 cur[1]==':'（数据源自持 NVS，无威胁） | 记录 |

### 干净清单（GLM 背书）

fat_scanner / backlight(WS2812 RMT) / rtc 主体 / wifi 主体 / mode_manager 栈形推演 / keyboard 消抖（20ms 延迟可接受）/ private_fs（对齐/魔数/CRC/erase 全对）/ kernel malloc-free 配对、OTA 线程 new/delete 回退、队列线程安全。

## 八、W5：解码线（豆包-2 审查，2026-10-01 并入）

> 审查人：豆包-2。范围：i2s_audio + helix + minimp3 + mp4_demux + photo/music/video/clock modes + fat_scanner 音频段 + m4a_adts。状态：7 处修复全部落盘，GLM 全绿 build 已编过（i2s fseeko 由 GLM 代改为全局符号），豆包-2 复跑背书进行中。

| # | 位置 | 问题 | 修法/状态 |
|---|------|------|------|
| 71 | `i2s_audio.cpp` AAC 分支 | **AACDecode 返回值是错误码**（成功=ERR_AAC_NONE=0，见 aacdec.c:496），输出样本数在 AACFrameInfo.outputSamps——原代码把返回值当样本数：首帧 n<=0 判失败、循环 n==0 判无输出 → **AAC/M4A 真机播放必无声**（编译期不暴露） | ✅已修：按 fi.outputSamps 取样本数 |
| 72 | `mp4_demux.cpp:287` | _parse_audio_entry 的 channelcount 取字节错位（取到高字节，单声道误判双声道，靠 channels==0→2 兜底掩盖） | ✅已修：`(uint8_t)(rd32(b+24)>>16)` |
| 73 | `mp4_demux.cpp:288` | samplerate 偏移错 +4（读 b+36 落到子 box size 字段，非 44100 的 m4a 错频/变速） | ✅已修：`rd32(b+32)>>16`（16.16 格式） |
| 74 | `mp4_demux.cpp:243` | stco 条目数未记录，stsc 隐含 chunk 数 > stco 时 chunk_offs 越界（损坏文件） | ✅已修：记录 n_co + 循环前置检查 |
| 75 | `i2s_audio.cpp:89` | mp3_seek_cb `(long)pos` 强转 32 位（>2GB 文件 seek 溢出） | ✅已修：全局 `fseeko` + off_t（GLM 代改 std::fseeko→fseeko，newlib 无 std:: 符号） |
| 76 | `i2s_audio.cpp` MP4 读取 | MP4 短读加固（读取不足预期即判失败而非吞数据） | ✅已修 |
| 77 | minimp3 调用细节 | NO_STDIO + 32KB IO 缓冲（PSRAM）/ callback IO 语义 / 首帧探测 hz/channels / mp3dec_ex_read(CHUNK/2) 2048 样本 / s_ctx.io 全局常驻无覆盖窗口 / first_pcm 落 BSS 不占栈 | ✅干净 |

### W5 其余（备案/记录）

- i2s_audio AAC 分支除上述外干净；music_mode 枚举/切歌/音量干净；m4a_make_adts ADTS 头/采样率索引表干净；fat_scanner guess_type 四扩展名/隐藏文件跳过/stat 校验/audio 目录映射干净。
- 模拟器无声（AAC/MP3 模拟不在本轮）；真机烧录后 m4a 播放实测（demo 音频素材待用户拷 TF）。

## 九、W7 收口状态（2026-10-01）

- [x] W4 清单并入（§七，首批/二批/三批/尾批 4 批全量）
- [x] W5 清单并入（§八）
- [x] W6 修复 5 处（#10/#11/#12/#15/#16）落盘，完整 build 背书（bin 0x1d9270 12:48）
- [x] W6b 修复 7 处（#31/#32/#35/#36/#39/#40/#43）落盘，逻辑测试 25/25，完整 build 背书（bin 0x1d9270）
- [x] 模式层 4 炸（#5/#6/#7/#8：LINE/CIRCLE/自检/记事）——用户拍板改派豆包-1，已修 + build 全绿背书（bin 0x1d9270，豆包-2 同 bin 互证）
- [x] 更新窗口行为项（#37 窗口用配置值 / #38 到点 reconnect()）——已修 + build 全绿背书（bin 0x1d9270）
- [x] 用户决策项汇总 → `docs/W7-DECISIONS.md`（#3/#4/#8/#13 已拍板已修，其余待拍）
- [x] 豆包-1 完整 build 接力验证（bin 0x1d9270 全绿，与豆包-2 互证）
- [x] W6/W6b 全量 38 条导入 ISSUE-BOARD（#32~#69，2026-10-01 12:48）
- [ ] 真机验证（烧录/上电/实测）：录音 4 项、中文显示、BLE 广播、背光恢复、m4a 播放


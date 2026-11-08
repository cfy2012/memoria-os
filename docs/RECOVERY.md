# 砖了怎么救

从"屏幕黑了"到"连下载模式都进不去"，四种情况，四种处理。**先判断是哪一级，别急着擦 flash**——`erase-flash` 会连 NVS 一起清掉，WiFi 和 OTA 地址全得重配。

串口一律 **115200**，接上再复位，看第一行输出。

## 一、先分级

| 现象 | 级别 | 要动的东西 |
|---|---|---|
| 能进桌面，只是某个功能崩 | 软件层 | 不用救砖，OTA 重装或改代码 |
| 屏幕黑/花屏，但串口有 `ESP-ROM:esp32s3` 之后的输出 | **app 层** | 先等自动回滚，不行再擦 `otadata` |
| 串口有 `ESP-ROM` 但之后没了，或反复重启 | **bootloader/app 层** | 整片重烧 |
| 串口一个字都没有 | **供电/bootloader** | 查电源 → 整片重烧 → 换核心板 |
| 连下载模式都进不去（电脑认不到 `USB JTAG/serial` 或 COM 口） | **硬件层** | 换通道（见第六节） |

判断 app 层最直接的一条：串口里搜 `OTA: slot`。

- `OTA: slot ota_0 state=` → 起来了，跑的是 A 槽
- `OTA: slot ota_0 pending verify, hold stamp 30 s` → 新固件第一次起来，还在观察期（1.6.7 起）
- `OTA: slot ota_0 verified (...), rollback cancelled` → 观察期满盖章通过
- 反复出现 `Loading app from ...` 但永远到不了 `========== Boot complete ==========` → app 初始化挂在中间

## 二、app 层挂了：第一件事是什么都别做，断电重启一次

`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` 的语义是：新固件第一次启动处于 `PENDING_VERIFY`，程序必须主动调 `esp_ota_mark_app_valid_cancel_rollback()` 才算"我活了"。**没有盖章就复位，bootloader 会把这一槽标成失败并回退另一个槽**。不需要连按三次复位，一次就够。

盖章点（1.6.7 起）在**进桌面并稳定运行 30 秒之后**，由 `kernel.cpp` 主循环触发，不再放在 boot 末尾。所以：

- 屏幕初始化失败 → 不盖章 → 复位自动回退，屏幕就回来了
- 字库文件缺失 → 不盖章 → 同上
- 进桌面 30 秒内 panic / 看门狗 / 掉电 → 不盖章 → 复位自动回退
- 观察期内主动关机 → `power_off_now()` **先盖章再关机**，算作认可当前固件，不会被回退
- 跑满 30 秒之后才出的 bug → **已经盖章了，不会再回滚**，往下走

断电重启（拔电池或长按电源键关机再开）之后，串口里会看到它从另一个槽启动，`OTA: slot ota_1 ...`。

**身份不符不会走到这一步**：1.6.7 起，OTA 下载完成后会读新槽的 `app_desc`，项目名必须是 `memoria_os`、版本不低于当前，否则直接把启动槽切回原槽并返回 4，根本不重启（串口出现 `OTA rejected, keep booting ota_0`）。

**内容不符更不会走到这一步**：1.6.9 起，下载时边收边算 sha256。HTTP 状态非 200、`Content-Length` 与实际字节数不符、sha256 与清单不符，任一命中都在写 `otadata` 之前 `esp_ota_abort`，串口分别出现 `unexpected http status` / `size mismatch` / `sha256 mismatch`，返回码 1 / 3 / 6。此时 otadata 未改，重启仍是原固件，直接重试即可。

## 三、盖章之后不想再要这个固件：擦 `otadata`

`otadata` 两个块记录"下次从哪个槽起"。擦掉它，bootloader 找不到有效记录时会按默认走——**有 `factory` 起 `factory`，没有 `factory` 就起 `ota_0`**。我们是后者，所以擦 `otadata` 等于强制回 A 槽，而且不动任何固件内容。

```
python -m esptool -p COMx erase-region 0x10000 0x2000
```

`esptool v5` 用连字符；老的 `esptool.py` 是下划线 `erase_region`、参数顺序同样是 `ADDRESS SIZE`。参数写反会把长度当起点，别抄错。

擦完直接复位，不用重烧任何 bin。这是**最便宜的急救动作**，安全，不会让机器更坏。

## 四、两槽都坏了 / 反复回滚 / 串口没有 boot 日志：整片重烧

第一步永远是整片擦，不是直接烧：

```powershell
idf.py -p COMx erase-flash
idf.py -p COMx flash monitor
```

为什么必须 `erase-flash`：分区表变了（`ota_0` 从 0x300000 挪到 **0x20000**），旧布局残留的 `otadata`、旧 `factory` 内容会让 bootloader 混乱。整片擦掉重来最干净。

不想用命令行，用官方 Flash Download Tool（Windows GUI）也行，三行地址必须按新布局填：

| 文件 | 烧录地址 |
|---|---|
| `build/bootloader/bootloader.bin` | **0x0** |
| `build/partition_table/partition-table.bin` | **0x8000** |
| `build/memoria_os.bin` | **0x20000** |

手写 esptool 命令时同一个道理：

```
python -m esptool -p COMx -b 460800 write-flash \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x20000 build/memoria_os.bin
```

**app 是 0x20000，不是 0x10000。** 0x10000 现在是 `otadata`，把 app 烧进去既起不来，还会把槽位记录弄坏——这是这次改分区表之后最容易踩的一脚。

## 五、只想重置某一部分

| 擦什么 | 地址 | 长度 | 丢了什么 | 什么时候用 |
|---|---|---|---|---|
| `otadata` | `0x010000` | `0x002000` | 下次从哪槽起 | 强制回 ota_0，最常用 |
| `nvs` | `0x009000` | `0x006000` | WiFi、桌面布局、OTA 地址（**卡在的话开机会从 `sys/nvs.bin` 回灌**） | 联网配不上、OTA 源填错 |
| `mem_priv` | `0xC20000` | `0x300000` | store 应用包、存档（卡可用时这份已不再被读写） | 应用层数据坏了 |
| `ota_1` | `0x310000` | `0x2F0000` | B 槽固件 | 清掉一个已知坏掉的备槽 |
| 全片 | `0x0` | `0x1000000` | 上面全部（卡上 `sys/` 仍保留） | 变砖救援 |

**1.6.5 起用户数据在卡上**：真正的权威副本是 TF 卡 `/mem_fat/sys/`（`nvs.bin` + `privfs.bin`）。所以想真正重置，先删卡上的 `sys/` 再擦 flash，否则开机 `vault::prepare()` 会把 `nvs.bin` 重新灌回 NVS，看起来"擦了没用"。`sys/` 目录的来龙去脉见 [`STORAGE.md`](STORAGE.md)。

命令模板：

```
python -m esptool -p COMx erase-region 0x009000 0x6000
```

长度必须照抄表里的值。擦完不写 `otadata` 是正常的，bootloader 会自己按默认槽起。

TF 卡上的东西（`/mem_fat`、`/sd_ext`）不在 flash 里，擦 flash 不影响。卡里缺字库会表现为"能开机但中文全是方框"，那是另一回事，把 `sdcard/` 的内容重新拷回卡根目录。

## 六、进不去下载模式

核心板自带 BOOT 键，不用外焊：

1. 按住 **BOOT**（GPIO0）
2. 按一下 **RST**，或插拔一次 USB 供电
3. 松开 BOOT

正常的话电脑会认到一个串口。

接哪条线，仓库里两份文档口径不一致（`docs/WIRING.md` 说核心板 USB-C 直插烧录；`docs/PARTS-DIMENSIONS.md` 说 USB 被 OTG 占用、烧录走 UART 43/44 排针）。实测为准：

- 插 USB-C 后电脑出现 `USB JTAG/serial debug unit` 或 COM 口 → 走这条
- 电脑没反应 → 用 43/44 两根排针接外置 USB-TTL（TX→RX 交叉，GND 共地），再按 BOOT + 复位

两条都不行，先怀疑供电：电池电压低于 3.4V、TP4056 没出 5V、核心板 3V3 实测没电压，都会表现为"完全没反应"。这种先量电压，不要先怀疑固件。

## 七、救完确认这几条

1. 串口开头出现 `OTA: slot ota_0 state=` 或 `verified`
2. 串口末尾出现 `========== Boot complete ==========`
3. `idf.py partition-table` 读出七行，`ota_0` 在 `0x20000`、`ota_1` 在 `0x310000`
4. 桌面能进，中文正常（说明字库读到了）
5. shell 里 `ver` 或关于页显示的版本号是你要的那个

## 八、救不回来的情况

- **flash 本身坏了**（擦除报错、`flash-id` 读不出、写入校验反复失败）：这块板的核心板是背焊在洞洞板上的，换核心板要拆焊。没有热风枪就是重做一块板。
- **供电层坏了**（TP4056 无输出、3V3 短路）：固件救不了，先修电源。
- **只是想回到旧版本**（比如 1.6.3）：不能只烧旧 bin——分区表在 `0x8000` 属于 bootloader 段，旧表里 app 在 `0x10000`，新 bootloader 会去 `0x20000` 找，直接起不来。必须用旧源码在独立目录（`git worktree`）整套重编译，再 `erase-flash` + `flash`。

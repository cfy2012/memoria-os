# 用户数据存储

一句话：**用户数据全在 TF 卡上，flash 里只有系统本体。**

`/mem_fat/sys/` 是唯一的数据根目录，里面就两个文件：

| 文件 | 装什么 |
|---|---|
| `sys/nvs.bin` | 七个命名空间的 KV 镜像：`settings`（系统设置）、`desktop`（桌面布局）、`package`（应用商店索引）、`wifi`（SSID/密码）、`ota`（固件源地址）、`pwr`（关机状态）、`net`（BLE 开关与名字） |
| `sys/privfs.bin` | 私有文件系统整个镜像：应用包、存档、笔记、照片、录音 |

flash 侧只剩 bootloader、分区表、`otadata`、`ota_0` / `ota_1` 两个 app 槽，外加一个最小的 `nvs` 分区。

## 为什么 flash 里还留着 nvs 分区

ESP-IDF 的 WiFi 驱动、PHY 校准、部分组件直接用默认 `nvs` 分区，整体删掉系统起不来。所以口径是：**业务数据全搬卡，NVS 分区本体保留**——flash 上会有一份冗余，但卡才是权威源，开机时以卡为准（见下面"回灌"）。

`mem_priv`（3 MB）只在没有卡时兜底用，卡上 `privfs.bin` 一旦建立，flash 那份就不再被读写。

## 写盘时序

| 时机 | 动作 |
|---|---|
| 开机第 5 步之后（TF 挂载完，PrivateFs 初始化前） | `vault::prepare()`：建 `sys/`、探可写、必要时从卡回灌 NVS、必要时立刻镜像一次 |
| 开机第 13 步 | `PrivateFs::init()`：先开 `sys/privfs.bin`；文件不存在则新建并把 flash `mem_priv` 整体搬过来（一次性） |
| 运行中（主循环每秒查一次） | 距上次镜像超过 **60 秒** 就存一次 `nvs.bin` |
| 关机（侧键长按 / 低电量保命） | 关外设之前先 `mirror_save()`，这是最后一次可靠写入 |
| shell `vault save` | 立刻存一次 |

镜像写法是 **临时文件 + rename**：先写 `nvs.tmp`，关文件，删旧 `nvs.bin`，再改名。掉电最坏留下一个半截 `nvs.tmp`，原来的 `nvs.bin` 还在。

## 回灌：NVS 被擦空时从卡恢复

`vault::prepare()` 会检查 flash 上七个命名空间是否**全空**。全空且卡上有 `nvs.bin`，就按镜像逐条写回 NVS 并 commit。

这覆盖了最常见的两种"东西没了"：

- 擦过 `nvs` 分区（0x9000 / 0x6000）之后重启
- 换了一块新核心板，只烧了固件、没带数据

反过来，flash 里有数据、卡上还没有镜像 → 说明是第一次上卡，直接把当前 NVS 存一份到卡。

## 首次上卡的迁移

`sys/privfs.bin` 不存在时按 `w+b` 新建，随即调用 `_migrate_from_flash()`：

1. 读 flash `mem_priv` 的 Superblock，magic 不对（空分区）就跳过；
2. 对就按 4096 字节分块把整个 3 MB 拷进卡文件，串口打 `migrate: moving 3072 KB from flash to card (one time)`。

迁移只在文件新建那一次发生。之后 flash 那份保持原样不删——卡丢了还能退回老数据。

## 卡不在时会发生什么

全部降级回 flash，系统照常开机、照常进桌面：

| 层 | 降级后的行为 |
|---|---|
| NVS | 读写落在 `nvs` 分区，跟改造前一样 |
| PrivateFs | 落在 `mem_priv`（0xC20000 / 0x300000），跟改造前一样 |
| 桌面 | `desktop_manager::load()` 拿不到布局就按注册顺序铺满应用 |
| 日志 | `Vault: card not writable at /mem_fat/sys -> fallback to flash` / `Vault: TF card missing, user data stays in flash` |

**注意**：卡和 flash 是两份独立数据。卡在的时候改设置只进卡，拔卡后再改只进 flash，插回去不会自动合并——以卡为准（`prepare` 只在 flash 全空时才回灌，不会拿 flash 覆盖卡）。

## 掉电风险（已知取舍）

PrivateFs 是自研简易文件系统（Superblock + 1024 条 Entry + 只扩不回收的数据堆），本身没有掉电事务保证。搬到 FAT 卡以后：

- 写数据过程中掉电 → 卡上文件可能半截，Superblock 里的 `next_free_offset` 可能超前于实际写入；
- NVS 镜像有临时文件保护，最坏丢最近 60 秒内的设置变更；
- 桌面拖拽、WiFi 改密码这类操作，关机前会强制存一次；直接拔电源最多丢 1 分钟。

要更严的掉电保护得在 PrivateFs 层加日志式提交，这不在本轮范围内。

## 换卡、备份、重置

| 想做的事 | 做法 |
|---|---|
| 换一张卡 | 把旧卡 `sys/` 整个目录拷到新卡根目录下，插上即可 |
| 备份用户数据 | 拷贝 `sys/` 两个文件；`nvs.bin` 是人可读的二进制 KV，`privfs.bin` 是完整 FS 镜像 |
| 重置用户数据 | 删掉 `sys/` 整个目录，重启后自动重建（NVS 会回到 flash 里的那份，或空） |
| 只重置网络 | shell `wifi` 相关命令，或删 `sys/nvs.bin` 后重启（会从 flash 回灌，所以要先擦 `nvs`） |
| 强行把当前 NVS 写进卡 | shell `vault save` |

卡本身格式化请用系统里的 `sd format`（会清整张卡）。

## 与 OTA 的关系

OTA 全程不碰 `sys/`：

- `esp_https_ota_*` 只写另一个 app 槽（`ota_0` ↔ `ota_1`）；
- 唯一的 NVS 交互是 `ota` 命名空间里的 `firmware_url` 一个 key，走镜像，不直接落 flash；
- 换固件、回滚、盖章都不影响卡上数据。

所以升级完设置还在，这条从 1.6.4 起就成立；1.6.5 之后即使整片 `erase-flash`，卡上 `sys/` 也还在，开机自动回灌。

## 排查

| 现象 | 看什么 |
|---|---|
| 设置改了重启就没了 | shell `vault` 看后端是不是 `card`；不是就查 TF 挂载 |
| 卡上没生成 `sys/` | 串口 `Vault: card not writable` → 卡只读 / FAT 异常，`sd format` 重来 |
| 桌面布局丢了 | 串口看 `restored N namespaces from card mirror`；没有说明 `nvs.bin` 缺失或 header 坏 |
| 应用包都没了 | `sys/privfs.bin` 没建起来，看 `migrate:` 日志 |
| 存储满了 | 私有 FS 只扩不回收，`sys/privfs.bin` 达到 3 MB 上限后 `create` 会报 `data heap exhausted` |

## 分区表现状与可回收空间

| 分区 | 偏移 / 大小 | 现在的作用 |
|---|---|---|
| `nvs` | 0x009000 / 0x006000 | IDF 依赖 + 无卡时兜底 |
| `phy_init` | 0x00F000 / 0x001000 | 射频校准 |
| `otadata` | 0x010000 / 0x002000 | A/B 槽选择 |
| `ota_0` | 0x020000 / 0x2F0000 | app 槽 0 |
| `ota_1` | 0x310000 / 0x2F0000 | app 槽 1 |
| `mem_fat` | 0x600000 / 0x620000 | **死区**，建库起没有任何代码挂载 |
| `mem_priv` | 0xC20000 / 0x300000 | 无卡时兜底 |

`mem_fat` 6.4 MB 与卡上数据不冲突（卡是外置的），这块 flash 目前空着；`mem_priv` 在卡可用时也不再被写。两者要不要收缩，等板子实测之后再动——改分区表就得整片重烧一次。

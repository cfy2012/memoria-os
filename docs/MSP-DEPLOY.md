# MSP-DEPLOY 掌机应用包部署三步

> 掌机 store 应用的包从打包到装进掌机的完整流程。打包工具：`tools/msp-packer/`（GUI，双击即用，[Release 下载](https://github.com/cfy2012/memoria-os/releases/tag/msp-packer-v1.0.0)）。

## 前置条件

| 项 | 要求 |
|---|---|
| 打包器 | `msp-packer.exe`（v1.0.0+），Windows 零依赖 |
| 服务器 | 任意可公网 HTTP 访问的空间（虚拟主机 / GitHub 仓库 / 自有服务器均可） |
| 掌机 | Memoria OS v1.2.0+，WiFi 已连网（系统设置里连） |
| TF 卡 | 可读写，store 数据落在 `/mem_fat/` 分区 |

---

## 第一步：打包

### 1a. 打 .msp 安装包（音频 media 包）

1. 双击 `msp-packer.exe`，切到「**.msp 安装包**」页签
2. 填**包名**（规则：只允许 `A-Za-z0-9_.-`，固件白名单，如 `demo_bell`）
3. 填**版本号**（整数，如 `1`）
4. 选** payload 文件**（音频包选 `.wav` / `.mp3` / `.m4a`，上限 1MB）
5. 确认**输出路径**（默认与 payload 同目录同名 `.msp`）
6. 点「**打 包**」——底部日志显示 `msp ok: … crc=0x…` 即成功（工具已自动回读自校验）

### 1b. 生成 store 清单（manifest.json）

1. 切到「**store 清单**」页签
2. 填**源地址 base_url**（即第二步部署后的目录 URL，如 `https://your.host/pkg`）
3. 点「**添加**」选入要发布的文件——`.bas` 脚本、`.msp` 音频包都可以混选
   - 工具自动识别：`.wav/.mp3/.m4a` 标记 `"type":"media"`；`.bas` 不带 type（脚本走裸文件直装）
   - `size` / `crc32` / `url` 全部自动算好
4. 点「**生成清单**」→ 得到 `manifest.json`

> 脚本包**不需要**打成 .msp：store 装 `.bas` 是裸文件下载直写 `/mem_fat/scripts/`。MSPACK 只在 media 音频包路径使用。

## 第二步：上传服务器

1. 准备目录，最终结构：

   ```
   /pkg/
   ├── manifest.json        ← 第 1b 步生成
   ├── chat.bas             ← 脚本包（裸文件）
   ├── demo_bell.msp        ← 音频包（第 1a 步产物）
   └── …
   ```

2. 把整个目录上传到任一 HTTP 服务：
   - **虚拟主机**：FTP 上传到网站目录（如 `/pkg/`）
   - **GitHub 仓库**：推到仓库后，文件走 `raw`/CDN 链接
   - **自建**：任意静态文件服务
3. 浏览器访问 `https://your.host/pkg/manifest.json` 能看到 JSON 内容（不是 404）即部署成功

> **国内注意**：不要直接用 `raw.githubusercontent.com` 做源（国内直连不通），用虚拟主机或已套 CDN 的地址。

## 第三步：掌机安装

1. TF 卡插电脑，在 `/mem_fat/` 分区根目录建（或编辑）文本文件 **`store_url.txt`**，内容一行：

   ```
   https://your.host/pkg/manifest.json
   ```

2. TF 卡插回掌机，开机，**连 WiFi**（系统设置 → WiFi 列表点选）
3. 桌面进入「**store**」应用 → F2 刷新列表 → 选中条目 → 安装
   - 脚本包装进 `/mem_fat/scripts/`，PRGM/BASIC 里直接跑
   - 音频包验头 + 双 CRC 后解包存 `/mem_fat/audio/`
4. 安装完成，应用列表里出现新条目

---

## 常见问题

| 现象 | 原因 | 处置 |
|---|---|---|
| store 列表为空 / 刷新失败 | `store_url.txt` 缺失或地址 404 | 核对 TF 卡文件与浏览器直访 |
| 提示 CRC 不符 | 上传后文件被改动 / 部分传输 | 重新上传，浏览器核对文件字节数与清单 `size` 一致 |
| 打包器报"包名只允许…" | 包名带中文/空格/非法字符 | 改为 `A-Za-z0-9_.-` |
| 音频包装上但播不了 | manifest 里没标 `"type":"media"` | 用打包器重生成清单（自动标记） |
| payload 超限 | 单文件 > 1MB | 压缩音频或分段，固件 verify 上限 1MB |

## 格式速查

- **MSPACK 头**（50 字节，`#pragma pack(1)`，小端）：`magic[6]="MSPACK"` + `version u32` + `name[32]`（NUL 结尾）+ `payload_size u32` + `crc32 u32`
- **CRC32**：zlib 标准（初值/末异或 `0xFFFFFFFF`，反射多项式 `0xEDB88320`），与固件 `crc32_compute` 逐位一致
- **manifest 条目**：`{"name","version","size","crc","url","type"?}`，格式权威定义见 `components/memoria_package/package_manager.cpp`

# Memoria MSP 打包器

Memoria OS 应用包图形化打包工具：打 `.msp`（MSPACK）安装包、生成 store 应用清单 `manifest.json`。纯 Python 标准库（tkinter），无需安装任何依赖。

## 运行

```
D:\py\python.exe msp_packer.py        # 图形界面（双击亦可）
msp_packer.py --selftest              # 无界面自测（打包→回读→CRC 逐位验证）
```

## .msp 格式（MSPACK）

对齐固件 `components/memoria_package/package_format.cpp`，`#pragma pack(1)` 共 50 字节头 + payload：

| 偏移 | 字段 | 类型 | 说明 |
|---|---|---|---|
| 0 | magic | char[6] | 固定 `MSPACK` |
| 6 | version | uint32 LE | 包版本号 |
| 10 | name | char[32] | 包名，NUL 结尾（白名单 `A-Za-z0-9_.-`） |
| 42 | payload_size | uint32 LE | payload 字节数（上限 1MB） |
| 46 | crc32 | uint32 LE | payload 的 CRC32 |

CRC32 与固件 `crc32_compute` 逐位一致：初值/末异或 `0xFFFFFFFF`、反射多项式 `0xEDB88320`（即 zlib 标准 CRC32）。

**用途**：固件当前只有 `type:"media"` 音频包（.wav/.mp3/.m4a）走 MSPACK 路径（下载 → 验头 → 验 CRC → 解 payload 存 `/mem_fat/audio/`）。

## store 清单（manifest.json）

对齐 `components/memoria_package/package_manager.cpp` 清单格式：

```json
[
  {"name": "demo_bell.msp", "version": "2026.10.06", "size": 1116,
   "crc": 1442251494, "url": "https://host/pkg/demo_bell.msp", "type": "media"}
]
```

- 脚本包（.bas）不带 `type` 字段：store 直接下载裸文件写入 `/mem_fat/scripts/`，CRC 可选校验
- 音频包必须 `"type":"media"`，固件才走 MSPACK 解包

## 部署到掌机

1. `manifest.json` + 包文件放到任意 HTTP 服务器或 GitHub 仓库
2. 掌机 TF 卡写入 `/mem_fat/store_url.txt`（一行 manifest 地址）——默认源指向 raw.githubusercontent.com，国内需重定向
3. 掌机连 WiFi → store 应用 → 刷新列表 → 安装

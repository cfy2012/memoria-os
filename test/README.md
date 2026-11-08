# test

没有硬件时唯一能自动跑的东西。纯 Python 标准库，不依赖 ESP-IDF、不依赖板子。

```
python test/run_all.py
```

退出码 0 = 全过，非 0 = 有失败项并逐条打印原因。GitHub Actions 的 `verify` 工作流每次 push 都跑一遍，编译那份也跑。

## 现在查什么

| 模块 | 查的事 | 抓的是哪类事故 |
|---|---|---|
| `checks/partitions.py` | 分区表对齐、重叠、越界、`otadata` 是否存在、`ota_0`/`ota_1` 大小是否对称、槽能否装下当前 bin、占用是否超 85% | 分区表改错把 bootloader 顶掉、bin 撑爆槽、烧上去起不来 |
| `checks/versions.py` | `CMakeLists.txt` / `system_info.h` / `version.json` 三处版本号是否一致；`channel` 与奇偶规则是否相符 | 改了版本号漏改一处，OTA 报"已是最新"或者升了个号不对的包 |
| `checks/firmware.py` | `version.json` 字段齐全、`url` 文件名与版本号对应、`major` 必须是字符串、bin 是否存在 | OTA 拉一个 404 的链接；`major` 写成布尔导致解析异常 |
| `checks/includes.py` | 组件里 `#include <esp_ota_ops.h>` / `esp_http_client.h` / `nvs_flash.h` 而 CMakeLists 没声明对应 `REQUIRES` | 就是 1.6.4 踩过的那个 `EXIT=2`：`esp_ota_ops.h: No such file or directory` |

## 为什么不做 C++ 单元测试

本机没有 cmake，也没法在本地验证“我编的这个 host 测试真的能跑”，那种测试提交上去只是摆设。等哪天真有了编译器，再加 `test/native/`，优先搬 Basic 解释器和路径回退逻辑。

## 加新检查

在 `test/checks/` 下加一个模块，暴露 `run(root) -> list[str]`（返回空列表即通过），然后把名字加进 `run_all.py` 的 `CHECKS`。

写完必须验证它能抓到错：故意把被检查的东西改坏一次，看它有没有变成 FAIL。只验证“能通过”等于没验证。

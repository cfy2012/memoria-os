# Memoria OS · 健康度与软件生态评估报告

> 生成：2026-10-06 · 评估人：豆包-2 · 数据来源：README / PROJECT-STATUS / 桥 / 代码统计 / examples
> 状态：快照评估，供项目决策参考

---

## 一、健康度总评：7.3 / 10

**架构健康 · 流程健康 · 验证滞后 · 债台不高但有四笔挂账**

| 维度 | 评分 | 依据 |
|------|------|------|
| 代码组织 | 9.0 | 8 组件各司其职；自有 ~2.3 万行，第三方解码库 ~1.6 万行（helix/minimp3/tjpgd，合理 vendor）；drivers_config.hpp 唯一引脚来源 |
| 构建稳定 | 7.0 | 编译全绿，bin 1.85MB / 3MB 分区（余 37%）；但真机三复位源未清零 |
| 文档完备 | 9.0 | 20+ 份：README/BOM/WIRING/教材 0x 化/语言手册/OOP 规划/回归清单/PERF-TUNING |
| 协作流程 | 9.0 | 桥协议 + 看板 + 三方分工 + "动共享文件先报桥"纪律 |
| 真机验证 | 4.0 | BOD 欠压 / 看门狗 / StoreProhibited panic 均未清零；模拟器全绿 ≠ 上板可用，最大短板 |
| 技术债 | 6.0 | 四笔挂账（见下） |

### 四笔技术债（按优先级）

1. **GB2312 字库 MISSING**：boot log `GB2312 font: MISSING, CJK fallback`——TF 卡未拷 `font/gb2312_16.bin`，真机中文降级。修复 = 拷卡，5 分钟。
2. **mem_fat 6MB 分区未挂载**：boot log `SDCARD: FAT mount failed: ESP_ERR_INVALID_ARG`——板载 FAT 分区定义了没挂上。
3. **安卓式电源管理未实现**：方案定版（GPIO2 长按/深睡/EXT0 唤醒），固件等窗口。
4. **SD 大卡驱动未开工**：SDMMC 39/40/41 方案已立，等窗口。

### 代码结构备忘

- memoria_window 单模块 7868 行（窗口+字库渲染）偏胖，后期可拆。
- 组件依赖清晰：kernel 调 window/drivers，package 独立，无循环依赖迹象。

---

## 二、软件生态评估：基建 80 分，内容 10 分

**结论：平台先行，内容未至——有商店没商品，有语言没作品。**

| 层 | 成熟度 | 现状 |
|----|--------|------|
| 平台层 | 8/10 | .msp 包格式 + CRC + manifest 多源 + 断点续传 + OTA + 模拟器 + chat 服务器 + 串口 shell，商店基建完整 |
| 语言层 | 7/10 | BASIC 手册 + 0x 教材 + 设备端编辑器 + .ms 脚本引擎；语法批次（mod/数组/文件流/OOP）落地后 → 9 |
| 内容层 | 1/10 | 上架仅 2 个 demo（demo-script + audio-test）；设备端脚本仅 chat.bas；.m4a 测试素材缺失 |

### 生态策略：寄希望于人民，但先备好鱼塘

开源生态的正确路线 = 平台先行、内容众筹（Linux 先例）。但贡献者"慕强而来、怕麻烦而去"，须备好三样：

1. **门槛够低**：BASIC 打磨到"十分钟写第一个程序"——语法批次 + 教材 0x 化是主力，落地即够格。
2. **示例够馋**：好示例是最好的广告。把 chat.bas 包装成第一个"真应用"上架；示例从 2 个补到 ~10 个（游戏/工具/聊天）。
3. **入口够顺**：README 一条龙"怎么写应用 → 打包 → 上架 → 进商店"，别让贡献者考古。

> 现实预期：GitHub 99% 项目 0 贡献者。行动项 = 修鱼塘（开发者体验）+ 挂鱼饵（示例/视频），不是空等。

---

## 三、优先级建议（合并 PERF-TUNING 执行顺序）

| # | 事项 | 归属 | 卡点 |
|---|------|------|------|
| 1 | 真机三关：BOD / 看门狗 / panic 定位修复 | GLM（build）+ 用户（实测） | 编译窗口 + 烧录 |
| 2 | 字库拷卡落地（TF 卡 `font/gb2312_16.bin`） | 用户 | 无，5 分钟 |
| 3 | 开机电流排班（背光爬坡 / WiFi 延迟 / TF 错峰） | GLM | 编译窗口 |
| 4 | 电源管理固件 + SD 大卡驱动 | 豆包-2 认领 | 编译窗口 |
| 5 | 商店上架真应用（chat.bas 首发）+ 示例扩充 | 豆包-1/2 | 语法批次落地后 |
| 6 | PERF-TUNING P0~P4（240MHz/双核/内存分层/LCD 120/SIMD） | GLM + 豆包-2 | 编译窗口 |

---

## 附：评估数据底账

- 自有代码行数（不含 vendor/build）：kernel 3342+3990(modes) / window 7868 / drivers 2209 / basic 1240 / package 785 / script 911 / fs 439 / shell 280 / main 33 ≈ **2.3 万行**
- 第三方：helix_aac 11917 / minimp3 3085 / tjpgd 1087 ≈ **1.6 万行**
- 文档：docs/ 20 份 + README + HANDOFF + MEMORY-SNAPSHOT
- 应用：examples/app-samples（demo-script.msp）+ audio-samples（audio-test.msp）；sdcard/scripts/chat.bas

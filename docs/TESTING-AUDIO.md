# Memoria OS — 音频播放实测清单（真机）

> 配套固件：`build/memoria_os.bin`（0x212850，MP3/WAV/M4A 三路全绿）
> 配套素材：`sample/sample_test.mp3`（116KB，pygame 测试音频，MP3 通路）

## 0. 前置

1. 烧录：`idf.py -p <PORT> flash monitor`（或 esptool 直接写 0x0/0x8000/0x10000 三件套）
2. TF 卡：项目 `sdcard/` 目录**整目录拷到卡根**（含 `font/gb2312_16.bin`，GLM 线 P1.6）
3. 音频文件放 TF 卡 `/mem_fat/audio/`：
   - `sample_test.mp3`（必测，MP3 通路）
   - 任意 .m4a / .aac（用户提供；M4A 容器 + AAC 裸流均支持）

## 1. 实测步骤

| # | 操作 | 预期 |
|---|------|------|
| 1 | 开机进 MUSIC 模式 | 列出 `/mem_fat/audio` 下所有 .wav/.mp3/.m4a/.aac，中文界面正常（字库 TF 化后需方框为缺字占位，正常） |
| 2 | 选中 `sample_test.mp3` 播放 | 3~10 秒内出声（minimp3 软解），无爆音、无卡顿 |
| 3 | 方向键切歌（_next/_prev） | 上一首停止、下一首开始，无残留播放、无内存泄漏迹象（连续切 20 次不崩） |
| 4 | 音量 +/−（步进 10） | 音量渐变，0 静音、100 满量程（MAX98357A 3W 功放） |
| 5 | 播放中按停止键 | 立即无声，任务退出（`I2sAudio::stop()` 150ms 退避） |
| 6 | 退出 MUSIC 模式再进入 | 重新枚举正常，无"正在播放"假状态 |
| 7 | 播放 .m4a（若已提供） | Helix AAC 软解出声，双声道不丢数据（已按 `fi.outputSamps` 修复） |

## 2. 已知缺陷排查

| 现象 | 排查 |
|------|------|
| m4a 无声 | ① 文件是否真 AAC-in-MP4（非 AC3/其他）；② 采样率 8k~96k、声道 1/2（超范围代码直接拒播并打 log）；③ 首帧 log `aac first frame decode rc=<n>`：`rc<0` 为解析错误，`rc==0` 表示成功（earlephilhower 版返回错误码，0 不是失败） |
| mp3 无声 | 检查 minimp3 初始化 log；文件是否标准 MP3（VBR/CBR 均可） |
| 播放中爆音/破音 | 音量 >80 时 MAX98357A 削波属正常物理现象，调低音量 |
| 切歌后旧歌仍在响 | `I2sAudio::stop()` 未生效 → 检查任务 `playing` 标志与 150ms 退避是否被打断 |

## 3. 串口 log 关键字

- `WAV done. <bytes> bytes` — WAV 播放结束
- `MP3 done` — MP3 播放结束
- `AAC done` — AAC/M4A 播放结束
- `aac decode rc=<n> (skip)` — 单帧解码失败跳过（容忍）
- `aac decode rc=ERR_AAC_INDATA_UNDERFLOW` — 输入不足，视为流结束

## 4. 备注

- 模拟器无声是已知缺口（AAC/MP3 软解不在浏览器模拟范围），音频以真机为准。
- 录音（INMP441 RX）未实现，本轮只测播放。
- demo .m4a 待用户提供（本机外网 https 被环境掐断，下载失败）。

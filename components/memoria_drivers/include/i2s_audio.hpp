/**
 * @file i2s_audio.hpp
 * @brief MAX98357A I2S 功放：WAV/MP3/M4A/AAC 解码播放 + INMP441 数字麦克风录音
 *
 * I2S 使用 STD 标准模式：i2s_new_channel + i2s_channel_init_std_mode
 * 播放：TX 通道（BCLK/WS/DOUT）；录音：RX 通道（BCLK/WS 复用 + MIC_SD_GPIO 数据）
 */

#pragma once

#include "drivers_config.hpp"
#include <esp_err.h>
#include <string>
#include <atomic>
#include <cstdint>

namespace memoria {
namespace drivers {

class I2sAudio {
public:
    static I2sAudio* instance();
    esp_err_t init();

    esp_err_t play_wav(const std::string& path);
    esp_err_t play_audio(const std::string& path);   /* 按后缀分流：.mp3 → 软解，其余按 WAV */
    void      stop();
    bool      is_playing() const { return _playing.load(); }

    /* --- INMP441 数字麦克风录音（I2S RX → TF 卡 WAV 16bit mono） --- */
    esp_err_t rec_start(const std::string& path, uint32_t sample_rate = 44100);
    esp_err_t rec_stop();            /* 回填 WAV 头真实长度，返回时长秒（经 _rec_seconds） */
    bool      is_recording() const { return _recording.load(); }

    /* 录音时长（秒，rec_stop 后有效） */
    std::atomic<uint32_t> _rec_seconds{0};

    /* 音量 0~100，同步更新 atomic 版本供播放线程实时读取 */
    void      set_volume(uint8_t v) { _volume = v; _volume_a.store(v); }
    uint8_t   get_volume() const { return _volume; }
    void      set_mute(bool m) { _muted = m; _muted_a.store(m); }
    bool      is_muted() const { return _muted; }

    /* atomic 成员（供 _playback_task_c 静态任务函数桥接访问） */
    std::atomic<uint8_t> _volume_a{80};
    std::atomic<bool>    _muted_a{false};
    std::atomic<bool>    _playing{false};
    std::atomic<bool>    _recording{false};

private:
    I2sAudio() = default;

    uint8_t _volume = 80;
    bool    _muted = false;
};

} // namespace drivers
} // namespace memoria
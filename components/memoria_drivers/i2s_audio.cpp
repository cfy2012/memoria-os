/**
 * @file i2s_audio.cpp
 * @brief MAX98357A I2S：WAV 直放 + MP3（minimp3）+ M4A/AAC（Helix AAC）软解
 *
 * 播放源分流（play_audio 按扩展名）：
 *   .mp3  → minimp3 流式解码（callback IO，无 mmap 依赖）
 *   .m4a  → MP4 容器 demux（mp4_demux）→ Helix AAC 逐帧解码
 *   .aac  → ADTS 裸流 → Helix AAC 逐帧解码
 *   其他  → WAV（PCM16）解析直放
 * 采样率/声道由源决定，I2S STD 模式按需配置。
 *
 * PlayCtx 必须在文件作用域（或 class 成员），因为静态任务函数要访问。
 */

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_STDIO        /* ESP32 无 mmap，禁用 FILE/mmap 打开路径 */
#define MINIMP3_IO_SIZE (32 * 1024)  /* 流式输入缓冲（默认 128KB 过大），走 PSRAM */
#include "minimp3_ex.h"

#include "i2s_audio.hpp"
#include "mp4_demux.hpp"

extern "C" {
#include "aacdec.h"
#include <driver/i2s_std.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
}

#include <cstdio>
#include <cstring>
#include <cctype>

#include <algorithm>
#include <cmath>

namespace memoria {
namespace drivers {

static const char* TAG = "I2S_AUDIO";

/* 播放完成事件：playback_task 退出时置位，stop() 等待其真退出
 * （替代旧的 150ms 硬等伪同步，防切歌/快停时双任务读写 s_ctx） */
static EventGroupHandle_t s_done_evt = nullptr;
static constexpr EventBits_t PLAY_DONE_BIT = (1 << 0);

I2sAudio* I2sAudio::instance() {
    static I2sAudio inst;
    return &inst;
}

esp_err_t I2sAudio::init() {
    s_done_evt = xEventGroupCreate();
    if (!s_done_evt) { ESP_LOGE(TAG, "play done event group create failed"); return ESP_ERR_NO_MEM; }
    ESP_LOGI(TAG, "MAX98357A OK: BCLK=%d WS=%d DOUT=%d port=%d",
             I2S_BCLK_GPIO, I2S_WS_GPIO, I2S_DOUT_GPIO, I2S_PORT);
    return ESP_OK;
}

/* ---------- 文件作用域 PlayCtx：静态任务函数可见 ---------- */
struct PlayCtx {
    FILE* fp = nullptr;
    uint32_t data_size = 0;        /* WAV: 剩余 PCM 字节 */
    i2s_chan_handle_t tx = nullptr;
    i2s_chan_handle_t rx = nullptr;    /* 常驻 RX（INMP441 常挂，供全双工录音） */
    uint32_t rate = 0;                 /* 当前播放采样率（录音跟随） */
    std::atomic<bool>* playing = nullptr;
    std::atomic<uint8_t>* volume = nullptr;
    std::atomic<bool>* muted = nullptr;

    bool is_mp3 = false;           /* true 走 MP3 解码分支 */
    mp3dec_io_t io{};              /* callback IO（常驻，mp3.io 指向这里） */
    mp3dec_ex_t mp3{};             /* MP3 流式解码器 */
    int16_t first_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME] = {0};  /* 首帧暂存（格式探测） */
    size_t  first_samples = 0;     /* 首帧样本数 */

    bool is_aac = false;           /* true 走 AAC 解码分支 */
    HAACDecoder aac = nullptr;     /* Helix AAC 解码器 */
    M4aDemux* demux = nullptr;     /* .m4a 容器（.aac 裸流时为 nullptr） */
    size_t aac_index = 0;          /* 下一帧序号（MP4 用） */
    uint8_t* frame = nullptr;      /* 单帧输入缓冲（ADTS 头+数据） */
    uint32_t frame_cap = 0;
    uint16_t aac_first_n = 0;      /* 首帧输出样本数（先于 I2S 配置解码得到） */
    int16_t* aac_first_pcm = nullptr;  /* 首帧 PCM 暂存 */
};
static PlayCtx s_ctx;

/* 播放任务（文件级：只经全局 s_ctx 工作，无类状态依赖） */
static void playback_task(void* arg);

/* minimp3 callback IO：用 stdio FILE* 读写 */
static size_t mp3_read_cb(void* buf, size_t size, void* ud) {
    return std::fread(buf, 1, size, static_cast<FILE*>(ud));
}
static int mp3_seek_cb(uint64_t pos, void* ud) {
    /* newlib 交叉工具链无 std::fseeko，用全局 fseeko（POSIX） */
    return fseeko(static_cast<FILE*>(ud), static_cast<off_t>(pos), SEEK_SET);
}

/* WAV 头解析 */
static bool parse_wav(FILE* fp, int& rate, bool& stereo,
                      uint32_t& data_start, uint32_t& data_size) {
    char riff[4] = {0};
    if (std::fread(riff, 1, 4, fp) != 4 || std::memcmp(riff, "RIFF", 4) != 0) return false;
    uint32_t _ = 0; std::fread(&_, 4, 1, fp);
    char wave[4] = {0};
    if (std::fread(wave, 1, 4, fp) != 4 || std::memcmp(wave, "WAVE", 4) != 0) return false;

    char chunk[4] = {0};
    bool got_fmt = false, got_data = false;
    while (!got_fmt || !got_data) {
        if (std::fread(chunk, 1, 4, fp) != 4) break;
        uint32_t cs = 0; std::fread(&cs, 4, 1, fp);
        if (std::memcmp(chunk, "fmt ", 4) == 0) {
            got_fmt = true;
            uint16_t af = 0, nc = 0; uint32_t sr = 0, br = 0;
            uint16_t ba = 0, bps = 0;
            std::fread(&af, 2, 1, fp); std::fread(&nc, 2, 1, fp);
            std::fread(&sr, 4, 1, fp); std::fread(&br, 4, 1, fp);
            std::fread(&ba, 2, 1, fp); std::fread(&bps, 2, 1, fp);
            if (cs > 16) std::fseek(fp, cs - 16, SEEK_CUR);
            if (af != 1 || bps != 16) return false;
            rate = sr; stereo = (nc == 2);
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            got_data = true;
            data_start = static_cast<uint32_t>(ftell(fp));
            data_size  = cs;
            break;
        } else {
            std::fseek(fp, cs, SEEK_CUR);
        }
    }
    if (!got_fmt || !got_data) return false;
    std::fseek(fp, data_start, SEEK_SET);
    return true;
}

/* I2S STD 通道配置（按源采样率/声道）。
 * want_rx=true 时一次拿双 handle：TX 配 bclk/ws/dout（时钟源），
 * RX 配 din 且 bclk/ws=UNUSED（时钟跟随 TX 内部同步）——INMP441 常挂，供全双工录音。 */
static esp_err_t setup_i2s_full(int rate, bool stereo,
                                i2s_chan_handle_t& tx,
                                i2s_chan_handle_t& rx,
                                bool want_rx) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_PORT, I2S_ROLE_MASTER);
    if (i2s_new_channel(&chan_cfg, &tx, want_rx ? &rx : nullptr) != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed"); return ESP_FAIL;
    }
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(static_cast<uint32_t>(rate)),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,
            stereo ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = static_cast<gpio_num_t>(I2S_BCLK_GPIO),
            .ws   = static_cast<gpio_num_t>(I2S_WS_GPIO),
            .dout = static_cast<gpio_num_t>(I2S_DOUT_GPIO),
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false, .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    if (i2s_channel_init_std_mode(tx, &std_cfg) != ESP_OK) {
        i2s_del_channel(tx); tx = nullptr;
        if (want_rx && rx) { i2s_del_channel(rx); rx = nullptr; }
        ESP_LOGE(TAG, "i2s_channel_init_std_mode(tx) failed"); return ESP_FAIL;
    }
    if (want_rx) {
        i2s_std_config_t rx_cfg = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(static_cast<uint32_t>(rate)),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                I2S_DATA_BIT_WIDTH_24BIT, I2S_SLOT_MODE_MONO),
            .gpio_cfg = {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = I2S_GPIO_UNUSED,      /* 时钟跟随 TX（共享端口内部同步） */
                .ws   = I2S_GPIO_UNUSED,
                .dout = I2S_GPIO_UNUSED,
                .din  = static_cast<gpio_num_t>(MIC_SD_GPIO),
                .invert_flags = {},   /* 全部不反相，补 -Werror=missing-field-initializers */
            },
        };
        if (i2s_channel_init_std_mode(rx, &rx_cfg) != ESP_OK ||
            i2s_channel_enable(rx) != ESP_OK) {
            i2s_del_channel(tx); tx = nullptr;
            i2s_del_channel(rx); rx = nullptr;
            ESP_LOGE(TAG, "i2s_channel_init_std_mode(rx) failed"); return ESP_FAIL;
        }
    }
    i2s_channel_enable(tx);
    return ESP_OK;
}

/* WAV 播放（内部实现） */
static esp_err_t play_wav_impl(const std::string& path) {
    /* 在播：先停并等旧 playback_task 真退出（防双任务读写 s_ctx），再开新播放 */
    if (s_ctx.playing->load()) I2sAudio::instance()->stop();

    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) { ESP_LOGE(TAG, "open %s failed", path.c_str()); return ESP_ERR_NOT_FOUND; }

    int rate = 44100; bool stereo = true;
    uint32_t data_start = 0, data_size = 0;
    if (!parse_wav(fp, rate, stereo, data_start, data_size)) {
        ESP_LOGE(TAG, "parse WAV failed");
        std::fclose(fp); return ESP_ERR_INVALID_ARG;
    }

    i2s_chan_handle_t tx = nullptr, rx = nullptr;
    if (setup_i2s_full(rate, stereo, tx, rx, true) != ESP_OK) { std::fclose(fp); return ESP_FAIL; }

    s_ctx.fp = fp;
    s_ctx.data_size = data_size;
    s_ctx.tx = tx;
    s_ctx.rx = rx;
    s_ctx.rate = static_cast<uint32_t>(rate);
    s_ctx.is_mp3 = false;
    s_ctx.playing->store(true);
    if (s_done_evt) xEventGroupClearBits(s_done_evt, PLAY_DONE_BIT);

    BaseType_t ok = xTaskCreate(playback_task, "audio_play", 6144,
                                nullptr, 3, nullptr);
    if (ok != pdPASS) {
        i2s_channel_disable(tx); i2s_del_channel(tx);
        if (rx) { i2s_channel_disable(rx); i2s_del_channel(rx); s_ctx.rx = nullptr; }
        std::fclose(fp); s_ctx.playing->store(false);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/* MP3 播放（minimp3 流式） */
static esp_err_t play_mp3_impl(const std::string& path) {
    /* 在播：先停并等旧 playback_task 真退出（防双任务读写 s_ctx），再开新播放 */
    if (s_ctx.playing->load()) I2sAudio::instance()->stop();

    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) { ESP_LOGE(TAG, "open %s failed", path.c_str()); return ESP_ERR_NOT_FOUND; }

    /* callback IO：FILE* 经 user_data 传给 read/seek（io 存全局 s_ctx，任务期常驻） */
    s_ctx.io.read = mp3_read_cb;   s_ctx.io.read_data = fp;
    s_ctx.io.seek = mp3_seek_cb;   s_ctx.io.seek_data = fp;

    mp3dec_ex_t mp3{};
    if (mp3dec_ex_open_cb(&mp3, &s_ctx.io, MP3D_SEEK_TO_BYTE) != 0) {
        ESP_LOGE(TAG, "mp3 open failed: %s", path.c_str());
        std::fclose(fp); return ESP_ERR_INVALID_ARG;
    }

    /* 解首帧确定采样率/声道（open 阶段只建索引不填 info） */
    mp3d_sample_t* pcm = nullptr;
    mp3dec_frame_info_t fi{};
    size_t first = mp3dec_ex_read_frame(&mp3, &pcm, &fi, MINIMP3_MAX_SAMPLES_PER_FRAME);
    if (first == 0 || fi.hz < 8000 || fi.hz > 96000 ||
        (fi.channels != 1 && fi.channels != 2)) {
        ESP_LOGE(TAG, "mp3 bad first frame: %s", path.c_str());
        mp3dec_ex_close(&mp3); std::fclose(fp); return ESP_ERR_INVALID_ARG;
    }

    i2s_chan_handle_t tx = nullptr, rx = nullptr;
    if (setup_i2s_full(fi.hz, fi.channels == 2, tx, rx, true) != ESP_OK) {
        mp3dec_ex_close(&mp3); std::fclose(fp); return ESP_FAIL;
    }

    s_ctx.fp = fp;
    s_ctx.data_size = 0;
    s_ctx.tx = tx;
    s_ctx.rx = rx;
    s_ctx.rate = static_cast<uint32_t>(fi.hz);
    s_ctx.is_mp3 = true;
    s_ctx.mp3 = mp3;                     /* 结构体拷贝：解码器状态移交给播放任务 */
    s_ctx.first_samples = first;
    std::memcpy(s_ctx.first_pcm, pcm, first * sizeof(mp3d_sample_t));
    s_ctx.playing->store(true);
    if (s_done_evt) xEventGroupClearBits(s_done_evt, PLAY_DONE_BIT);

    BaseType_t ok = xTaskCreate(playback_task, "audio_play", 6144,
                                nullptr, 3, nullptr);
    if (ok != pdPASS) {
        i2s_channel_disable(tx); i2s_del_channel(tx);
        if (rx) { i2s_channel_disable(rx); i2s_del_channel(rx); s_ctx.rx = nullptr; }
        mp3dec_ex_close(&mp3); std::fclose(fp);
        s_ctx.playing->store(false);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/* 从 ADTS 裸流读一帧（含 7 字节头），返回帧长；EOF/缓冲不足返回 0 */
static uint32_t read_adts_frame(FILE* fp, uint8_t* out, uint32_t cap) {
    uint8_t h[7];
    /* 找同步字 0xFFF（最多扫 1KB 容错） */
    for (int probe = 0; probe < 1024; probe++) {
        if (std::fread(h, 1, 1, fp) != 1) return 0;
        if (h[0] != 0xFF) continue;
        if (std::fread(h + 1, 1, 1, fp) != 1) return 0;
        if ((h[1] & 0xF6) != 0xF0) continue;   /* 0xFFFx */
        break;
    }
    if (std::fread(h + 2, 1, 5, fp) != 5) return 0;
    uint32_t flen = ((uint32_t)(h[3] & 0x03) << 11) | ((uint32_t)h[4] << 3) | ((uint32_t)h[5] >> 5);
    if (flen < 7) return 0;
    if (flen > cap) { ESP_LOGW(TAG, "ADTS frame too big: %lu", (unsigned long)flen); return 0; }
    std::memcpy(out, h, 7);
    if (std::fread(out + 7, 1, flen - 7, fp) != flen - 7) return 0;
    return flen;
}

/* M4A/AAC 播放（Helix AAC 流式解码） */
static esp_err_t play_aac_impl(const std::string& path) {
    /* 在播：先停并等旧 playback_task 真退出（防双任务读写 s_ctx），再开新播放 */
    if (s_ctx.playing->load()) I2sAudio::instance()->stop();

    bool is_m4a = false;
    {
        std::string lower = path;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        is_m4a = lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".m4a") == 0;
    }

    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) { ESP_LOGE(TAG, "open %s failed", path.c_str()); return ESP_ERR_NOT_FOUND; }

    M4aDemux* demux = nullptr;
    if (is_m4a) {
        demux = new M4aDemux();
        if (!demux->open(path)) {
            ESP_LOGE(TAG, "m4a demux failed: %s (%s)", path.c_str(), demux->error());
            delete demux; std::fclose(fp); return ESP_ERR_INVALID_ARG;
        }
    }

    HAACDecoder dec = AACInitDecoder();
    if (!dec) {
        ESP_LOGE(TAG, "AACInitDecoder failed");
        delete demux; std::fclose(fp); return ESP_ERR_NO_MEM;
    }

    /* 帧缓冲 + 首帧输出缓冲（PSRAM） */
    constexpr uint32_t FRAME_CAP = 4096 + 7;
    uint8_t* frame = static_cast<uint8_t*>(heap_caps_malloc(FRAME_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    int16_t* out = static_cast<int16_t*>(heap_caps_malloc(4096 * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!frame || !out) {
        ESP_LOGE(TAG, "aac buffers oom");
        heap_caps_free(frame); heap_caps_free(out);
        AACFreeDecoder(dec); delete demux; std::fclose(fp);
        return ESP_ERR_NO_MEM;
    }

    /* 读首帧并解码：确定采样率/声道 */
    uint32_t flen = 0;
    if (demux) {
        if (demux->count == 0) { ESP_LOGE(TAG, "m4a has no samples"); goto fail; }
        m4a_make_adts(frame, demux->sample_rate, demux->channels, demux->samples[0].size + 7);
        uint32_t got = demux->read_sample(0, frame + 7, FRAME_CAP - 7);
        if (got != demux->samples[0].size) { ESP_LOGE(TAG, "m4a read sample 0 short"); goto fail; }
        flen = demux->samples[0].size + 7;
    } else {
        flen = read_adts_frame(fp, frame, FRAME_CAP);
        if (flen == 0) { ESP_LOGE(TAG, "aac: no ADTS frame"); goto fail; }
    }

    {
        uint8_t* in = frame;
        int left = static_cast<int>(flen);
        /* 注意：earlephilhower 版 AACDecode 返回错误码（成功 = ERR_AAC_NONE = 0），
           输出样本数须从 AACFrameInfo.outputSamps 取，不能用返回值当样本数 */
        int rc = AACDecode(dec, &in, &left, out);
        if (rc < 0) { ESP_LOGE(TAG, "aac first frame decode rc=%d", rc); goto fail; }

        AACFrameInfo fi{};
        AACGetLastFrameInfo(dec, &fi);
        if (fi.sampRateOut < 8000 || fi.sampRateOut > 96000 ||
            (fi.nChans != 1 && fi.nChans != 2)) {
            ESP_LOGE(TAG, "aac unsupported format: %dHz %dch", fi.sampRateOut, fi.nChans);
            goto fail;
        }
        uint32_t n = static_cast<uint32_t>(fi.outputSamps);

        i2s_chan_handle_t tx = nullptr, rx = nullptr;
        if (setup_i2s_full(fi.sampRateOut, fi.nChans == 2, tx, rx, true) != ESP_OK) goto fail;

        s_ctx.fp = fp;
        s_ctx.data_size = 0;
        s_ctx.tx = tx;
        s_ctx.rx = rx;
        s_ctx.rate = static_cast<uint32_t>(fi.sampRateOut);
        s_ctx.is_aac = true;
        s_ctx.aac = dec;
        s_ctx.demux = demux;
        s_ctx.aac_index = demux ? 1 : 0;
        s_ctx.frame = frame;
        s_ctx.frame_cap = FRAME_CAP;
        s_ctx.aac_first_n = static_cast<uint16_t>(n);
        s_ctx.aac_first_pcm = out;      /* out 即首帧 PCM：任务先写首帧，再复用为输出缓冲 */
        s_ctx.playing->store(true);
        if (s_done_evt) xEventGroupClearBits(s_done_evt, PLAY_DONE_BIT);

        BaseType_t ok = xTaskCreate(playback_task, "audio_play", 6144,
                                    nullptr, 3, nullptr);
        if (ok != pdPASS) {
            i2s_channel_disable(tx); i2s_del_channel(tx);
            if (rx) { i2s_channel_disable(rx); i2s_del_channel(rx); s_ctx.rx = nullptr; }
            AACFreeDecoder(dec); delete demux; std::fclose(fp);
            heap_caps_free(frame); heap_caps_free(out);
            s_ctx.playing->store(false);
            return ESP_ERR_NO_MEM;
        }
        return ESP_OK;
    }

fail:
    heap_caps_free(frame);
    heap_caps_free(out);
    AACFreeDecoder(dec);
    delete demux;
    std::fclose(fp);
    return ESP_ERR_INVALID_ARG;
}

esp_err_t I2sAudio::play_wav(const std::string& path) {
    return play_wav_impl(path);
}

esp_err_t I2sAudio::play_audio(const std::string& path) {
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto end_with = [&](const char* suf) {
        size_t len = std::strlen(suf);
        return lower.size() >= len && lower.compare(lower.size() - len, len, suf) == 0;
    };
    if (end_with(".mp3")) return play_mp3_impl(path);
    if (end_with(".m4a") || end_with(".aac")) return play_aac_impl(path);
    return play_wav_impl(path);
}

void I2sAudio::stop() {
    if (!_playing.load()) return;
    _playing.store(false);
    /* 等 playback_task 真正退出（置 PLAY_DONE 后其 s_ctx 清理已完成），
     * 500ms 超时回退：此时 I2S 端口单通道约束会让下一次 play 的 setup 失败，不会双任务 */
    if (s_done_evt) {
        xEventGroupWaitBits(s_done_evt, PLAY_DONE_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(500));
    }
}

static void playback_task(void* arg) {
    (void)arg;   /* 通过全局 s_ctx 访问 */
    PlayCtx& ctx = s_ctx;
    if (!ctx.fp || !ctx.tx) {
        if (s_done_evt) xEventGroupSetBits(s_done_evt, PLAY_DONE_BIT);
        vTaskDelete(nullptr); return;
    }

    constexpr size_t CHUNK = 4096;
    int16_t* buf = static_cast<int16_t*>(heap_caps_malloc(
        CHUNK, MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
    if (!buf) {
        if (s_done_evt) xEventGroupSetBits(s_done_evt, PLAY_DONE_BIT);
        vTaskDelete(nullptr); return;
    }

    i2s_chan_handle_t tx = ctx.tx;

    if (ctx.is_aac) {
        /* ---- M4A/AAC 流式解码分支（Helix） ---- */
        int16_t* pcm = ctx.aac_first_pcm;              /* 首帧输出缓冲（PSRAM） */
        if (ctx.aac_first_n > 0) {
            size_t nb = ctx.aac_first_n * sizeof(int16_t);
            if (!ctx.muted->load()) {
                float vol = std::clamp<float>(static_cast<float>(ctx.volume->load()) / 50.0f, 0.0f, 2.0f);
                for (size_t i = 0; i < ctx.aac_first_n; i++) {
                    int32_t v = static_cast<int32_t>(pcm[i]) * static_cast<int32_t>(vol);
                    pcm[i] = static_cast<int16_t>(std::clamp<int32_t>(v, -32768, 32767));
                }
            } else {
                std::memset(pcm, 0, nb);
            }
            size_t written = 0;
            i2s_channel_write(tx, pcm, nb, &written, portMAX_DELAY);
        }
        while (ctx.playing->load()) {
            uint32_t flen;
            if (ctx.demux) {
                /* MP4：拼 ADTS 头 + 裸 AAC sample */
                size_t idx = ctx.aac_index;
                if (idx >= ctx.demux->count) break;
                m4a_make_adts(ctx.frame, ctx.demux->sample_rate, ctx.demux->channels,
                              ctx.demux->samples[idx].size + 7);
                uint32_t got = ctx.demux->read_sample(idx, ctx.frame + 7, ctx.frame_cap - 7);
                if (got != ctx.demux->samples[idx].size) break;   /* 短读视为流结束 */
                ctx.aac_index = idx + 1;
                flen = ctx.demux->samples[idx].size + 7;
            } else {
                flen = read_adts_frame(ctx.fp, ctx.frame, ctx.frame_cap);
                if (flen == 0) break;
            }
            uint8_t* in = ctx.frame;
            int left = static_cast<int>(flen);
            /* AACDecode 返回错误码；成功帧的样本数取 outputSamps */
            int rc = AACDecode(ctx.aac, &in, &left, pcm);
            if (rc < 0) {
                if (rc == ERR_AAC_INDATA_UNDERFLOW) break;
                ESP_LOGW(TAG, "aac decode rc=%d (skip)", rc);
                continue;
            }
            AACFrameInfo fi{};
            AACGetLastFrameInfo(ctx.aac, &fi);
            uint32_t n = static_cast<uint32_t>(fi.outputSamps);
            if (!ctx.muted->load()) {
                float vol = std::clamp<float>(static_cast<float>(ctx.volume->load()) / 50.0f, 0.0f, 2.0f);
                for (uint32_t i = 0; i < n; i++) {
                    int32_t v = static_cast<int32_t>(pcm[i]) * static_cast<int32_t>(vol);
                    pcm[i] = static_cast<int16_t>(std::clamp<int32_t>(v, -32768, 32767));
                }
            } else {
                std::memset(pcm, 0, n * sizeof(int16_t));
            }
            size_t written = 0;
            i2s_channel_write(tx, pcm, n * sizeof(int16_t), &written, portMAX_DELAY);
        }
        if (ctx.demux) { delete ctx.demux; ctx.demux = nullptr; }
        if (ctx.aac) { AACFreeDecoder(ctx.aac); ctx.aac = nullptr; }
        heap_caps_free(ctx.frame);
        heap_caps_free(ctx.aac_first_pcm);
        ctx.frame = nullptr; ctx.aac_first_pcm = nullptr;
        ESP_LOGI(TAG, "AAC done");
    } else if (ctx.is_mp3) {
        /* ---- MP3 流式解码分支 ---- */
        if (ctx.first_samples > 0) {
            size_t n0 = ctx.first_samples * sizeof(mp3d_sample_t);
            std::memcpy(buf, ctx.first_pcm, n0);
            if (!ctx.muted->load()) {
                float vol = std::clamp<float>(static_cast<float>(ctx.volume->load()) / 50.0f, 0.0f, 2.0f);
                for (size_t i = 0; i < ctx.first_samples; i++) {
                    int32_t v = static_cast<int32_t>(buf[i]) * static_cast<int32_t>(vol);
                    buf[i] = static_cast<int16_t>(std::clamp<int32_t>(v, -32768, 32767));
                }
            } else {
                std::memset(buf, 0, n0);
            }
            size_t written = 0;
            i2s_channel_write(tx, buf, n0, &written, portMAX_DELAY);
        }
        while (ctx.playing->load()) {
            size_t got = mp3dec_ex_read(&ctx.mp3, buf, CHUNK / 2);
            if (got == 0) break;
            size_t n = got * sizeof(mp3d_sample_t);
            if (!ctx.muted->load()) {
                float vol = std::clamp<float>(static_cast<float>(ctx.volume->load()) / 50.0f, 0.0f, 2.0f);
                for (size_t i = 0; i < got; i++) {
                    int32_t v = static_cast<int32_t>(buf[i]) * static_cast<int32_t>(vol);
                    buf[i] = static_cast<int16_t>(std::clamp<int32_t>(v, -32768, 32767));
                }
            } else {
                std::memset(buf, 0, n);
            }
            size_t written = 0;
            i2s_channel_write(tx, buf, n, &written, portMAX_DELAY);
        }
        mp3dec_ex_close(&ctx.mp3);
        ESP_LOGI(TAG, "MP3 done");
    } else {
        /* ---- WAV 直放分支 ---- */
        uint32_t bytes_read = 0;
        while (ctx.playing->load() && bytes_read < ctx.data_size) {
            uint32_t remain = ctx.data_size - bytes_read;
            uint32_t want = std::min<uint32_t>(static_cast<uint32_t>(CHUNK), remain);
            size_t n = std::fread(buf, 1, want, ctx.fp);
            if (n == 0) break;

            if (!ctx.muted->load()) {
                float vol = std::clamp<float>(static_cast<float>(ctx.volume->load()) / 50.0f, 0.0f, 2.0f);
                int32_t samp_count = static_cast<int32_t>(n / 2);
                int16_t* p = buf;
                for (int32_t i = 0; i < samp_count; i++) {
                    int32_t v = static_cast<int32_t>(*p) * static_cast<int32_t>(vol);
                    *p = static_cast<int16_t>(std::clamp<int32_t>(v, -32768, 32767));
                    p++;
                }
            } else {
                std::memset(buf, 0, n);
            }

            size_t written = 0;
            i2s_channel_write(tx, buf, n, &written, portMAX_DELAY);
            bytes_read += n;
        }
        ESP_LOGI(TAG, "WAV done. %lu bytes", (unsigned long)bytes_read);
    }

    i2s_channel_disable(tx);
    i2s_del_channel(tx);
    if (ctx.rx) {
        if (!I2sAudio::instance()->_recording.load()) {
            /* 无录音：播放收尾 del 常驻 RX */
            i2s_channel_disable(ctx.rx);
            i2s_del_channel(ctx.rx);
        }
        /* 录音中：RX 交 rec_task 收尾 del（单一 del 责任方，防 double-del） */
        ctx.rx = nullptr;
    }
    std::fclose(ctx.fp);
    ctx.fp = nullptr;
    std::free(buf);
    if (s_done_evt) xEventGroupSetBits(s_done_evt, PLAY_DONE_BIT);
    vTaskDelete(nullptr);
}

/* ============================================================
 * INMP441 数字麦克风录音（I2S RX → TF 卡 WAV 16bit mono）
 *
 * BCLK/WS 与功放共用（I2S0 端口），RX 只走独立 SD 数据脚。
 * INMP441 输出 24-bit 左对齐（WS 低电平有效），DMA 每样本 3 字节，
 * 转 16-bit 写 WAV：RIFF 头先写占位，rec_stop 回填真实长度。
 * ============================================================ */
struct RecCtx {
    i2s_chan_handle_t rx = nullptr;
    i2s_chan_handle_t tx = nullptr;  /* 纯录音时建的 TX（时钟源，欠载静音）；复用播放 RX 时为 nullptr */
    FILE* fp = nullptr;
    uint32_t rate = 44100;
};
static RecCtx s_rec;

static void rec_write_wav_header(FILE* fp, uint32_t rate, uint32_t data_len) {
    char h[44] = {0};
    uint32_t u32 = 36 + data_len; uint16_t u16;
    std::memcpy(h, "RIFF", 4);
    std::memcpy(h + 4, &u32, 4);
    std::memcpy(h + 8, "WAVE", 4);
    std::memcpy(h + 12, "fmt ", 4);
    u32 = 16; std::memcpy(h + 16, &u32, 4);
    u16 = 1;  std::memcpy(h + 20, &u16, 2);   /* PCM */
    u16 = 1;  std::memcpy(h + 22, &u16, 2);   /* mono */
    std::memcpy(h + 24, &rate, 4);
    u32 = rate * 2; std::memcpy(h + 28, &u32, 4);   /* byte rate */
    u16 = 2;  std::memcpy(h + 32, &u16, 2);   /* block align */
    u16 = 16; std::memcpy(h + 34, &u16, 2);   /* bits per sample */
    std::memcpy(h + 36, "data", 4);
    std::memcpy(h + 40, &data_len, 4);
    std::fseek(fp, 0, SEEK_SET);
    std::fwrite(h, 1, 44, fp);
    std::fflush(fp);
}

static void rec_task(void* arg) {
    (void)arg;
    RecCtx& rc = s_rec;
    uint8_t* dma = static_cast<uint8_t*>(
        heap_caps_malloc(4096, MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
    int16_t* out = static_cast<int16_t*>(
        heap_caps_malloc(4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!dma || !out) {
        heap_caps_free(dma); heap_caps_free(out);
        vTaskDelete(nullptr); return;
    }
    uint32_t total = 0;
    while (I2sAudio::instance()->_recording.load()) {
        size_t got = 0;
        esp_err_t err = i2s_channel_read(rc.rx, dma, 4096, &got, pdMS_TO_TICKS(100));
        if (err != ESP_OK || got == 0) continue;
        size_t n = got / 3;              /* 24-bit → 3 字节/样本 */
        for (size_t i = 0; i < n; i++) {
            int32_t s = static_cast<int32_t>(
                dma[i * 3] | (dma[i * 3 + 1] << 8) | (dma[i * 3 + 2] << 16));
            if (s & 0x800000) s |= ~0xFFFFFF;   /* 24-bit 符号扩展 */
            out[i] = static_cast<int16_t>(s >> 8);
        }
        std::fwrite(out, 2, n, rc.fp);
        total += n;
    }
    I2sAudio::instance()->_rec_seconds.store(total / rc.rate);
    rec_write_wav_header(rc.fp, rc.rate, total * 2);
    std::fclose(rc.fp); rc.fp = nullptr;
    i2s_channel_disable(rc.rx);
    i2s_del_channel(rc.rx); rc.rx = nullptr;
    if (rc.tx) { i2s_channel_disable(rc.tx); i2s_del_channel(rc.tx); rc.tx = nullptr; }
    heap_caps_free(dma); heap_caps_free(out);
    vTaskDelete(nullptr);
}

esp_err_t I2sAudio::rec_start(const std::string& path, uint32_t sample_rate) {
    if (_recording.load() || s_rec.rx) return ESP_ERR_INVALID_STATE;

    FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp) return ESP_ERR_NOT_FOUND;

    i2s_chan_handle_t tx = nullptr, rx = nullptr;
    if (_playing.load()) {
        /* 全双工：复用播放常驻 RX，不停播放；采样率跟随播放 */
        if (!s_ctx.rx) {
            std::fclose(fp); return ESP_ERR_INVALID_STATE;
        }
        rx = s_ctx.rx;
        if (s_ctx.rate) sample_rate = s_ctx.rate;
    } else {
        /* 纯录音：建 tx+rx 对，tx 不写数据（欠载静音），时钟由 TX 驱动 */
        if (setup_i2s_full(static_cast<int>(sample_rate), false, tx, rx, true) != ESP_OK) {
            std::fclose(fp); return ESP_FAIL;
        }
    }
    rec_write_wav_header(fp, sample_rate, 0);   /* 占位头，结束回填 */

    s_rec.rx = rx; s_rec.tx = tx; s_rec.fp = fp; s_rec.rate = sample_rate;
    _rec_seconds.store(0);
    _recording.store(true);
    if (xTaskCreate(rec_task, "audio_rec", 4096, nullptr, 3, nullptr) != pdPASS) {
        _recording.store(false);
        if (tx) {
            /* 纯录音：两个都回收 */
            i2s_channel_disable(rx); i2s_del_channel(rx);
            i2s_channel_disable(tx); i2s_del_channel(tx);
        }
        /* 复用播放 RX：不 del（还给 s_ctx.rx），播放不受影响 */
        std::fclose(fp); s_rec.rx = nullptr; s_rec.tx = nullptr; s_rec.fp = nullptr;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "REC start: %s @%luHz%s", path.c_str(), (unsigned long)sample_rate,
             tx ? " (standalone)" : " (full-duplex)");
    return ESP_OK;
}

esp_err_t I2sAudio::rec_stop() {
    if (!_recording.load()) return ESP_ERR_INVALID_STATE;
    _recording.store(false);
    /* 等任务回填 WAV 头并释放通道（read 超时 100ms + 写头，500ms 足够） */
    for (int i = 0; i < 50 && s_rec.rx; i++) vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "REC stop: %lus", (unsigned long)_rec_seconds.load());
    return ESP_OK;
}

} // namespace drivers
} // namespace memoria

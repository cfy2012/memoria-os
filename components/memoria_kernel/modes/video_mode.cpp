/**
 * @file video_mode.cpp
 * @brief VIDEO 视频模式
 *
 * 播放链路：读取 /mem_fat/video 下的 .mjpg 文件 → 扫描连续 JPEG 帧（FFD8…FFD9）
 * → TJpgDec（tjpgd，公有领域微型 JPEG 解码器）逐帧软解
 * → RGB565 输出写入 PSRAM 帧缓冲 → DMA 推屏。
 *
 * 分辨率自适应：源视频无论多大（最高 8K），解码时自动降采样到屏幕尺寸以内
 * （tjpgd scale 1/2·1/4·1/8，解码量平方级下降），并动态监控单帧耗时：
 * 连续偏慢自动再降一档保流畅，长时间有余量自动恢复清晰度档位。
 * 1080P→480×270、4K→480×270 均可流畅播放。
 * 帧率：60ms/帧（≈16.6fps 上限），实际受解码耗时限制。
 */

#include "mode_manager.hpp"
#include "ili9341.hpp"

extern "C" {
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <dirent.h>
#include <sys/stat.h>
}

#include "tjpgd.h"

#include <cstdio>
#include <cstring>

#include <vector>
#include <string>
#include <algorithm>
#include <memory>

namespace memoria {
using namespace drivers;
namespace modes {

static const char* TAG = "VIDEO_MODE";

/* MJPG 帧标记 */
static constexpr uint16_t SOI = 0xFFD8;
static constexpr uint16_t EOI = 0xFFD9;
static constexpr size_t  MAX_FILE_BYTES = 8 * 1024 * 1024;   /* 8MB PSRAM 上限 */
static constexpr size_t  JPEG_WORK_BYTES = 8 * 1024;         /* tjpgd 工作区（内部 RAM） */
static constexpr int64_t FRAME_US       = 60000;             /* ≈16.6fps 上限 */

/* 自适应降采样（tjpgd scale：0=1:1, 1=1/2, 2=1/4, 3=1/8） */
static constexpr uint32_t MAX_SRC_W = 8192;   /* 源分辨率合理上限，防野数据 */
static constexpr uint32_t MAX_SRC_H = 8192;
static constexpr int64_t  SLOW_FRAME_US = 90000;   /* 单帧 >90ms(≈11fps) 视为偏慢 */
static constexpr int64_t  FAST_FRAME_US = 40000;   /* 单帧 <40ms(≈25fps) 视为有余量 */
static constexpr int      SLOW_N = 6;              /* 连续 6 帧慢 → 降一档清晰度 */
static constexpr int      FAST_N = 30;             /* 连续 30 帧快 → 恢复一档清晰度 */

/* ============================================================
 *  TJpgDec I/O 回调
 * ============================================================ */
struct DecCtx {
    const uint8_t* data = nullptr;   /* 当前帧数据起点 */
    size_t         size = 0;         /* 帧数据字节数 */
    size_t         pos  = 0;         /* 流内读取位置 */
    uint8_t*       out  = nullptr;   /* 解码输出缓冲（RGB565） */
    uint32_t       stride = 0;       /* 输出行字节数 */
};

static size_t jpeg_in(JDEC* jd, uint8_t* buf, size_t ndata) {
    auto* c = static_cast<DecCtx*>(jd->device);
    size_t n = (c->pos + ndata <= c->size) ? ndata : (c->size - c->pos);
    if (n == 0) return 0;
    std::memcpy(buf, c->data + c->pos, n);
    c->pos += n;
    return n;
}

static int jpeg_out(JDEC* jd, void* bitmap, JRECT* rect) {
    auto* c = static_cast<DecCtx*>(jd->device);
    const uint16_t w = rect->right - rect->left + 1;
    const uint16_t h = rect->bottom - rect->top + 1;
    const uint8_t* src = static_cast<const uint8_t*>(bitmap);
    uint8_t* dst = c->out + (size_t)rect->top * c->stride + (size_t)rect->left * 2;
    for (uint16_t y = 0; y < h; ++y) {
        std::memcpy(dst, src, (size_t)w * 2);
        src += (size_t)w * 2;
        dst += c->stride;
    }
    return 1;   /* 继续解码 */
}

class VideoMode : public ModeWindow {
public:
    VideoMode() { _scan(); }
    ~VideoMode() { _stop_play(); }

    void mode_render(window::UIRenderer* ui, bool focused) override {
        /* 播放中：按帧率推进解码并推屏，不绘制列表 UI */
        if (_playing) {
            int64_t now = esp_timer_get_time();
            if (now - _last_ms >= FRAME_US) {
                _last_ms = now;
                if (!_render_frame(_fidx)) { _stop_play(); }
                else _fidx = (_fidx + 1) % _frames.size();
            }
            return;
        }

        draw_title(ui, "VIDEO 视频", _files.empty() ? "0 个" : std::to_string(_files.size()).c_str());
        if (_files.empty()) { draw_hint(ui, "空 · 把 .mjpg 放进 /mem_fat/video"); return; }
        int y = 34;
        int start = std::max(0, _cur - 8);
        for (int i = start; i < (int)_files.size() && y < SCREEN_H - 16 - 12; i++, y += 12) {
            std::string line = (i == _cur ? "> " : "  ") + _files[i];
            ui->draw_text(2, y, line, i == _cur ? COLOR_YELLOW : COLOR_WHITE);
        }
        if (!_hint.empty()) draw_hint(ui, _hint.c_str());
        else draw_hint(ui, "Enter 播放 · 上下选择");
    }

    bool mode_nav(const window::NavInput& ni) override {
        switch (ni.dir) {
            case window::NavEvent::NavUp:    _cur = std::max(0, _cur - 1); break;
            case window::NavEvent::NavDown:  _cur = std::min((int)_files.size() - 1, _cur + 1); break;
            case window::NavEvent::NavEnter:
                if (_playing) _stop_play();
                else _play();
                break;
            default: return false;
        }
        return true;
    }

    bool on_key(uint16_t key, bool pressed) override {
        if (!pressed) return false;
        if (_playing && key == 0x001B) { _stop_play(); return true; }   /* ESC 停止 */
        return false;
    }

    const char* const* fn_labels() override {
        static const char* f[6] = {"播放", "上一", "下一", "快进", "停止", "菜单"};
        return f;
    }

    void on_fn(int idx) override {
        if (_playing && idx != 5) { if (idx == 4) _stop_play(); return; }
        if (_files.empty() && idx != 5) return;
        switch (idx) {
            case 0: _play(); break;
            case 1: _cur = std::max(0, _cur - 1); break;
            case 2: _cur = std::min((int)_files.size() - 1, _cur + 1); break;
            case 3: _hint = "快进 +5s（当前帧流按序循环）"; break;
            case 4: _stop_play(); break;
            case 5: _hint = "目录 /mem_fat/video · 支持 .mjpg"; break;
        }
    }

private:
    void _scan() {
        _files.clear();
        DIR* d = opendir("/mem_fat/video");
        if (!d) return;
        struct dirent* e;
        while ((e = readdir(d)) != nullptr) {
            std::string n = e->d_name;
            if (n.size() > 3) _files.push_back(n);
        }
        closedir(d);
        std::sort(_files.begin(), _files.end());
        _cur = std::min(_cur, (int)_files.size() - 1);
    }

    void _play() {
        if (_files.empty()) return;
        _stop_play();
        if (!_work) {
            _work.reset(static_cast<uint8_t*>(heap_caps_malloc(JPEG_WORK_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
            if (!_work) { _hint = "解码工作区分配失败"; return; }
        }
        if (!_load_frames("/mem_fat/video/" + _files[_cur])) {
            _hint = "加载失败: " + _files[_cur];
            return;
        }
        _fidx = 0;
        _scale_off = 0;
        _slow_cnt = _fast_cnt = 0;
        _last_ms = esp_timer_get_time();
        _playing = true;
        ESP_LOGI(TAG, "playing %s (%zu frames)", _files[_cur].c_str(), _frames.size());
    }

    /* 读文件到 PSRAM + 扫描 JPEG 帧边界 */
    bool _load_frames(const std::string& path) {
        FILE* fp = std::fopen(path.c_str(), "rb");
        if (!fp) return false;
        std::fseek(fp, 0, SEEK_END);
        long sz = std::ftell(fp);
        std::fseek(fp, 0, SEEK_SET);
        if (sz <= 0 || (size_t)sz > MAX_FILE_BYTES) { std::fclose(fp); return false; }

        _data.reset(static_cast<uint8_t*>(heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
        if (!_data) { std::fclose(fp); return false; }
        if (std::fread(_data.get(), 1, sz, fp) != (size_t)sz) { std::fclose(fp); return false; }
        std::fclose(fp);

        _frames.clear();
        size_t i = 0;
        while (i + 1 < (size_t)sz) {
            if (_data.get()[i] == 0xFF && _data.get()[i + 1] == 0xD8) {
                uint32_t start = (uint32_t)i;
                size_t j = i + 2;
                while (j + 1 < (size_t)sz) {
                    if (_data.get()[j] == 0xFF && _data.get()[j + 1] == 0xD9) { j += 2; break; }
                    j++;
                }
                _frames.emplace_back(start, (uint32_t)(j - start));
                i = j;
            } else {
                i++;
            }
        }
        return !_frames.empty();
    }

    /* 取"输出不超屏幕"的最小降采样档（0=1:1, 1=1/2, 2=1/4, 3=1/8）。
     * 1080P→2 档(480×270)，4K→3 档(480×270)；解码量平方级下降，保证流畅。 */
    static uint8_t pick_base_scale(uint32_t w, uint32_t h) {
        uint8_t s = 0;
        while (s < 3) {
            uint32_t nw = (w + ((1u << (s + 1)) - 1)) >> (s + 1);
            uint32_t nh = (h + ((1u << (s + 1)) - 1)) >> (s + 1);
            if (nw <= SCREEN_W && nh <= SCREEN_H) break;
            s++;
        }
        return s;
    }

    /* 动态帧率监控：连续 N 帧解码偏慢 → 升一档降采样（牺牲清晰度保流畅）；
     * 长时间明显有余量 → 降回一档（尽量恢复清晰度）。在 base 之上浮动。 */
    void _adapt_scale(uint8_t base, int64_t dt_us) {
        if (dt_us > SLOW_FRAME_US)      { _slow_cnt++; _fast_cnt = 0; }
        else if (dt_us < FAST_FRAME_US) { _fast_cnt++; _slow_cnt = 0; }
        else                            { _slow_cnt = _fast_cnt = 0; }
        if (_slow_cnt >= SLOW_N) {
            if (_scale_off < 3 - base) { _scale_off++; ESP_LOGI(TAG, "降清晰度档 +%u", _scale_off); }
            _slow_cnt = _fast_cnt = 0;
        } else if (_fast_cnt >= FAST_N && _scale_off > 0) {
            _scale_off--; ESP_LOGI(TAG, "恢复清晰度档 %u", _scale_off);
            _slow_cnt = _fast_cnt = 0;
        }
    }

    /* TJpgDec 解码一帧并推屏：按源分辨率自动降采样到屏幕尺寸以内，
     * 居中显示；失败返回 false（由调用方停止播放）。 */
    bool _render_frame(size_t fi) {
        auto [off, len] = _frames[fi];

        DecCtx ctx;
        ctx.data  = _data.get() + off;
        ctx.size  = len;
        ctx.pos   = 0;

        JDEC jdec;
        JRESULT r = jd_prepare(&jdec, jpeg_in, _work.get(), JPEG_WORK_BYTES, &ctx);
        if (r != JDR_OK) {
            ESP_LOGW(TAG, "frame %zu: jd_prepare %d", fi, (int)r);
            return false;
        }
        if (jdec.width == 0 || jdec.height == 0 ||
            jdec.width > MAX_SRC_W || jdec.height > MAX_SRC_H) {
            ESP_LOGW(TAG, "frame %zu: bad size %ux%u", fi, jdec.width, jdec.height);
            return false;
        }

        const uint8_t base = pick_base_scale(jdec.width, jdec.height);
        const uint8_t use  = std::min<uint8_t>(3, base + _scale_off);
        const uint32_t out_w = (jdec.width  + ((1u << use) - 1)) >> use;
        const uint32_t out_h = (jdec.height + ((1u << use) - 1)) >> use;
        const uint32_t out_bytes = out_w * out_h * 2;
        if (out_bytes > _out_cap) {
            _out.reset(static_cast<uint8_t*>(heap_caps_malloc(out_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
            if (!_out) { ESP_LOGE(TAG, "outbuf %u B alloc failed", (unsigned)out_bytes); return false; }
            _out_cap = out_bytes;
        }
        ctx.out    = _out.get();
        ctx.stride = out_w * 2;

        int64_t t0 = esp_timer_get_time();
        r = jd_decomp(&jdec, jpeg_out, use);
        int64_t dt = esp_timer_get_time() - t0;
        if (r != JDR_OK) {
            ESP_LOGW(TAG, "frame %zu: jd_decomp %d", fi, (int)r);
            return false;
        }
        _adapt_scale(base, dt);

        auto* lcd = Ili9341::instance();
        auto* fb  = lcd->framebuffer();
        uint16_t dst_x = (SCREEN_W - out_w) / 2;
        uint16_t dst_y = (SCREEN_H - out_h) / 2;
        const uint8_t* src = _out.get();
        for (uint32_t y = 0; y < out_h; ++y) {
            std::memcpy(fb + (dst_y + y) * SCREEN_W + dst_x, src + y * ctx.stride, (size_t)out_w * 2);
        }
        lcd->flush();
        return true;
    }

    void _stop_play() {
        _playing = false;
        _frames.clear();
        _data.reset();
    }

    std::vector<std::string> _files;
    std::vector<std::pair<uint32_t, uint32_t>> _frames;
    std::unique_ptr<uint8_t, void(*)(void*)> _data{nullptr, heap_caps_free};
    std::unique_ptr<uint8_t, void(*)(void*)> _out{nullptr, heap_caps_free};
    std::unique_ptr<uint8_t, void(*)(void*)> _work{nullptr, heap_caps_free};
    uint32_t _out_cap = 0;
    size_t _fidx = 0;
    int64_t _last_ms = 0;
    bool _playing = false;
    uint8_t _scale_off = 0;   /* 相对"不超屏最小档"的动态偏移（0=基准） */
    int _slow_cnt = 0, _fast_cnt = 0;
    std::string _hint;
};

static std::shared_ptr<window::Window> video_create() {
    return std::make_shared<VideoMode>();
}

void video_mode_register() {
    ModeDesc d{};
    d.id = "video"; d.name = "VIDEO"; d.cn = "视频";
    d.color = rgb565(96, 165, 250);
    d.create = video_create;
    ModeManager::instance()->register_mode(d);
}

} // namespace modes
} // namespace memoria
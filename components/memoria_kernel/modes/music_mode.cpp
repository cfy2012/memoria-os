/**
 * @file music_mode.cpp
 * @brief MUSIC 音乐模式
 *
 * 枚举 /mem_fat/audio 下的 .wav，I2S 播放 + 切歌 + 音量。
 * 上下 = 选曲，Enter = 播放，左右 = 音量，长按返回 = 主菜单。
 */

#include "mode_manager.hpp"
#include "i2s_audio.hpp"
#include "ili9341.hpp"

extern "C" {
#include <dirent.h>
}

#include <cstdio>
#include <cctype>

#include <vector>
#include <string>
#include <algorithm>

namespace memoria {
using namespace drivers;
namespace modes {

class MusicMode : public ModeWindow {
public:
    MusicMode() { _scan(); }

    void mode_render(window::UIRenderer* ui, bool focused) override {
        draw_title(ui, "MUSIC 音乐",
                   _tracks.empty() ? "0 首" : (std::to_string(_tracks.size()) + " 首").c_str());
        if (_tracks.empty()) { draw_hint(ui, "没有歌曲 | 把 .wav/.mp3/.m4a 放进 /mem_fat/audio"); return; }

        auto* aud = drivers::I2sAudio::instance();
        auto* lcd = Ili9341::instance();
        const std::string& cur = _tracks[_cur];
        const bool playing = aud->is_playing();

        /* ---- 正在播放卡片 ---- */
        ui->card({6, 34, 468, 88}, 8, COLOR_CARD, COLOR_STROKE);

        /* 封面占位：圆角底色 + 唱片圆心（选中时用强调色描边） */
        ui->fill_round_rect({14, 42, 68, 68}, 8, COLOR_BG_SOFT);
        ui->draw_round_rect({14, 42, 68, 68}, 8, focused ? _accent : COLOR_STROKE_HI, 2);
        lcd->fill_circle(48, 76, 22, COLOR_CARD_HI);
        lcd->draw_circle(48, 76, 16, COLOR_STROKE);
        lcd->fill_circle(48, 76, 7, _accent);
        lcd->fill_circle(48, 76, 2, COLOR_CARD);

        /* 播放状态圆点：播放中=绿，暂停=灰 */
        lcd->fill_circle(457, 46, 4, playing ? COLOR_OK : COLOR_TEXT_DIM);

        /* 歌名 + 次要信息（右侧音量） */
        ui->draw_text_fit(94, 42, cur, COLOR_TEXT, 336);
        ui->draw_text(94, 60, "本地音频", COLOR_TEXT_DIM);
        const std::string vol = "音量 " + std::to_string(static_cast<int>(aud->get_volume())) + "%";
        ui->draw_text(static_cast<int16_t>(466 - window::UIRenderer::text_width_utf8(vol)),
                      60, vol, COLOR_TEXT_DIM);

        /* 进度条 + 时间（播放位置无对外接口，总长按未知显示） */
        ui->progress({94, 84, 364, 6}, 0, _accent, COLOR_BG_SOFT, 3);
        ui->draw_text(94, 92, "0:00", COLOR_TEXT_DIM);
        ui->draw_text(426, 92, "--:--", COLOR_TEXT_DIM);

        /* ---- 曲目列表：卡片行 ---- */
        const int visible = 6;
        int start = _cur - 2;
        if (start > static_cast<int>(_tracks.size()) - visible)
            start = static_cast<int>(_tracks.size()) - visible;
        if (start < 0) start = 0;

        int16_t y = 128;
        for (int i = start; i < static_cast<int>(_tracks.size()) && i < start + visible; i++, y += 26) {
            const bool sel = (i == _cur);
            const window::Rect row{6, y, 468, 24};
            ui->card(row, 8, sel ? COLOR_CARD_HI : COLOR_CARD,
                     sel ? COLOR_CARD_HI : COLOR_STROKE);
            if (sel) ui->draw_round_rect(row, 8, _accent, 2);

            /* 左侧图标块：正在播放的曲目用绿色，其余用强调色 */
            ui->fill_round_rect({12, static_cast<int16_t>(y + 3), 18, 18}, 6,
                                (i == _cur && playing) ? COLOR_OK : _accent);

            ui->draw_text_fit(38, static_cast<int16_t>(y + 6), _tracks[i], COLOR_TEXT, 400);
            const std::string idx = (i + 1 < 10 ? "0" : "") + std::to_string(i + 1);
            ui->draw_text(static_cast<int16_t>(466 - window::UIRenderer::text_width_utf8(idx)),
                          static_cast<int16_t>(y + 6), idx, COLOR_TEXT_DIM);
        }
        if (!_hint.empty()) draw_hint(ui, _hint.c_str());
    }

    bool mode_nav(const window::NavInput& ni) override {
        if (_tracks.empty()) return true;
        switch (ni.dir) {
            case window::NavEvent::NavUp:    _cur = std::max(0, _cur - 1); break;
            case window::NavEvent::NavDown:  _cur = std::min((int)_tracks.size() - 1, _cur + 1); break;
            case window::NavEvent::NavLeft:  _vol(-1); break;
            case window::NavEvent::NavRight: _vol(+1); break;
            case window::NavEvent::NavEnter: _play(_cur); break;
            default: return false;
        }
        return true;
    }

    const char* const* fn_labels() override {
        static const char* f[6] = {"播放", "上一", "下一", "音量-", "音量+", "菜单"};
        return f;
    }

    void on_fn(int idx) override {
        if (_tracks.empty()) return;
        switch (idx) {
            case 0: _play(_cur); break;
            case 1: _prev(); break;
            case 2: _next(); break;
            case 3: _vol(-1); break;
            case 4: _vol(+1); break;
            case 5: _hint = "列表循环 | 队列自动续播"; break;
        }
    }

private:
    void _scan() {
        _tracks.clear();
        DIR* d = opendir("/mem_fat/audio");
        if (!d) return;
        struct dirent* e;
        while ((e = readdir(d)) != nullptr) {
            std::string n = e->d_name;
            std::string lower = n;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower.size() > 4 &&
                (lower.compare(lower.size() - 4, 4, ".wav") == 0 ||
                 lower.compare(lower.size() - 4, 4, ".mp3") == 0 ||
                 lower.compare(lower.size() - 4, 4, ".m4a") == 0 ||
                 lower.compare(lower.size() - 4, 4, ".aac") == 0))
                _tracks.push_back(n);
        }
        closedir(d);
        std::sort(_tracks.begin(), _tracks.end());
    }

    void _play(int i) {
        auto* aud = drivers::I2sAudio::instance();
        aud->stop();
        aud->play_audio("/mem_fat/audio/" + _tracks[i]);
        _cur = i;
    }
    void _prev() { _play((_cur - 1 + (int)_tracks.size()) % _tracks.size()); }
    void _next() { _play((_cur + 1) % _tracks.size()); }
    void _vol(int d) {
        auto* aud = drivers::I2sAudio::instance();
        uint8_t v = aud->get_volume();
        v = (uint8_t)std::clamp<int>(v + d * 10, 0, 100);
        aud->set_volume(v);
    }

    std::vector<std::string> _tracks;
    std::string _hint;
};

static std::shared_ptr<window::Window> music_create() {
    return std::make_shared<MusicMode>();
}

void music_mode_register() {
    ModeDesc d{};
    d.id = "music"; d.name = "MUSIC"; d.cn = "音乐";
    d.color = rgb565(244, 114, 182);
    d.create = music_create;
    ModeManager::instance()->register_mode(d);
}

} // namespace modes
} // namespace memoria
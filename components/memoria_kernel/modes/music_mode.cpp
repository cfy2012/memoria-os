/**
 * @file music_mode.cpp
 * @brief MUSIC 音乐模式
 *
 * 枚举 /mem_fat/audio 下的 .wav，I2S 播放 + 切歌 + 音量。
 * 上下 = 选曲，Enter = 播放，左右 = 音量，长按返回 = 主菜单。
 */

#include "mode_manager.hpp"
#include "i2s_audio.hpp"

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
        draw_title(ui, "MUSIC 音乐", _tracks.empty() ? "0 首" : std::to_string(_tracks.size()).c_str());
        if (_tracks.empty()) { draw_hint(ui, "没有歌曲 · 把 .wav/.mp3/.m4a 放进 /mem_fat/audio"); return; }

        auto* aud = drivers::I2sAudio::instance();
        const std::string& cur = _tracks[_cur];

        /* 播放条 */
        char now[56];
        snprintf(now, sizeof(now), "%s%s  音量 %d%%",
                 aud->is_playing() ? "▶" : "■", cur.c_str(), aud->get_volume());
        ui->fill_rect({0, 32, (int16_t)SCREEN_W, 18}, COLOR_DEEP_BLUE);
        ui->draw_text(2, 34, now, COLOR_WHITE);

        /* 曲目列表（可见 8 行） */
        int y = 54;
        int start = std::max(0, _cur - 4);
        for (int i = start; i < (int)_tracks.size() && y < SCREEN_H - 16 - 12; i++, y += 12) {
            std::string line = (i == _cur ? "> " : "  ") + _tracks[i];
            ui->draw_text(2, y, line, i == _cur ? COLOR_YELLOW : COLOR_WHITE);
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
            case 5: _hint = "列表循环 · 队列自动续播"; break;
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
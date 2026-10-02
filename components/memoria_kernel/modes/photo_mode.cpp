/**
 * @file photo_mode.cpp
 * @brief PHOTO 相册模式
 *
 * 从 PrivateFS 列出 Photo 条目，缩略图网格 + 全屏查看 + 幻灯片。
 * 真机图片解码（JPEG）后续接入 esp_jpeg；当前以色块模拟图片显示。
 */

#include "mode_manager.hpp"
#include "private_fs.hpp"

extern "C" {
#include <esp_timer.h>
}

#include <cstdio>
#include <vector>
#include <string>

namespace memoria {
using namespace drivers;
namespace modes {

class PhotoMode : public ModeWindow {
public:
    PhotoMode() { _reload(); }

    void mode_render(window::UIRenderer* ui, bool focused) override {
        /* 幻灯片推进（render_all 约 20ms 调用一次） */
        if (_slide && _view >= 0 && !_names.empty()) {
            uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
            if (now - _slide_last > 3000) {
                _slide_last = now;
                _view = (_view + 1) % _names.size();
            }
        }
        if (_view >= 0) { _render_view(ui); return; }
        _render_grid(ui, focused);
    }

    bool mode_nav(const window::NavInput& ni) override {
        if (_view >= 0) {
            if (ni.dir == window::NavEvent::NavLeft)  { _view = (_view - 1 + (int)_names.size()) % _names.size(); }
            else if (ni.dir == window::NavEvent::NavRight) { _view = (_view + 1) % _names.size(); }
            else if (ni.dir == window::NavEvent::NavEnter) { _view = -1; _slide = false; }
            return true;
        }
        switch (ni.dir) {
            case window::NavEvent::NavUp:    _cur = _cur - 3 >= 0 ? _cur - 3 : _cur; break;
            case window::NavEvent::NavDown:  _cur = _cur + 3 < (int)_names.size() ? _cur + 3 : _cur; break;
            case window::NavEvent::NavLeft:  _cur = _cur > 0 ? _cur - 1 : _cur; break;
            case window::NavEvent::NavRight: _cur = _cur + 1 < (int)_names.size() ? _cur + 1 : _cur; break;
            case window::NavEvent::NavEnter: if (!_names.empty()) { _view = _cur; _slide = false; } break;
            default: return false;
        }
        return true;
    }

    const char* const* fn_labels() override {
        static const char* f[6] = {"查看", "上一", "下一", "信息", "幻灯", "菜单"};
        return f;
    }

    void on_fn(int idx) override {
        switch (idx) {
            case 0: if (_names.empty()) return; _view = _view >= 0 ? _view : _cur; _slide = false; break;
            case 1: if (_view >= 0) _view = (_view - 1 + (int)_names.size()) % _names.size(); break;
            case 2: if (_view >= 0) _view = (_view + 1) % _names.size(); break;
            case 3: _info(); break;
            case 4: if (!_names.empty()) { _view = _view >= 0 ? _view : _cur; _slide = !_slide; _slide_last = esp_timer_get_time() / 1000; } break;
            case 5: _menu(); break;
        }
    }

private:
    void _reload() {
        _names.clear();
        if (fs::PrivateFs::instance()->is_inited()) {
            fs::PrivateFs::instance()->list_by_type(fs::FileType::Photo,
                [this](const fs::Entry& e) { _names.push_back(e.name); return true; });
        }
    }

    /* 文件名 hash → RGB565 色块（模拟缩略图） */
    static uint16_t _color_of(const std::string& n) {
        static const uint16_t pal[8] = {
            rgb565(236, 72, 153), rgb565(34, 211, 238), rgb565(251, 191, 36),
            rgb565(52, 211, 153), rgb565(167, 139, 250), rgb565(248, 113, 113),
            rgb565(96, 165, 250), rgb565(244, 114, 182),
        };
        uint32_t h = 2166136261u;
        for (char c : n) h = (h ^ (uint8_t)c) * 16777619u;
        return pal[h & 7];
    }

    void _render_grid(window::UIRenderer* ui, bool focused) {
        draw_title(ui, "PHOTO 相册", _names.empty() ? "0 张" : std::to_string(_names.size()).c_str());
        if (_names.empty()) { draw_hint(ui, "空相册 · 把照片放进 /mem_fat/photos"); return; }
        const int cols = 3, pad = 6, gap = 4;
        const int cw = (SCREEN_W - pad * 2 - gap * 2) / cols;
        const int ch = 44;
        for (int i = 0; i < (int)_names.size() && i < 3 * 4; i++) {
            int x = pad + (i % cols) * (cw + gap);
            int y = 32 + (i / cols) * (ch + gap);
            window::Rect cell{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(cw), static_cast<int16_t>(ch)};
            ui->fill_rect(cell, _color_of(_names[i]));
            if (i == _cur && focused) ui->draw_cursor(cell, COLOR_WHITE, 2);
            ui->draw_text(x + 2, y + ch - window::UIRenderer::FONT_H - 1,
                          _names[i].substr(0, (cw - 4) / window::UIRenderer::FONT_W), COLOR_WHITE);
        }
        if (!_hint.empty()) draw_hint(ui, _hint.c_str());
        else draw_hint(ui, "Enter 查看 · 左右翻页 · F5 幻灯片");
    }

    void _render_view(window::UIRenderer* ui) {
        if (_names.empty()) return;
        const std::string& n = _names[_view];
        ui->fill_rect({0, 16, (int16_t)SCREEN_W, (int16_t)(SCREEN_H - 16 - 16)}, _color_of(n));
        char tag[48];
        snprintf(tag, sizeof(tag), "%s  %d/%zu%s", n.c_str(), _view + 1, _names.size(), _slide ? " · SLIDE" : "");
        draw_hint(ui, tag);
    }

    void _info() {
        if (_names.empty()) return;
        _hint = "IMG 640x480 · 128KB · 2026-09-25";
    }

    void _menu() {
        _hint = "菜单：排序(时间) · 目录(/mem_fat/photos)";
    }

    std::vector<std::string> _names;
    std::string _hint;
    int _view = -1;
    bool _slide = false;
    uint32_t _slide_last = 0;
};

static std::shared_ptr<window::Window> photo_create() {
    return std::make_shared<PhotoMode>();
}

void photo_mode_register() {
    ModeDesc d{};
    d.id = "photo"; d.name = "PHOTO"; d.cn = "相册";
    d.color = rgb565(236, 72, 153);
    d.create = photo_create;
    ModeManager::instance()->register_mode(d);
}

} // namespace modes
} // namespace memoria
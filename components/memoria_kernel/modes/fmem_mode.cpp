/**
 * @file fmem_mode.cpp
 * @brief FMEM 文件模式（TF 卡文件浏览器）
 *
 * 浏览 /mem_fat（TF 卡）固定目录：photos/notes/audio/video/scripts + 根文件。
 * F1 打开（目录进入 / 文件信息）、F4 删除（确认）、F5 信息。
 */

#include "mode_manager.hpp"
#include "private_fs.hpp"

extern "C" {
#include <dirent.h>
#include <sys/stat.h>
}

#include <cstdio>
#include <cstring>

#include <vector>
#include <string>
#include <algorithm>

namespace memoria {
using namespace drivers;
namespace modes {

struct FsEntry {
    std::string name;
    bool        is_dir;
    uint32_t    size;
};

class FmemMode : public ModeWindow {
public:
    FmemMode() { _cd("/mem_fat"); }

    void mode_render(window::UIRenderer* ui, bool focused) override {
        char head[40];
        snprintf(head, sizeof(head), "FMEM %s", _path.c_str());
        draw_title(ui, head, std::to_string(_entries.size()).append(" 项").c_str());

        if (_entries.empty()) { draw_hint(ui, "空目录 · F4 返回上级"); }
        else if (!_hint.empty()) draw_hint(ui, _hint.c_str());
        int y = 34;
        int start = std::max(0, _cur - 8);
        for (int i = start; i < (int)_entries.size() && y < SCREEN_H - 16 - 12; i++, y += 12) {
            std::string line = (i == _cur ? "> " : "  ") + _entries[i].name + (_entries[i].is_dir ? "/" : "");
            ui->draw_text(2, y, line, i == _cur ? COLOR_YELLOW : COLOR_WHITE);
        }

        /* 存储用量条（PrivateFS 私有区） */
        uint32_t used = 0, total = 0, ue = 0, te = 0;
        if (fs::PrivateFs::instance()->is_inited())
            fs::PrivateFs::instance()->stats(used, total, ue, te);
        int pct = total > 0 ? (int)(used * 100 / total) : 0;
        ui->fill_rect({6, SCREEN_H - 16 - 14, 200, 5}, COLOR_DARK_GRAY);
        ui->fill_rect({6, SCREEN_H - 16 - 14, (int16_t)(200 * pct / 100), 5}, COLOR_GREEN);
        char bar[40];
        /* ue 为私区条目数，原标签误写为"TF N 项"，此处改为私区条目数 */
        snprintf(bar, sizeof(bar), "私区 %d%%  %u 条", pct, (unsigned)ue);
        ui->draw_text(6, SCREEN_H - 16 - 24, bar, COLOR_LIGHT_GRAY);
    }

    bool mode_nav(const window::NavInput& ni) override {
        switch (ni.dir) {
            case window::NavEvent::NavUp:    _cur = std::max(0, _cur - 1); break;
            case window::NavEvent::NavDown:  _cur = std::min((int)_entries.size() - 1, _cur + 1); break;
            case window::NavEvent::NavEnter: _open(); break;
            default: return false;
        }
        return true;
    }

    const char* const* fn_labels() override {
        static const char* f[6] = {"打开", "新建", "上级", "删除", "信息", "菜单"};
        return f;
    }

    void on_fn(int idx) override {
        switch (idx) {
            case 0: _open(); break;
            case 1: _hint = "新建目录（真机：字符表命名）"; break;
            case 2: _up(); break;
            case 3: _del(); break;
            case 4: _info(); break;
            case 5: _hint = "目录：photos/notes/audio/video/scripts"; break;
        }
    }

    void on_confirm_ok() override {
        if (_pending >= 0 && _pending < (int)_entries.size() && !_entries[_pending].is_dir) {
            remove((_path + "/" + _entries[_pending].name).c_str());
        }
        _pending = -1;
        _reload();
    }

private:
    void _cd(const std::string& p) {
        _path = p;
        _reload();
        _cur = 0;
    }

    void _reload() {
        _entries.clear();
        DIR* d = opendir(_path.c_str());
        if (!d) return;
        struct dirent* e;
        while ((e = readdir(d)) != nullptr) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
            std::string full = _path + "/" + e->d_name;
            struct stat st{};
            FsEntry en;
            en.name = e->d_name;
            en.is_dir = (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
            en.size = en.is_dir ? 0 : (uint32_t)st.st_size;
            _entries.push_back(en);
        }
        closedir(d);
        /* 目录排前，其余按名 */
        std::stable_sort(_entries.begin(), _entries.end(),
            [](const FsEntry& a, const FsEntry& b) { return a.is_dir != b.is_dir ? a.is_dir : a.name < b.name; });
        _cur = std::min(_cur, (int)_entries.size() - 1);
    }

    void _open() {
        if (_entries.empty()) return;
        const FsEntry& en = _entries[_cur];
        if (en.is_dir) { _cd(_path + "/" + en.name); }
        else _info();
    }

    void _up() {
        if (_path == "/mem_fat") return;
        size_t pos = _path.rfind('/');
        _cd(pos == std::string::npos ? "/mem_fat" : _path.substr(0, pos));
    }

    void _del() {
        if (_entries.empty() || _entries[_cur].is_dir) { _hint = "仅删除文件"; return; }
        _pending = _cur;
        request_confirm("删除此文件?");
    }

    void _info() {
        if (_entries.empty()) return;
        const FsEntry& en = _entries[_cur];
        char buf[40];
        if (en.is_dir) snprintf(buf, sizeof(buf), "[目录] %s", en.name.c_str());
        else snprintf(buf, sizeof(buf), "[文件] %s · %u B", en.name.c_str(), (unsigned)en.size);
        _hint = buf;
    }

    std::string _path;
    std::vector<FsEntry> _entries;
    std::string _hint;
    int _pending = -1;
};

static std::shared_ptr<window::Window> fmem_create() {
    return std::make_shared<FmemMode>();
}

void fmem_mode_register() {
    ModeDesc d{};
    d.id = "fmem"; d.name = "FMEM"; d.cn = "文件";
    d.color = rgb565(129, 140, 248);
    d.create = fmem_create;
    ModeManager::instance()->register_mode(d);
}

} // namespace modes
} // namespace memoria
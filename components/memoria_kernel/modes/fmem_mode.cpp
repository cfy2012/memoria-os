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
        /* 路径面包屑放标题右侧胶囊（过长截断，避免压住标题） */
        const std::string crumb = window::UIRenderer::clip_text(_path, 168);
        draw_title(ui, "FMEM 文件", crumb.c_str());

        if (_entries.empty()) draw_hint(ui, "空目录 | F4 返回上级");
        else if (!_hint.empty()) draw_hint(ui, _hint.c_str());

        /* 文件列表：卡片行，文件夹黄色图标块 / 文件强调色图标块 */
        const int visible = 8;
        const int n = static_cast<int>(_entries.size());
        int start = _cur - 4;
        if (start > n - visible) start = n - visible;
        if (start < 0) start = 0;

        int16_t y = 34;
        for (int i = start; i < n && i < start + visible; i++, y += 26) {
            const bool sel = (i == _cur);
            const window::Rect row{6, y, 468, 24};
            ui->card(row, 8, sel ? COLOR_CARD_HI : COLOR_CARD,
                     sel ? COLOR_CARD_HI : COLOR_STROKE);
            if (sel) ui->draw_round_rect(row, 8, _accent, 2);

            ui->fill_round_rect({12, static_cast<int16_t>(y + 3), 18, 18}, 6,
                                _entries[i].is_dir ? COLOR_WARN : _accent);

            const std::string name = _entries[i].name + (_entries[i].is_dir ? "/" : "");
            const std::string right = _entries[i].is_dir ? std::string("目录")
                                                         : _size_text(_entries[i].size);
            const int rw = window::UIRenderer::text_width_utf8(right);
            ui->draw_text_fit(38, static_cast<int16_t>(y + 6), name, COLOR_TEXT, 422 - rw);
            ui->draw_text(static_cast<int16_t>(466 - rw), static_cast<int16_t>(y + 6),
                          right, COLOR_TEXT_DIM);
        }

        /* 私有区用量卡片 */
        uint32_t used = 0, total = 0, ue = 0, te = 0;
        if (fs::PrivateFs::instance()->is_inited())
            fs::PrivateFs::instance()->stats(used, total, ue, te);
        const int pct = total > 0 ? static_cast<int>(used * 100 / total) : 0;

        ui->card({6, 248, 468, 34}, 8, COLOR_CARD, COLOR_STROKE);
        char bar[48];
        /* ue 为私区条目数，原标签误写为"TF N 项"，此处改为私区条目数 */
        snprintf(bar, sizeof(bar), "私区 %d%%  %u 条", pct, (unsigned)ue);
        ui->draw_text(14, 252, bar, COLOR_TEXT_DIM);
        ui->progress({14, 268, 452, 6}, pct * 10, _accent, COLOR_BG_SOFT, 3);
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
    /* 文件大小文本：<1KB 用 B，否则 KB/MB（保留一位小数） */
    static std::string _size_text(uint32_t n) {
        char b[24];
        if (n < 1024) {
            snprintf(b, sizeof(b), "%u B", (unsigned)n);
        } else if (n < 1024u * 1024u) {
            snprintf(b, sizeof(b), "%u.%u KB",
                     (unsigned)(n / 1024), (unsigned)((n % 1024) * 10 / 1024));
        } else {
            snprintf(b, sizeof(b), "%u.%u MB",
                     (unsigned)(n / (1024u * 1024u)),
                     (unsigned)((n % (1024u * 1024u)) * 10 / (1024u * 1024u)));
        }
        return std::string(b);
    }

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
        else snprintf(buf, sizeof(buf), "[文件] %s | %u B", en.name.c_str(), (unsigned)en.size);
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
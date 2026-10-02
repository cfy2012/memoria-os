/**
 * @file notes_mode.cpp
 * @brief NOTES 记事模式
 *
 * 从 PrivateFS 列出 Note 条目，查看全文 / 新建 / 删除。
 * 真机文本输入走后续字符表方案，当前"新建"写入一条示例笔记。
 */

#include "mode_manager.hpp"
#include "private_fs.hpp"

#include <cstdio>
#include <cstring>

#include <vector>
#include <string>
#include <algorithm>

namespace memoria {
using namespace drivers;
namespace modes {

class NotesMode : public ModeWindow {
public:
    NotesMode() { _reload(); }

    void mode_render(window::UIRenderer* ui, bool focused) override {
        if (_view >= 0) { _render_note(ui); return; }
        draw_title(ui, "NOTES 记事", _names.empty() ? "0 篇" : std::to_string(_names.size()).c_str());
        if (_names.empty()) { draw_hint(ui, "空 · F2 新建一条"); return; }
        int y = 34;
        int start = std::max(0, _cur - 8);
        for (int i = start; i < (int)_names.size() && y < SCREEN_H - 16 - 12; i++, y += 12) {
            std::string line = (i == _cur ? "> " : "  ") + _names[i];
            ui->draw_text(2, y, line, i == _cur ? COLOR_YELLOW : COLOR_WHITE);
        }
        if (!_hint.empty()) draw_hint(ui, _hint.c_str());
    }

    bool mode_nav(const window::NavInput& ni) override {
        if (_view >= 0) {
            if (ni.dir == window::NavEvent::NavUp) _view = _view > 0 ? _view - 1 : _view;
            else if (ni.dir == window::NavEvent::NavDown) _view = std::min((int)_names.size() - 1, _view + 1);
            else if (ni.dir == window::NavEvent::NavEnter) { _view = -1; }
            return true;
        }
        switch (ni.dir) {
            case window::NavEvent::NavUp:    _cur = std::max(0, _cur - 1); break;
            case window::NavEvent::NavDown:  _cur = std::min((int)_names.size() - 1, _cur + 1); break;
            case window::NavEvent::NavEnter: if (!_names.empty()) _view = _cur; break;
            default: return false;
        }
        return true;
    }

    const char* const* fn_labels() override {
        static const char* f[6] = {"查看", "新建", "编辑", "删除", "翻页", "菜单"};
        return f;
    }

    void on_fn(int idx) override {
        switch (idx) {
            case 0: if (!_names.empty()) _view = _view >= 0 ? _view : _cur; break;
            case 1: _create(); break;
            case 2: _hint = _view >= 0 ? "已保存（真机：字符表编辑）" : "先 Enter 进入查看再编辑"; break;
            case 3: _remove(); break;
            case 4: if (_view >= 0) _view = std::min((int)_names.size() - 1, _view + 1); break;
            case 5: _hint = "排序：日期倒序 · 导出 txt 到 TF 卡"; break;
        }
    }

private:
    void _reload() {
        _names.clear();
        if (fs::PrivateFs::instance()->is_inited()) {
            fs::PrivateFs::instance()->list_by_type(fs::FileType::Note,
                [this](const fs::Entry& e) { _names.push_back(e.name); return true; });
        }
    }

    void _render_note(window::UIRenderer* ui) {
        char head[48];
        snprintf(head, sizeof(head), "%s  %d/%zu", _names[_view].c_str(), _view + 1, _names.size());
        draw_title(ui, head, "Esc 返回");
        /* 从 PrivateFS 读取当前笔记正文（上限 8KB，防异常大文件占内存） */
        std::string body;
        auto* pfs = fs::PrivateFs::instance();
        if (pfs->is_inited()) {
            fs::Entry* e = pfs->find_by_name(_names[_view]);
            if (e && e->size > 0) {
                fs::FileHandle h;
                if (pfs->open(e->id, h, false) == ESP_OK) {
                    uint32_t rd = std::min(e->size, (uint32_t)8192);
                    body.resize(rd);
                    if (pfs->read(h, &body[0], rd) < 0) body.clear();
                    pfs->close(h);
                }
            }
        }
        if (body.empty()) body = "（无内容）";
        int y = 34, x = 2, i = 0;
        while (i < (int)body.size() && y < SCREEN_H - 16 - 12) {
            int take = std::min((int)(SCREEN_W - 4) / window::UIRenderer::FONT_W, (int)body.size() - i);
            ui->draw_text(x, y, body.substr(i, take), COLOR_WHITE);
            i += take; y += 12;
        }
    }

    void _create() {
        uint32_t used = 0, total = 0, ue = 0, te = 0;
        fs::PrivateFs::instance()->stats(used, total, ue, te);
        std::string title = "note_" + std::to_string(ue + 1);
        std::string body = "今天在操场拍了很多照片，风很舒服。";
        fs::Entry* out = nullptr;
        fs::PrivateFs::instance()->create(fs::FileType::Note, title, (uint32_t)body.size(), &out);
        if (out) {
            fs::FileHandle h;
            if (fs::PrivateFs::instance()->open(out->id, h, true) == ESP_OK) {
                fs::PrivateFs::instance()->write(h, body.data(), body.size());
                fs::PrivateFs::instance()->close(h);
            }
        }
        _reload();
        if (!_names.empty()) _cur = (int)_names.size() - 1;
    }

    void _remove() {
        if (_names.empty()) return;
        const int i = _view >= 0 ? _view : _cur;
        request_confirm("删除这条记事?");
        _pending_del = i;
    }

    void on_confirm_ok() override {
        if (_pending_del < 0) return;
        if (fs::PrivateFs::instance()->is_inited()) {
            fs::PrivateFs::instance()->list_by_type(fs::FileType::Note,
                [this](const fs::Entry& e) {
                    if (_pending_del == 0) { fs::PrivateFs::instance()->remove(e.id); return false; }
                    _pending_del--;
                    return true;
                });
        }
        _view = -1; _pending_del = -1;
        _reload();
        _cur = std::max(0, std::min(_cur, (int)_names.size() - 1));
    }

    std::vector<std::string> _names;
    std::string _hint;
    int _view = -1;
    int _pending_del = -1;
};

static std::shared_ptr<window::Window> notes_create() {
    return std::make_shared<NotesMode>();
}

void notes_mode_register() {
    ModeDesc d{};
    d.id = "notes"; d.name = "NOTES"; d.cn = "记事";
    d.color = rgb565(167, 139, 250);
    d.create = notes_create;
    ModeManager::instance()->register_mode(d);
}

} // namespace modes
} // namespace memoria
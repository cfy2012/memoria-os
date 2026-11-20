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
#include <ctime>

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
        draw_title(ui, "NOTES 记事",
                   _names.empty() ? "0 篇" : (std::to_string(_names.size()) + " 篇").c_str());
        if (_names.empty()) { draw_hint(ui, "空 | F2 新建一条"); return; }
        _render_list(ui);
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
            case 5: _hint = "排序：日期倒序 | 导出 txt 到 TF 卡"; break;
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

    /* 条目时间文本 MM-DD HH:MM；RTC 未同步（1970 等）时显示 -- */
    std::string _time_text(int i) const {
        if (i < 0 || i >= static_cast<int>(_names.size())) return std::string("--");
        auto* pfs = fs::PrivateFs::instance();
        fs::Entry* e = pfs->is_inited() ? pfs->find_by_name(_names[i]) : nullptr;
        if (!e || e->created_at < 1000000000u) return std::string("--");
        const time_t t = static_cast<time_t>(e->created_at);
        struct tm tmv{};
        localtime_r(&t, &tmv);
        char buf[24];
        snprintf(buf, sizeof(buf), "%02d-%02d %02d:%02d",
                 tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
        return std::string(buf);
    }

    /* UTF-8 按像素宽度折行（ASCII 8px / 汉字 16px），最多保留 16 行 */
    static std::vector<std::string> _wrap_utf8(const std::string& s, int max_px) {
        std::vector<std::string> lines;
        std::string line;
        int w = 0;
        size_t i = 0;
        while (i < s.size() && lines.size() < 16) {
            const uint8_t b0 = static_cast<uint8_t>(s[i]);
            if (b0 == '\n') { lines.push_back(line); line.clear(); w = 0; i++; continue; }
            if (b0 == '\r') { i++; continue; }
            size_t len = 1;
            int cw = window::UIRenderer::FONT_W;
            if (b0 >= 0x80) {
                if ((b0 & 0xE0) == 0xC0)      len = 2;
                else if ((b0 & 0xF0) == 0xE0) len = 3;
                else if ((b0 & 0xF8) == 0xF0) len = 4;
                cw = window::UIRenderer::CJK_W;
            }
            if (i + len > s.size()) break;
            if (w + cw > max_px && !line.empty()) {
                lines.push_back(line);
                line.clear();
                w = 0;
                continue;                  /* 换行后重新判断当前字符 */
            }
            line.append(s, i, len);
            w += cw;
            i += len;
        }
        if (!line.empty() && lines.size() < 16) lines.push_back(line);
        if (lines.empty()) lines.push_back(std::string());
        return lines;
    }

    /* 列表页：左侧卡片行列表 + 右侧预览卡片 */
    void _render_list(window::UIRenderer* ui) {
        const int16_t LW = 196;
        const int n = static_cast<int>(_names.size());
        int start = _cur - 3;
        if (start > n - 7) start = n - 7;
        if (start < 0) start = 0;

        int16_t y = 34;
        for (int i = start; i < n && i < start + 7; i++, y += 30) {
            const bool sel = (i == _cur);
            const window::Rect row{6, y, LW, 26};
            ui->card(row, 8, sel ? COLOR_CARD_HI : COLOR_CARD,
                     sel ? COLOR_CARD_HI : COLOR_STROKE);
            if (sel) ui->draw_round_rect(row, 8, _accent, 2);

            /* 左侧小图标块（单字 N 表示笔记） */
            ui->fill_round_rect({12, static_cast<int16_t>(y + 5), 16, 16}, 5,
                                sel ? _accent : COLOR_BG_SOFT);
            ui->draw_text(18, static_cast<int16_t>(y + 7), "N",
                          sel ? COLOR_BG : COLOR_TEXT_DIM);

            ui->draw_text_fit(34, static_cast<int16_t>(y + 2), _names[i], COLOR_TEXT, 156);
            ui->draw_text_fit(34, static_cast<int16_t>(y + 13), _time_text(i), COLOR_TEXT_DIM, 156);
        }

        /* 右侧预览面板 */
        const window::Rect panel{208, 34, 266, 206};
        ui->card(panel, 8, COLOR_CARD, COLOR_STROKE);

        ui->draw_text(218, 42, "笔记预览", COLOR_TEXT_DIM);
        const std::string cnt = "共 " + std::to_string(_names.size()) + " 篇";
        ui->draw_text(static_cast<int16_t>(466 - window::UIRenderer::text_width_utf8(cnt)),
                      42, cnt, COLOR_TEXT_DIM);
        ui->divider_h(218, 60, 248, COLOR_STROKE);

        ui->fill_round_rect({218, 72, 48, 48}, 10, COLOR_BG_SOFT);
        ui->draw_round_rect({218, 72, 48, 48}, 10, _accent, 1);
        ui->draw_text(238, 90, "N", _accent);

        ui->draw_text_fit(278, 74, _names[_cur], COLOR_TEXT, 188);
        ui->draw_text_fit(278, 92, _time_text(_cur), COLOR_TEXT_DIM, 188);
        const std::string pos = "第 " + std::to_string(_cur + 1) + " / " +
                                std::to_string(_names.size()) + " 篇";
        ui->draw_text_fit(278, 110, pos, COLOR_TEXT_DIM, 188);

        ui->divider_h(218, 132, 248, COLOR_STROKE);
        ui->draw_text(218, 142, "Enter 查看全文", COLOR_TEXT);
        ui->draw_text(218, 160, "F2 新建 | F4 删除", COLOR_TEXT_DIM);
        ui->draw_text(218, 178, "F3 编辑 | F6 导出", COLOR_TEXT_DIM);
        ui->draw_text(218, 196, "上下键切换条目", COLOR_TEXT_DIM);
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
        if (body.empty()) body = "无内容";

        /* 正文卡片：标题行 + 分隔线 + UTF-8 折行正文 */
        ui->card({6, 34, 468, 240}, 8, COLOR_CARD, COLOR_STROKE);
        ui->draw_text_fit(18, 42, _names[_view], COLOR_TEXT, 300);
        const std::string t = _time_text(_view);
        ui->draw_text(static_cast<int16_t>(466 - window::UIRenderer::text_width_utf8(t)),
                      42, t, COLOR_TEXT_DIM);
        ui->divider_h(18, 60, 444, COLOR_STROKE);

        const std::vector<std::string> lines = _wrap_utf8(body, 444);
        int16_t y = 68;
        for (const std::string& ln : lines) {
            if (y + window::UIRenderer::CJK_H > 266) break;
            ui->draw_text(18, y, ln, COLOR_TEXT);
            y += window::UIRenderer::CJK_H;
        }
    }

    void _create() {
        uint32_t used = 0, total = 0, ue = 0, te = 0;
        fs::PrivateFs::instance()->stats(used, total, ue, te);
        std::string title = "note_" + std::to_string(ue + 1);
        std::string body = "欢迎使用 Memoria OS。本机可记事、拍照、听歌、运行 BASIC 程序。按 Fn 键新建或删除记事，方向键翻页查看。";
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
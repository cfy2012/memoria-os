/**
 * @file mode_manager.cpp
 * @brief 模式管理器实现：主菜单网格 + 功能键行 + 导航
 */

#include "mode_manager.hpp"
#include "desktop_manager.hpp"
#include "app_mode.hpp"
#include "ili9341.hpp"

extern "C" {
#include <esp_log.h>
}

#include <cstdio>
#include <cstring>

#include <string>
#include <vector>

namespace memoria {
using namespace drivers;
namespace modes {

static const char* TAG = "MODE";

/* ============================================================
 *  ModeManager
 * ============================================================ */
ModeManager* ModeManager::instance() {
    static ModeManager inst;
    return &inst;
}

void ModeManager::register_mode(const ModeDesc& d) {
    if (!d.id || !d.name || !d.create) { ESP_LOGW(TAG, "bad mode desc"); return; }
    _modes.push_back(d);
    ESP_LOGI(TAG, "mode [%zu] '%s' %s", _modes.size(), d.id, d.name);
}

const ModeDesc* ModeManager::get(int i) const {
    return (i >= 0 && i < static_cast<int>(_modes.size())) ? &_modes[i] : nullptr;
}

int ModeManager::index_of(const char* id) const {
    if (!id) return -1;
    for (int i = 0; i < static_cast<int>(_modes.size()); i++) {
        if (_modes[i].id && std::strcmp(_modes[i].id, id) == 0) return i;
    }
    return -1;
}

bool ModeManager::enter_by_id(const char* id) {
    int i = index_of(id);
    if (i < 0) return false;
    enter(i);
    return true;
}

void ModeManager::boot() {
    auto* wm = window::WindowManager::instance();
    _launcher = std::make_shared<LauncherWindow>();
    _launcher->set_rect({0, 16, static_cast<int16_t>(SCREEN_W), static_cast<int16_t>(SCREEN_H - 16)});
    wm->push_top(_launcher);
    ESP_LOGI(TAG, "boot: %d modes registered", static_cast<int>(_modes.size()));
}

void ModeManager::enter(int index) {
    const ModeDesc* d = get(index);
    if (!d) return;
    auto* wm = window::WindowManager::instance();
    if (_current) { wm->pop_top(); _current.reset(); }
    _current = d->create();
    if (!_current) { ESP_LOGW(TAG, "'%s' create failed", d->id); return; }
    _current->set_rect({0, 16, static_cast<int16_t>(SCREEN_W), static_cast<int16_t>(SCREEN_H - 16)});
    wm->push_top(_current);
    ESP_LOGI(TAG, "enter mode '%s'", d->id);
}

void ModeManager::back_to_menu() {
    auto* wm = window::WindowManager::instance();
    if (_current) { wm->pop_top(); _current.reset(); }
    if (_launcher) wm->push_top(_launcher);
}

bool ModeManager::enter_app(const std::string& bas_path) {
    auto* wm = window::WindowManager::instance();
    if (_current) { wm->pop_top(); _current.reset(); }
    _current = std::make_shared<AppRunWindow>(bas_path);
    _current->set_rect({0, 16, static_cast<int16_t>(SCREEN_W), static_cast<int16_t>(SCREEN_H - 16)});
    wm->push_top(_current);
    ESP_LOGI(TAG, "enter app '%s'", bas_path.c_str());
    return true;
}

bool ModeManager::dispatch_key(uint16_t key, bool pressed) {
    if (!_current || _current == _launcher) return false;
    /* _current 只会是 ModeWindow 子类（enter() 时由 d->create() 创建） */
    auto* mw = static_cast<ModeWindow*>(_current.get());
    return mw->on_key(key, pressed);
}

/* ============================================================
 *  LauncherWindow：安卓式可编辑桌面（3×4 网格 + 文件夹）
 * ============================================================ */
LauncherWindow::LauncherWindow() {
    _rect = {0, 16, static_cast<int16_t>(SCREEN_W), static_cast<int16_t>(SCREEN_H - 16)};
    set_rect(_rect);
    DesktopManager::instance()->load();
}

void LauncherWindow::on_render() {
    auto* ui = window::UIRenderer::instance();
    auto* dm = DesktopManager::instance();

    /* APP 直启：周期扫描 TF 卡 .bas 自动上桌面（首帧 + 每 100 帧 ≈2s） */
    {
        static uint32_t s_frame = 0;
        if ((s_frame++ % 100) == 0) dm->scan_bas_apps();
    }

    ui->fill_rect({0, 16, static_cast<int16_t>(SCREEN_W), static_cast<int16_t>(SCREEN_H - 16)}, COLOR_BLACK);

    const bool edit = dm->is_edit();
    const int COLS = 3, ROWS = 4;
    const int pad = 6, gap = 4;
    const int area_w = SCREEN_W - pad * 2 - gap * (COLS - 1);
    /* 编辑模式底部留 14px 提示条 */
    const int area_h = (SCREEN_H - 16) - pad * 2 - gap * (ROWS - 1) - (edit ? 14 : 0);
    const int cell_w = area_w / COLS;
    const int cell_h = area_h / ROWS;

    auto* mm = ModeManager::instance();
    const auto& cells = dm->cells();
    if (cells.empty()) return; /* 桌面未铺保底时拒绝越界读（双保险） */
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            int idx = r * COLS + c;
            int x = pad + c * (cell_w + gap);
            int y = 16 + pad + r * (cell_h + gap);
            if (idx >= (int)cells.size()) continue;
            const Cell& cell = cells[idx];

            window::Rect rc{static_cast<int16_t>(x), static_cast<int16_t>(y),
                            static_cast<int16_t>(cell_w), static_cast<int16_t>(cell_h)};

            if (cell.type == CellType::Empty) {
                ui->draw_rect(rc, COLOR_DARK_GRAY, 1);
            } else if (cell.type == CellType::App) {
                int mi = mm->index_of(cell.app_id.c_str());
                const ModeDesc* d = mi >= 0 ? mm->get(mi) : nullptr;
                uint16_t color = d ? d->color : rgb565(90, 90, 90);
                ui->fill_rect(rc, color);
                ui->fill_rect({static_cast<int16_t>(x), static_cast<int16_t>(y),
                               static_cast<int16_t>(cell_w), 3}, COLOR_WHITE);
                std::string name = d ? d->name : cell.app_id;
                int tw = window::UIRenderer::text_width_utf8(name);
                ui->draw_text(x + (cell_w - tw) / 2, y + (cell_h - window::UIRenderer::FONT_H) / 2 - 3,
                              name, COLOR_BLACK);
                char num[8]; snprintf(num, sizeof(num), "%02d", idx + 1);
                ui->draw_text(x + 2, y + 1, num, COLOR_BLACK);
            } else if (cell.type == CellType::BasApp) {
                /* BASIC APP：文件名哈希取色瓷砖，名字去 .bas */
                uint32_t hsh = 2166136261u;
                for (char ch : cell.app_id) { hsh ^= (uint8_t)ch; hsh *= 16777619u; }
                static const uint16_t kPal[8] = {
                    rgb565(59, 130, 246),  rgb565(16, 185, 129), rgb565(245, 158, 11),
                    rgb565(239, 68, 68),   rgb565(139, 92, 246), rgb565(236, 72, 153),
                    rgb565(20, 184, 166),  rgb565(132, 204, 22),
                };
                uint16_t color = kPal[(hsh >> 8) & 7];
                ui->fill_rect(rc, color);
                ui->fill_rect({static_cast<int16_t>(x), static_cast<int16_t>(y),
                               static_cast<int16_t>(cell_w), 3}, COLOR_WHITE);
                std::string name = cell.app_id;
                if (name.size() > 4 && name.compare(name.size() - 4, 4, ".bas") == 0)
                    name = name.substr(0, name.size() - 4);
                int tw = window::UIRenderer::text_width_utf8(name);
                ui->draw_text(x + (cell_w - tw) / 2, y + (cell_h - window::UIRenderer::FONT_H) / 2 - 3,
                              name, COLOR_BLACK);
                char num2[8]; snprintf(num2, sizeof(num2), "%02d", idx + 1);
                ui->draw_text(x + 2, y + 1, num2, COLOR_BLACK);
            } else if (cell.type == CellType::Folder) {
                ui->fill_rect(rc, rgb565(250, 204, 21));
                ui->fill_rect({static_cast<int16_t>(x), static_cast<int16_t>(y),
                               static_cast<int16_t>(cell_w), 3}, COLOR_WHITE);
                std::string name = dm->folder_name(cell.folder_id);
                int tw = window::UIRenderer::text_width_utf8(name);
                ui->draw_text(x + (cell_w - tw) / 2, y + (cell_h - window::UIRenderer::FONT_H) / 2 - 3,
                              name, COLOR_BLACK);
                char num[8]; snprintf(num, sizeof(num), "%02d", idx + 1);
                ui->draw_text(x + 2, y + 1, num, COLOR_BLACK);
            }

            /* 光标 */
            if (idx == _sel) {
                ui->draw_cursor(rc, edit ? COLOR_YELLOW : COLOR_WHITE, 2);
            }
            /* 拿起标记：大 X（驱动层画线） */
            if (edit && idx == dm->picked()) {
                auto* lcd = Ili9341::instance();
                lcd->draw_line((uint16_t)(x + 2), (uint16_t)(y + 2),
                               (uint16_t)(x + cell_w - 2), (uint16_t)(y + cell_h - 2), COLOR_RED);
                lcd->draw_line((uint16_t)(x + cell_w - 2), (uint16_t)(y + 2),
                               (uint16_t)(x + 2), (uint16_t)(y + cell_h - 2), COLOR_RED);
            }
        }
    }

    /* 编辑模式底部提示条 */
    if (edit) {
        std::string hint = "编辑";
        if (dm->in_folder()) { hint += " " + dm->folder_name(dm->current_folder()); }
        hint += " · Enter拿起/放下 · 空格建夹 · 长按退出";
        ui->fill_rect({0, SCREEN_H - 16, static_cast<int16_t>(SCREEN_W), 14}, COLOR_DARK_GRAY);
        ui->draw_text(2, SCREEN_H - 15, hint, COLOR_YELLOW);
    }
}

bool LauncherWindow::on_nav(const window::NavInput& ni) {
    auto* dm = DesktopManager::instance();
    auto* mm = ModeManager::instance();
    const bool edit = dm->is_edit();

    if (ni.dir == window::NavEvent::NavBack) {
        if (edit) {
            if (dm->picked() >= 0) dm->cancel_pick();
            else if (dm->in_folder()) dm->go_up();
            else dm->set_edit(false);
        } else {
            if (dm->in_folder()) dm->go_up();
            else return false;
        }
        return true;
    }

    if (ni.dir == window::NavEvent::NavEnter) {
        const auto& cells = dm->cells();
        if (_sel < 0 || _sel >= (int)cells.size()) return true;
        const Cell& cell = cells[_sel];

        if (edit) {
            if (cell.type == CellType::App || cell.type == CellType::BasApp) {
                if (dm->picked() < 0) dm->move_pick(_sel);
                else dm->move_drop(_sel);
            } else if (cell.type == CellType::Folder) {
                if (dm->picked() >= 0) dm->move_drop(_sel);   /* 放入文件夹 */
                else dm->enter_folder(_sel);
            } else {
                if (dm->picked() >= 0) dm->move_drop(_sel);   /* 移到空位 */
                else dm->create_folder(_sel);                 /* 空位新建文件夹 */
            }
            return true;
        }

        if (cell.type == CellType::App) {
            int mi = mm->index_of(cell.app_id.c_str());
            if (mi >= 0) mm->enter(mi);
        } else if (cell.type == CellType::BasApp) {
            /* APP 直启：桌面图标直达，跳过 PRGM（用户拍板 2026-10-06） */
            mm->enter_app("/mem_fat/scripts/" + cell.app_id);
        } else if (cell.type == CellType::Folder) {
            dm->enter_folder(_sel);
        }
        return true;
    }

    switch (ni.dir) {
        case window::NavEvent::NavUp:    _sel = _sel - 3 >= 0 ? _sel - 3 : _sel; break;
        case window::NavEvent::NavDown:  _sel = _sel + 3 < 12 ? _sel + 3 : _sel; break;
        case window::NavEvent::NavLeft:  _sel = (_sel - 1 + 12) % 12; break;
        case window::NavEvent::NavRight: _sel = (_sel + 1) % 12; break;
        default: return false;
    }
    return true;
}

/* ============================================================
 *  ModeWindow：模式窗口基类
 * ============================================================ */
ModeWindow::ModeWindow() {
    _rect = {0, 16, static_cast<int16_t>(SCREEN_W), static_cast<int16_t>(SCREEN_H - 16)};
    set_rect(_rect);
}

void ModeWindow::on_render() {
    auto* ui = window::UIRenderer::instance();
    ui->fill_rect(_rect, COLOR_BLACK);
    if (_confirm >= 0) {
        mode_render(ui, false);   /* 内容变暗（不画焦点） */
        draw_confirm(ui);
        return;
    }
    mode_render(ui, true);
    draw_fn_bar(ui);
}

bool ModeWindow::on_nav(const window::NavInput& ni) {
    if (_confirm >= 0) {
        if (ni.dir == window::NavEvent::NavBack) { _confirm = -1; return true; }
        if (ni.dir == window::NavEvent::NavLeft || ni.dir == window::NavEvent::NavRight) {
            _confirm = 1 - _confirm;   /* 切换 确认/取消 高亮 */
        } else if (ni.dir == window::NavEvent::NavEnter) {
            if (_confirm == 1) { _confirm = -1; on_confirm_ok(); }
            else _confirm = -1;
        }
        return true;
    }
    if (ni.dir == window::NavEvent::NavBack) {
        ModeManager::instance()->back_to_menu();
        return true;
    }
    return mode_nav(ni);
}

void ModeWindow::request_confirm(const char* text) {
    _confirm_text = text;
    _confirm = 1;
}

void ModeWindow::draw_confirm(window::UIRenderer* ui) {
    /* 半透明遮罩 + 居中确认框 */
    window::Rect box{SCREEN_W / 2 - 80, 16 + 60, 160, 60};
    ui->fill_rect(box, COLOR_DARK_GRAY);
    ui->draw_rect(box, COLOR_WHITE, 1);
    if (_confirm_text) {
        int tw = static_cast<int>(strlen(_confirm_text)) * window::UIRenderer::FONT_W;
        ui->draw_text(SCREEN_W / 2 - tw / 2, box.y + 6, _confirm_text, COLOR_WHITE);
    }
    /* 确认 / 取消 */
    const char* yes = "确认";
    const char* no  = "取消";
    ui->draw_text(box.x + 14, box.y + 34, yes, _confirm == 1 ? COLOR_YELLOW : COLOR_LIGHT_GRAY);
    ui->draw_text(box.x + 96, box.y + 34, no,  _confirm == 0 ? COLOR_YELLOW : COLOR_LIGHT_GRAY);
    if (_confirm == 1) ui->draw_text(box.x + 12, box.y + 34, ">", COLOR_YELLOW);
    else ui->draw_text(box.x + 94, box.y + 34, ">", COLOR_YELLOW);
}

void ModeWindow::draw_fn_bar(window::UIRenderer* ui) {
    /* 底部功能键行：y 208..224，6 键均分 */
    const int y = SCREEN_H - 16;
    const int h = 16;
    ui->fill_rect({0, y, static_cast<int16_t>(SCREEN_W), h}, COLOR_DARK_GRAY);
    ui->fill_rect({0, y, static_cast<int16_t>(SCREEN_W), 1}, COLOR_LIGHT_GRAY);

    const char* const* labels = fn_labels();
    const int cell_w = SCREEN_W / 6;
    for (int i = 0; i < 6; i++) {
        const char* lab = labels ? labels[i] : "";
        if (!lab || !*lab) continue;
        std::string text = std::string("F") + char('1' + i) + " " + lab;
        int x = i * cell_w + 2;
        /* 标签超宽时截短显示 */
        int max_chars = (cell_w - 4) / window::UIRenderer::FONT_W;
        if ((int)text.size() > max_chars) text = text.substr(0, max_chars);
        ui->draw_text(x, y + 2, text, COLOR_WHITE);
    }
}

void ModeWindow::draw_title(window::UIRenderer* ui, const char* title, const char* right) {
    ui->fill_rect({0, 16, static_cast<int16_t>(SCREEN_W), 16}, COLOR_DEEP_BLUE);
    ui->draw_text(2, 18, title ? title : "", COLOR_WHITE);
    if (right && *right) {
        int rw = static_cast<int>(strlen(right)) * window::UIRenderer::FONT_W;
        ui->draw_text(SCREEN_W - rw - 4, 18, right, COLOR_LIGHT_GRAY);
    }
}

void ModeWindow::draw_hint(window::UIRenderer* ui, const char* text) {
    if (!text || !*text) return;
    int rw = static_cast<int>(strlen(text)) * window::UIRenderer::FONT_W;
    ui->draw_text((SCREEN_W - rw) / 2, SCREEN_H - 16 - window::UIRenderer::FONT_H - 4,
                  text, COLOR_LIGHT_GRAY);
}

} // namespace modes
} // namespace memoria
/**
 * @file mode_manager.cpp
 * @brief 模式管理器实现：主菜单网格 + 功能键行 + 导航
 */

#include "mode_manager.hpp"
#include "desktop_manager.hpp"
#include "app_mode.hpp"
#include "keyboard_keymap.hpp"
#include "ili9341.hpp"

extern "C" {
#include <esp_log.h>
#include <nvs_flash.h>
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

    /* 开机第一屏：锁屏计算器（全屏盖住状态栏），Launcher 暂不压栈，
     * 密码通过后由 unlock() 切过去 */
    _lock = create_lock_calc();
    if (_lock) {
        _lock->set_rect({0, 0, static_cast<int16_t>(SCREEN_W), static_cast<int16_t>(SCREEN_H)});
        wm->push_top(_lock);
        _lock_active = true;
        ESP_LOGI(TAG, "boot: lock calculator up, %d modes registered",
                 static_cast<int>(_modes.size()));
    } else {
        wm->push_top(_launcher);
        ESP_LOGI(TAG, "boot: %d modes registered", static_cast<int>(_modes.size()));
    }
}

void ModeManager::unlock() {
    if (!_lock_active) return;
    _lock_active = false;          /* 之后键盘不再发给锁屏（对象保留，避免回调中析构自身） */
    auto* wm = window::WindowManager::instance();
    wm->push_top(_launcher);       /* push_top 会替换掉栈顶的锁屏窗口 */
    ESP_LOGI(TAG, "unlock: desktop up");
}

/* ---------------- 开机密码存取 ---------------- */

static const char* PWD_NS      = "settings";
static const char* PWD_KEY     = "lock_pw";
static const char* PWD_DEFAULT = "20120201";

std::string ModeManager::login_password() const {
    nvs_handle_t h;
    if (nvs_open(PWD_NS, NVS_READONLY, &h) == ESP_OK) {
        char buf[24] = {0};
        size_t len = sizeof(buf);
        const esp_err_t e = nvs_get_str(h, PWD_KEY, buf, &len);
        nvs_close(h);
        if (e == ESP_OK && len > 1 && buf[0] != 0) return std::string(buf);
    }
    return std::string(PWD_DEFAULT);
}

bool ModeManager::set_login_password(const std::string& p) {
    if (p.size() != 8) return false;
    for (char c : p)
        if (c < '0' || c > '9') return false;

    nvs_handle_t h;
    if (nvs_open(PWD_NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "set password: nvs_open failed");
        return false;
    }
    bool ok = (nvs_set_str(h, PWD_KEY, p.c_str()) == ESP_OK);
    if (ok) ok = (nvs_commit(h) == ESP_OK);
    nvs_close(h);
    ESP_LOGI(TAG, "set password: %s", ok ? "ok" : "fail");
    return ok;
}

void ModeManager::enter(int index) {
    const ModeDesc* d = get(index);
    if (!d) return;
    auto* wm = window::WindowManager::instance();
    if (_current) { wm->pop_top(); _current.reset(); }
    _current = d->create();
    if (!_current) { ESP_LOGW(TAG, "'%s' create failed", d->id); return; }
    static_cast<ModeWindow*>(_current.get())->set_accent(d->color);
    _current->set_rect({0, 16, static_cast<int16_t>(SCREEN_W), static_cast<int16_t>(SCREEN_H - 16)});
    wm->push_top(_current);
    ESP_LOGI(TAG, "enter mode '%s'", d->id);
}

void ModeManager::back_to_menu() {
    if (_lock_active) return;      /* 锁屏期间不允许回桌面 */
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
    /* 锁屏期间键盘只进锁屏计算器（Launcher / 模式都不可用） */
    if (_lock_active && _lock) {
        auto* lw = static_cast<ModeWindow*>(_lock.get());
        return lw->on_key(key, pressed);
    }
    if (!_current || _current == _launcher) return false;
    /* _current 只会是 ModeWindow 子类（enter() 时由 d->create() 创建） */
    auto* mw = static_cast<ModeWindow*>(_current.get());
    /* 物理 F1～F6 = 底部功能键 1～6 */
    if (pressed && key >= input::K_F1 && key <= input::K_F6) {
        mw->on_fn(static_cast<int>(key - input::K_F1));
        return true;
    }
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

/* 色块亮度判定：浅色块配深色字，深色块配白字 */
static bool _tint_light(uint16_t c) {
    const int r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    return (r * 2 + g * 4 + b) > 96;
}

/* 取 UTF-8 首个字符（图标块里显示的单字） */
static std::string _first_glyph(const std::string& s) {
    if (s.empty()) return std::string("?");
    const uint8_t b0 = static_cast<uint8_t>(s[0]);
    size_t len = 1;
    if ((b0 & 0xE0) == 0xC0) len = 2;
    else if ((b0 & 0xF0) == 0xE0) len = 3;
    else if ((b0 & 0xF8) == 0xF0) len = 4;
    if (len > s.size()) len = 1;
    return s.substr(0, len);
}

void LauncherWindow::on_render() {
    auto* ui = window::UIRenderer::instance();
    auto* dm = DesktopManager::instance();

    /* APP 直启：周期扫描 TF 卡 .bas 自动上桌面（首帧 + 每 100 帧 ≈2s） */
    {
        static uint32_t s_frame = 0;
        if ((s_frame++ % 100) == 0) dm->scan_bas_apps();
    }

    ui->fill_rect({0, 16, static_cast<int16_t>(SCREEN_W), static_cast<int16_t>(SCREEN_H - 16)}, COLOR_BG);

    const bool edit = dm->is_edit();
    const int COLS = 3, ROWS = 4;
    const int pad = 6, gap = 4;
    const int area_w = SCREEN_W - pad * 2 - gap * (COLS - 1);
    /* 多页时底部留 10px 页码圆点；编辑模式改留 16px 提示条 */
    const int pages = dm->in_folder() ? 1 : dm->page_count();
    const int bot = edit ? 16 : (pages > 1 ? 10 : 0);
    const int area_h = (SCREEN_H - 16) - pad * 2 - gap * (ROWS - 1) - bot;
    const int cell_w = area_w / COLS;
    const int cell_h = area_h / ROWS;

    auto* mm = ModeManager::instance();
    const auto& cells = dm->cells();
    if (cells.empty()) return; /* 桌面未铺保底时拒绝越界读（双保险） */

    /* BASIC APP 图标色板 */
    static const uint16_t kPal[8] = {
        rgb565(59, 130, 246),  rgb565(16, 185, 129), rgb565(245, 158, 11),
        rgb565(239, 68, 68),   rgb565(139, 92, 246), rgb565(236, 72, 153),
        rgb565(20, 184, 166),  rgb565(132, 204, 22),
    };

    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            const int idx = r * COLS + c;
            const int x = pad + c * (cell_w + gap);
            const int y = 16 + pad + r * (cell_h + gap);
            if (idx >= (int)cells.size()) continue;
            const Cell& cell = cells[idx];
            const bool sel = (idx == _sel);

            window::Rect rc{static_cast<int16_t>(x), static_cast<int16_t>(y),
                            static_cast<int16_t>(cell_w), static_cast<int16_t>(cell_h)};

            /* 空位：细描边 + 编辑态下的加号 */
            if (cell.type == CellType::Empty) {
                ui->draw_round_rect(rc, 8, sel ? COLOR_ACCENT : COLOR_STROKE, 1);
                if (edit) {
                    const int cx = x + cell_w / 2, cy = y + cell_h / 2;
                    ui->fill_rect({static_cast<int16_t>(cx - 7), static_cast<int16_t>(cy - 1), 14, 2},
                                  COLOR_STROKE_HI);
                    ui->fill_rect({static_cast<int16_t>(cx - 1), static_cast<int16_t>(cy - 7), 2, 14},
                                  COLOR_STROKE_HI);
                }
                continue;
            }

            /* 卡片面：选中提亮 + 强调色描边 */
            ui->fill_round_rect(rc, 8, sel ? COLOR_CARD_HI : COLOR_CARD);
            ui->draw_round_rect(rc, 8, sel ? COLOR_ACCENT : COLOR_STROKE, sel ? 2 : 1);

            /* 图标块 + 标题/副标题 */
            uint16_t tint = COLOR_CARD_HI;
            std::string glyph = "?";
            std::string name;
            std::string sub;

            if (cell.type == CellType::App) {
                const int mi = mm->index_of(cell.app_id.c_str());
                const ModeDesc* d = mi >= 0 ? mm->get(mi) : nullptr;
                tint  = d ? d->color : rgb565(90, 90, 90);
                name  = (d && d->cn) ? d->cn : cell.app_id;
                sub   = (d && d->name) ? d->name : "";
                glyph = _first_glyph(name);
            } else if (cell.type == CellType::BasApp) {
                uint32_t hsh = 2166136261u;
                for (char ch : cell.app_id) { hsh ^= (uint8_t)ch; hsh *= 16777619u; }
                tint = kPal[(hsh >> 8) & 7];
                name = cell.app_id;
                if (name.size() > 4 && name.compare(name.size() - 4, 4, ".bas") == 0)
                    name = name.substr(0, name.size() - 4);
                glyph = _first_glyph(name);
                sub = "BASIC";
            } else {  /* Folder */
                tint = rgb565(250, 204, 21);
                name = dm->folder_name(cell.folder_id);
                glyph = "夹";
                sub = "文件夹";
            }

            const int tile = 32;
            window::Rect tc{static_cast<int16_t>(x + 10), static_cast<int16_t>(y + (cell_h - tile) / 2),
                            static_cast<int16_t>(tile), static_cast<int16_t>(tile)};
            ui->fill_round_rect(tc, 8, tint);
            const uint16_t on_tint = _tint_light(tint) ? COLOR_BLACK : COLOR_WHITE;
            const int gw = window::UIRenderer::text_width_utf8(glyph);
            ui->draw_text(static_cast<int16_t>(tc.x + (tile - gw) / 2),
                          static_cast<int16_t>(tc.y + (tile - window::UIRenderer::CJK_H) / 2),
                          glyph, on_tint);

            const int tx = x + 10 + tile + 10;
            const int max_tw = x + cell_w - tx - 10;
            ui->draw_text_fit(static_cast<int16_t>(tx), static_cast<int16_t>(y + cell_h / 2 - 17),
                              name, COLOR_TEXT, max_tw);
            if (!sub.empty())
                ui->draw_text_fit(static_cast<int16_t>(tx), static_cast<int16_t>(y + cell_h / 2 + 1),
                                  sub, COLOR_TEXT_DIM, max_tw);

            /* 序号：右上角弱化显示 */
            char num[8]; snprintf(num, sizeof(num), "%02d", idx + 1);
            ui->draw_text(static_cast<int16_t>(x + cell_w - 20), static_cast<int16_t>(y + 6),
                          num, COLOR_STROKE_HI);

            /* 拿起标记：警示色大 X */
            if (edit && idx == dm->picked()) {
                auto* lcd = Ili9341::instance();
                lcd->draw_line((uint16_t)(x + 6), (uint16_t)(y + 6),
                               (uint16_t)(x + cell_w - 6), (uint16_t)(y + cell_h - 6), COLOR_WARN);
                lcd->draw_line((uint16_t)(x + cell_w - 6), (uint16_t)(y + 6),
                               (uint16_t)(x + 6), (uint16_t)(y + cell_h - 6), COLOR_WARN);
            }
        }
    }

    /* 页码圆点：多页时标出当前页（编辑态让位给提示条） */
    if (pages > 1 && !edit) {
        const int spacing = 10;
        const int total = (pages - 1) * spacing;
        const int cx0 = SCREEN_W / 2 - total / 2;
        const int cy = SCREEN_H - 9;
        for (int i = 0; i < pages; i++) {
            ui->fill_round_rect({static_cast<int16_t>(cx0 + i * spacing),
                                 static_cast<int16_t>(cy), 4, 4}, 2,
                                i == dm->page() ? COLOR_ACCENT : COLOR_STROKE_HI);
        }
    }

    /* 编辑模式底部提示条（圆角胶囊） */
    if (edit) {
        const int16_t by = static_cast<int16_t>(SCREEN_H - 16 + 2);
        ui->fill_round_rect({6, by, static_cast<int16_t>(SCREEN_W - 12), 12}, 6, COLOR_CARD);
        std::string hint = "编辑";
        if (dm->in_folder()) { hint += " " + dm->folder_name(dm->current_folder()); }
        hint += "   Enter 拿起/放下   |   空格 建夹   |   长按退出";
        ui->draw_text_fit(14, static_cast<int16_t>(by + 1), hint, COLOR_WARN,
                          SCREEN_W - 28);
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
        /* 根桌面第一行再往上 / 最后一行再往下：翻页（文件夹内不翻页） */
        case window::NavEvent::NavUp:
            if (_sel < 3) {
                if (!edit && !dm->in_folder() && dm->prev_page()) _sel += 9;
            } else _sel -= 3;
            break;
        case window::NavEvent::NavDown:
            if (_sel >= 9) {
                if (!edit && !dm->in_folder() && dm->next_page()) _sel -= 9;
            } else _sel += 3;
            break;
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
    ui->fill_rect(_rect, COLOR_BG);
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
    /* 居中确认卡片：深色面 + 强调描边 + 两个胶囊按钮 */
    const int16_t bw = 220, bh = 96;
    window::Rect box{static_cast<int16_t>(SCREEN_W / 2 - bw / 2),
                     static_cast<int16_t>(16 + 80), bw, bh};
    /* 外圈压暗，突出弹窗 */
    ui->fill_rect({0, 16, static_cast<int16_t>(SCREEN_W), static_cast<int16_t>(SCREEN_H - 32)},
                  COLOR_BG);
    ui->fill_round_rect(box, 10, COLOR_CARD);
    ui->draw_round_rect(box, 10, COLOR_STROKE_HI, 1);

    if (_confirm_text) {
        const int tw = window::UIRenderer::text_width_utf8(_confirm_text);
        ui->draw_text_fit(static_cast<int16_t>(SCREEN_W / 2 - tw / 2),
                          static_cast<int16_t>(box.y + 16), _confirm_text, COLOR_TEXT,
                          box.w - 24);
    }

    /* 确定 / 取消：选中项用强调色实心，另一项为描边 */
    const char* yes = "确定";
    const char* no  = "取消";
    const int16_t btn_w = 88, btn_h = 22;
    const int16_t by = static_cast<int16_t>(box.y + bh - btn_h - 16);

    window::Rect b_no{static_cast<int16_t>(box.x + 16), by, btn_w, btn_h};
    window::Rect b_yes{static_cast<int16_t>(box.x + box.w - 16 - btn_w), by, btn_w, btn_h};

    if (_confirm == 1) {
        ui->fill_round_rect(b_yes, 11, _accent);
        ui->fill_round_rect(b_no, 11, COLOR_CARD_HI);
        ui->draw_round_rect(b_no, 11, COLOR_STROKE, 1);
    } else {
        ui->fill_round_rect(b_no, 11, _accent);
        ui->fill_round_rect(b_yes, 11, COLOR_CARD_HI);
        ui->draw_round_rect(b_yes, 11, COLOR_STROKE, 1);
    }

    const int yw = window::UIRenderer::text_width_utf8(yes);
    const int nw = window::UIRenderer::text_width_utf8(no);
    ui->draw_text(static_cast<int16_t>(b_yes.x + (btn_w - yw) / 2),
                  static_cast<int16_t>(by + (btn_h - window::UIRenderer::CJK_H) / 2),
                  yes, _confirm == 1 ? COLOR_BLACK : COLOR_TEXT_DIM);
    ui->draw_text(static_cast<int16_t>(b_no.x + (btn_w - nw) / 2),
                  static_cast<int16_t>(by + (btn_h - window::UIRenderer::CJK_H) / 2),
                  no, _confirm == 0 ? COLOR_BLACK : COLOR_TEXT_DIM);
}

void ModeWindow::draw_fn_bar(window::UIRenderer* ui) {
    /* 底部功能键行：y = SCREEN_H-16 起 16px */
    const int16_t y = static_cast<int16_t>(SCREEN_H - 16);
    ui->fill_rect({0, y, static_cast<int16_t>(SCREEN_W), 16}, COLOR_BG);
    ui->fill_rect({0, y, static_cast<int16_t>(SCREEN_W), 1}, COLOR_STROKE);

    const char* const* labels = fn_labels();
    const int cell_w = SCREEN_W / 6;
    for (int i = 0; i < 6; i++) {
        const int x = i * cell_w;
        char key[4];
        snprintf(key, sizeof(key), "F%d", i + 1);
        /* 键位名用强调色，标签用次文字色，按像素宽度裁剪 */
        ui->draw_text(static_cast<int16_t>(x + 5), static_cast<int16_t>(y + 2), key, _accent);
        const char* lab = labels ? labels[i] : "";
        if (!lab || !*lab) continue;
        ui->draw_text_fit(static_cast<int16_t>(x + 23), static_cast<int16_t>(y + 2),
                          lab, COLOR_TEXT_DIM, cell_w - 27);
    }
}

void ModeWindow::draw_title(window::UIRenderer* ui, const char* title, const char* right) {
    ui->fill_rect({0, 16, static_cast<int16_t>(SCREEN_W), 16}, COLOR_CARD);
    ui->fill_rect({0, 31, static_cast<int16_t>(SCREEN_W), 1}, COLOR_STROKE);
    /* 左侧强调色竖条 */
    ui->fill_round_rect({6, 21, 3, 10}, 1, _accent);
    ui->draw_text_fit(14, 18, title ? title : "", COLOR_TEXT, SCREEN_W - 110);
    if (right && *right) {
        const int rw = window::UIRenderer::text_width_utf8(right);
        const int pw = rw + 12;
        const int px = SCREEN_W - pw - 6;
        ui->fill_round_rect({static_cast<int16_t>(px), 20, static_cast<int16_t>(pw), 12}, 6,
                            COLOR_CARD_HI);
        ui->draw_text_fit(static_cast<int16_t>(px + 6), 20, right, COLOR_TEXT_DIM, rw);
    }
}

void ModeWindow::draw_hint(window::UIRenderer* ui, const char* text) {
    if (!text || !*text) return;
    const std::string s(text);
    const int rw = window::UIRenderer::text_width_utf8(s);
    ui->draw_text_fit(static_cast<int16_t>((SCREEN_W - rw) / 2),
                      static_cast<int16_t>(SCREEN_H - 16 - window::UIRenderer::FONT_H - 6),
                      s, COLOR_TEXT_DIM, SCREEN_W - 12);
}

} // namespace modes
} // namespace memoria
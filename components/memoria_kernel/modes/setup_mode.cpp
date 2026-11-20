/**
 * @file setup_mode.cpp
 * @brief SETUP 设置模式
 *
 * 卡西欧式"分组 + 循环切换"：左右循环改值，Enter 保存（NVS）。
 * 与旧 settings_app 共用 NVS namespace "settings"（backlight/volume/auto_update）。
 */

#include "mode_manager.hpp"
#include "ili9341.hpp"
#include "i2s_audio.hpp"
#include "package_manager.hpp"
#include "firmware_update.hpp"
#include "wifi_manager.hpp"
#include "ble_manager.hpp"
#include "keyboard_keymap.hpp"

extern "C" {
#include <nvs_flash.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
}

#include <cstdio>
#include <cstring>

#include <string>
#include <vector>
#include <utility>
#include <algorithm>

namespace memoria {
using namespace drivers;
namespace modes {

static const char* NVS_NS = "settings";

class SetupMode : public ModeWindow {
public:
    SetupMode() { _load(); }

    void mode_render(window::UIRenderer* ui, bool focused) override {
        if (_net_active) { _render_net(ui); return; }
        if (_pw_active) { _render_pw(ui, focused); return; }

        char right[40];
        snprintf(right, sizeof(right), "%d/%d", _cur + 1, (int)_items.size());
        draw_title(ui, "SETUP 设置", right);

        /* 四个分组各一张圆角卡片：当前项所在组展开显示设置行，其余组收起只留组标题。
         * 全部展开会超出 256px 内容区，故用"选中组展开"保证 26px 行高与可读性。 */
        int y = 34;
        for (int g = 0; g < 4; g++) {
            const int b = _group_begin(g), e = _group_end(g);
            const int rows = e - b;
            const bool open = (g == _group_of(_cur));
            const int h = open ? (28 + rows * SET_ROW_H) : 26;
            window::Rect card{6, static_cast<int16_t>(y), static_cast<int16_t>(SCREEN_W - 12),
                              static_cast<int16_t>(h)};
            ui->fill_round_rect(card, 8, COLOR_CARD);
            ui->draw_round_rect(card, 8, open ? _accent : COLOR_STROKE, 1);

            ui->draw_text_fit(static_cast<int16_t>(card.x + 12), static_cast<int16_t>(y + 7),
                              _group_name(g), open ? COLOR_TEXT : COLOR_TEXT_DIM, card.w - 90);
            char cnt[16];
            snprintf(cnt, sizeof(cnt), "%d 项", rows);
            const int cw2 = window::UIRenderer::text_width_utf8(cnt);
            ui->draw_text(static_cast<int16_t>(card.x + card.w - 12 - cw2),
                          static_cast<int16_t>(y + 7), cnt, COLOR_TEXT_DIM);

            if (open) {
                ui->divider_h(static_cast<int16_t>(card.x + 10), static_cast<int16_t>(y + 22),
                              static_cast<int16_t>(card.w - 20), COLOR_STROKE);
                for (int i = b; i < e; i++)
                    _draw_row(ui, card, y + 24 + (i - b) * SET_ROW_H, i, i == _cur);
            }
            y += h + 4;
        }

        draw_hint(ui, focused ? "左右 切换值 | Enter 保存 | 长按返回" : "");
    }

    bool mode_nav(const window::NavInput& ni) override {
        if (_net_active) return _net_nav(ni);
        if (_pw_active) return _pw_nav(ni);
        switch (ni.dir) {
            case window::NavEvent::NavUp:    _cur = (_cur - 1 + (int)_items.size()) % _items.size(); break;
            case window::NavEvent::NavDown:  _cur = (_cur + 1) % _items.size(); break;
            case window::NavEvent::NavLeft:  _items[_cur].change(-1); break;
            case window::NavEvent::NavRight: _items[_cur].change(+1); break;
            case window::NavEvent::NavEnter:
                if (_items[_cur].enter) _items[_cur].enter();
                else _save(_cur);
                break;
            default: return false;
        }
        return true;
    }

    /* 长按返回：网络子界面先退回设置列表，再退主菜单 */
    bool on_nav(const window::NavInput& ni) override {
        if (ni.dir == window::NavEvent::NavBack && _pw_active) {
            _pw_active = false;
            _pw_buf.clear();
            _pw_msg.clear();
            return true;
        }
        if (ni.dir == window::NavEvent::NavBack && _net_active) {
            if (_net_edit != 0) { _net_cancel_edit(); }   /* 编辑中先取消编辑 */
            else _net_active = false;
            return true;
        }
        return ModeWindow::on_nav(ni);
    }

    /* 键盘字符输入：网络子界面编辑模式消费（WiFi 密码 / 热点配置 / 蓝牙名称） */
    bool on_key(uint16_t key, bool pressed) override {
        if (_pw_active) return _pw_key(key, pressed);
        if (!_net_active || _net_edit == 0) return false;
        if (!pressed) return true;   /* 编辑期间吞掉释放沿 */
        if (key >= 0x20 && key <= 0x7E) {
            if (_net_edit_len < 63) {
                _net_edit_buf[_net_edit_len++] = (char)key;
                _net_edit_buf[_net_edit_len] = 0;
            }
            return true;
        }
        switch (key) {
            case input::K_BACKSPACE:
                if (_net_edit_len > 0) {
                    _net_edit_len--;
                    _net_edit_buf[_net_edit_len] = 0;
                }
                return true;
            case input::K_ENTER:
                _net_commit_edit();
                return true;
            case input::K_ESC:
                _net_cancel_edit();
                return true;
            default:
                return true;   /* 编辑期间其它键一律吞掉 */
        }
    }

    const char* const* fn_labels() override {
        if (_pw_active) {
            static const char* pf[6] = {"-", "保存", "-", "-", "-", "返回"};
            return pf;
        }
        if (_net_active) {
            static const char* nf[6] = {"WiFi", "热点", "蓝牙", "扫描", "-", "返回"};
            return nf;
        }
        static const char* f[6] = {"-", "确认", "+", "默认", "保存", "菜单"};
        return f;
    }

    void on_fn(int idx) override {
        if (_pw_active) {
            switch (idx) {
                case 1: _pw_save(); break;
                case 5:
                    _pw_active = false;
                    _pw_buf.clear();
                    _pw_msg.clear();
                    break;
                default: break;
            }
            return;
        }
        if (_net_active) {
            switch (idx) {
                case 0: _net_sub = 0; _net_sel = 0; break;
                case 1: _net_sub = 1; _net_sel = 0; break;
                case 2: _net_sub = 2; _net_sel = 0; break;
                case 3:
                    if (_net_sub == 0) _net_scan_async();
                    else _net_toast("到 WiFi 页再扫描");
                    break;
                case 4: break;
                case 5: _net_active = false; break;   /* 返回设置列表 */
            }
            return;
        }
        switch (idx) {
            case 0: _items[_cur].change(-1); break;
            case 1: _save(_cur); break;
            case 2: _items[_cur].change(+1); break;
            case 3: _reset(); break;
            case 4: _save_all(); break;
            case 5: break;
        }
    }

private:
    struct Item {
        std::string name;
        std::function<std::string()> value;
        std::function<void(int)> change;
        std::function<void()> enter{};   /* 可选：NavEnter 时执行（优先于保存） */
    };

    /* 内容区设置行高（26px，符合深色卡片列表节奏） */
    static const int SET_ROW_H = 26;

    static int _group_of(int i) { return i < 1 ? 0 : (i < 4 ? 1 : (i < 9 ? 2 : 3)); }
    static int _group_begin(int g) { static const int b[4] = {0, 1, 4, 9}; return b[g]; }
    static int _group_end(int g) { static const int e[4] = {1, 4, 9, 11}; return e[g]; }

    static const char* _group_name(int g) {
        static const char* names[4] = {"显示", "声音 | 网络", "系统", "商店 | 源"};
        return names[g < 0 ? 0 : (g > 3 ? 3 : g)];
    }

    /* 设置行：左标题 + 右侧值/开关/进度；选中 = 提亮面 + 左侧 3px 强调竖条 */
    void _draw_row(window::UIRenderer* ui, const window::Rect& card, int y, int idx, bool sel) {
        const int ry = y + 2;
        if (sel) {
            ui->fill_round_rect({static_cast<int16_t>(card.x + 5), static_cast<int16_t>(ry),
                                 static_cast<int16_t>(card.w - 10), 22}, 6, COLOR_CARD_HI);
            ui->fill_round_rect({static_cast<int16_t>(card.x + 9), static_cast<int16_t>(ry + 5),
                                 3, 12}, 1, _accent);
        }
        ui->draw_text_fit(static_cast<int16_t>(card.x + 18), static_cast<int16_t>(ry + 5),
                          _items[idx].name, COLOR_TEXT, 150);

        const std::string v = _items[idx].value();
        const int right_edge = card.x + card.w - 12;
        if (idx == 0 || idx == 1 || idx == 5) {
            /* 连续量：数值 + 进度条（进度 = 当前值占上限比例） */
            const int vmax = 130;
            const int vw = window::UIRenderer::text_width_utf8(v);
            ui->draw_text_fit(static_cast<int16_t>(right_edge - std::min(vw, vmax)),
                              static_cast<int16_t>(ry + 2), v,
                              sel ? _accent : COLOR_TEXT_DIM, vmax);
            ui->progress({static_cast<int16_t>(right_edge - vmax), static_cast<int16_t>(ry + 15),
                          static_cast<int16_t>(vmax), 4}, _slider_permille(idx), _accent,
                         COLOR_BG_SOFT, 2);
        } else if (idx == 2) {
            /* 布尔量：圆角轨道 + 圆点开关 */
            _draw_switch(ui, right_edge - 34, ry + 4, _auto_update);
        } else {
            const int vmax = 200;
            const int vw = window::UIRenderer::text_width_utf8(v);
            ui->draw_text_fit(static_cast<int16_t>(right_edge - std::min(vw, vmax)),
                              static_cast<int16_t>(ry + 5), v,
                              sel ? _accent : COLOR_TEXT_DIM, vmax);
        }
    }

    /* 开关：轨道 COLOR_BG_SOFT，圆点 开=_accent / 关=COLOR_STROKE_HI */
    void _draw_switch(window::UIRenderer* ui, int x, int y, bool on) {
        window::Rect tr{static_cast<int16_t>(x), static_cast<int16_t>(y), 34, 14};
        ui->fill_round_rect(tr, 7, COLOR_BG_SOFT);
        ui->draw_round_rect(tr, 7, on ? _accent : COLOR_STROKE_HI, 1);
        const int cx = on ? (x + 34 - 9) : (x + 9);
        drivers::Ili9341::instance()->fill_circle(static_cast<int16_t>(cx), static_cast<int16_t>(y + 7),
                                                 5, on ? _accent : COLOR_STROKE_HI);
    }

    /* 连续量 → 0~1000 千分比 */
    int _slider_permille(int idx) {
        if (idx == 0) return (int)drivers::Ili9341::instance()->get_backlight() * 1000 / 255;
        if (idx == 1) return (int)drivers::I2sAudio::instance()->get_volume() * 10;
        if (idx == 5) return (int)_sleep_min * 1000 / 30;
        return 0;
    }

    void _save_u8(const char* key, uint8_t v) {
        nvs_handle_t h;
        if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
            nvs_set_u8(h, key, v); nvs_commit(h); nvs_close(h);
        }
    }
    uint8_t _get_u8(const char* key, uint8_t def) {
        nvs_handle_t h; uint8_t v = def;
        if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
            nvs_get_u8(h, key, &v); nvs_close(h);
        }
        return v;
    }

    void _load() {
        _items.clear();
        auto* lcd = drivers::Ili9341::instance();
        auto* aud = drivers::I2sAudio::instance();
        uint8_t bl = _get_u8("backlight", 200);
        uint8_t vol = _get_u8("volume", 50);
        uint8_t au = _get_u8("auto_update", 1);
        _auto_update = au != 0;
        _hour12 = _get_u8("hour12", 0) != 0;
        _sleep_min = _get_u8("sleep_min", 3);

        _items.push_back({"背光亮度", [lcd, bl]() { return std::to_string(lcd->get_backlight()) + "/255"; },
                          [lcd](int d) { lcd->set_backlight((uint8_t)std::clamp<int>(lcd->get_backlight() + d * 25, 20, 255)); }});
        _items.push_back({"音量", [aud, vol]() { return std::to_string(aud->get_volume()) + "%"; },
                          [aud](int d) { aud->set_volume((uint8_t)std::clamp<int>(aud->get_volume() + d * 10, 0, 100)); }});
        _items.push_back({"WiFi 自动更新", [this]() { return _auto_update ? "开" : "关"; },
                          [this](int) { _auto_update = !_auto_update; }});
        _items.push_back({"网络管理", [this]() { return _net_active ? "管理中" : "WiFi/热点/蓝牙"; },
                          [](int) {},
                          [this]() { _enter_net(); }});
        _items.push_back({"日期格式", [this]() { return _hour12 ? "12 小时" : "24 小时"; },
                          [this](int) { _hour12 = !_hour12; }});
        _items.push_back({"自动休眠", [this]() { return std::to_string(_sleep_min) + " 分钟"; },
                          [this](int d) { _sleep_min = (uint8_t)std::clamp<int>(_sleep_min + d, 1, 30); }});
        _items.push_back({"开机密码", [this]() { return ModeManager::instance()->login_password(); },
                          [](int) {},
                          [this]() { _pw_active = true; _pw_buf.clear(); _pw_msg.clear(); }});
        _items.push_back({"软件源", [this]() { return _source_domain(); },
                          [](int) { /* 换源方式见提示 */ }});
        _items.push_back({"系统版本", [this]() { return package::firmware_current_version(); },
                          [](int) {}});
        _items.push_back({"检查更新", [this]() { return _upd_msg.empty() ? std::string("未检查") : _upd_msg; },
                          [](int) {},
                          [this]() { _check_update(); }});
        _items.push_back({"立即更新", [this]() { return _upd_ready ? std::string("按 Enter 下载安装") : std::string("先检查更新"); },
                          [](int) {},
                          [this]() { _apply_update(); }});
    }

    /* 检查更新：拉版本清单对比，结果缓存到 _upd_msg/_upd_ready */
    void _check_update() {
        package::FirmwareInfo info;
        if (!package::firmware_check(info)) {
            _upd_msg = "失败: " + info.error;
            _upd_ready = false;
            return;
        }
        if (info.update_available) {
            _upd_msg = "发现新版本 " + info.latest + (info.major ? "（大版本）" : "");
            _upd_ready = true;
            _upd_url = info.url;
            _upd_sha = info.sha256;
        } else {
            _upd_msg = "已是最新 " + info.latest;
            _upd_ready = false;
        }
    }

    /* 立即更新：后台任务下载安装（成功自动重启），UI 保持响应 */
    void _apply_update() {
        if (!_upd_ready || _upd_url.empty()) { _upd_msg = "先检查更新"; return; }
        _upd_msg = "下载更新中…";
        typedef std::pair<std::string, std::string> OtaJob;
        OtaJob* job = new OtaJob(_upd_url, _upd_sha);
        BaseType_t ok = xTaskCreate([](void* arg) {
            OtaJob* j = static_cast<OtaJob*>(arg);
            package::firmware_apply(j->first, j->second);   /* 成功即重启，不返回 */
            delete j;
            vTaskDelete(nullptr);
        }, "fw_ota", 8192, job, 3, nullptr);
        if (ok != pdPASS) { delete job; _upd_msg = "任务创建失败"; }
    }

    std::string _source_domain() {
        std::string u = package::get_mirror_url();
        size_t p = u.find("://");
        if (p != std::string::npos) u = u.substr(p + 3);
        p = u.find('/');
        if (p != std::string::npos) u = u.substr(0, p);
        return u.empty() ? "默认源" : u;
    }

    void _save(int idx) {
        switch (idx) {
            case 0: _save_u8("backlight", drivers::Ili9341::instance()->get_backlight()); break;
            case 1: _save_u8("volume", drivers::I2sAudio::instance()->get_volume()); break;
            case 2: _save_u8("auto_update", _auto_update ? 1 : 0); break;
            case 3: break; /* 网络管理：操作即时持久化 */
            case 4: _save_u8("hour12", _hour12 ? 1 : 0); break;
            case 5: _save_u8("sleep_min", _sleep_min); break;
            case 6: break; /* 开机密码：密码子界面里直接写 NVS */
            case 7: break; /* 软件源：由商店页/TF卡管理 */
        }
    }

    void _save_all() {
        _save(0); _save(1); _save(2); _save(3); _save(4); _save(5); _save(6); _save(7);
    }

    void _reset() {
        drivers::Ili9341::instance()->set_backlight(200);
        drivers::I2sAudio::instance()->set_volume(50);
        _auto_update = true; _hour12 = false; _sleep_min = 3;
        _save_all();
    }

    /* ============================================================
     *  开机密码子界面：键盘直接输 8 位数字，Enter 写 NVS
     *  写进去之后下次 boot 才生效，本次会话不改当前锁屏
     * ============================================================ */
    void _render_pw(window::UIRenderer* ui, bool focused) {
        draw_title(ui, "开机密码", "SETUP");

        /* 当前密码卡：标签弱化 + 数值主文字 */
        window::Rect box{6, 34, static_cast<int16_t>(SCREEN_W - 12), 92};
        ui->card(box, 8, COLOR_CARD, COLOR_STROKE);
        ui->draw_text_fit(static_cast<int16_t>(box.x + 12), static_cast<int16_t>(box.y + 10),
                          "当前密码", COLOR_TEXT_DIM, box.w - 24);
        const std::string cur = ModeManager::instance()->login_password();
        ui->draw_text_fit(static_cast<int16_t>(box.x + 12), static_cast<int16_t>(box.y + 28),
                          cur, COLOR_TEXT, box.w - 24);

        /* 8 个密码格：已填 = 提亮面 + 强调描边 */
        const int bw = 30, gap = 8;
        const int total = 8 * bw + 7 * gap;
        const int x0 = (SCREEN_W - total) / 2;
        const int y0 = 64;
        for (int i = 0; i < 8; i++) {
            const int x = x0 + i * (bw + gap);
            const bool filled = i < static_cast<int>(_pw_buf.size());
            window::Rect cell{static_cast<int16_t>(x), static_cast<int16_t>(y0),
                              static_cast<int16_t>(bw), 26};
            ui->fill_round_rect(cell, 6, filled ? COLOR_CARD_HI : COLOR_BG_SOFT);
            ui->draw_round_rect(cell, 6, filled ? _accent : COLOR_STROKE, 1);
            if (filled) {
                char s[2] = {_pw_buf[i], 0};
                ui->draw_text(static_cast<int16_t>(x + bw / 2 - 4), static_cast<int16_t>(y0 + 7),
                              s, COLOR_TEXT);
            }
        }

        const std::string tip = _pw_msg.empty() ? "直接打 8 位数字 | Enter 保存 | ESC 返回" : _pw_msg;
        ui->draw_text_fit(8, 132, tip, _pw_msg.empty() ? COLOR_TEXT_DIM : COLOR_WARN, SCREEN_W - 16);

        draw_hint(ui, focused ? "改完下次开机用新密码，本次会话不锁回去" : "");
    }

    bool _pw_nav(const window::NavInput& ni) {
        switch (ni.dir) {
            case window::NavEvent::NavEnter: _pw_save(); break;
            case window::NavEvent::NavBack:
                _pw_active = false;
                _pw_buf.clear();
                _pw_msg.clear();
                break;
            default: break;
        }
        return true;
    }

    bool _pw_key(uint16_t key, bool pressed) {
        if (!pressed) return true;
        if (key >= '0' && key <= '9') {
            if (_pw_buf.size() < 8) {
                _pw_buf += static_cast<char>(key);
                _pw_msg.clear();
            }
            return true;
        }
        switch (key) {
            case input::K_BACKSPACE:
                if (!_pw_buf.empty()) _pw_buf.pop_back();
                _pw_msg.clear();
                return true;
            case input::K_ENTER:
                _pw_save();
                return true;
            case input::K_ESC:
                _pw_active = false;
                _pw_buf.clear();
                _pw_msg.clear();
                return true;
            default:
                return true;
        }
    }

    void _pw_save() {
        if (_pw_buf.size() != 8) {
            _pw_msg = "要 8 位数字，还差 " + std::to_string(8 - static_cast<int>(_pw_buf.size())) + " 位";
            return;
        }
        if (ModeManager::instance()->set_login_password(_pw_buf)) {
            _pw_msg = "已保存，下次开机生效";
            _pw_buf.clear();
        } else {
            _pw_msg = "保存失败：NVS 写入错误";
        }
    }

    /* ============================================================
     *  网络管理子界面（手机式三子页：WiFi / 热点 / 蓝牙）
     *  铁律：boot 零射频不破——扫描/连接只在用户进入网络页操作时触发
     * ============================================================ */
    void _enter_net() {
        _net_active = true;
        _net_sub = 0; _net_sel = 0; _net_edit = 0;
        s_net_scan_done = false;   /* 不主动扫描，用户按 Enter/F4 才扫 */
    }

    /* 网络页行：标题 + 右侧值/开关；选中 = 提亮面 + 左侧强调竖条 */
    void _net_row(window::UIRenderer* ui, int x, int y, int w, const char* label,
                  const std::string& value, bool sel, bool sw, bool sw_on, int label_max) {
        const int ry = y + 2;
        if (sel) {
            ui->fill_round_rect({static_cast<int16_t>(x + 5), static_cast<int16_t>(ry),
                                 static_cast<int16_t>(w - 10), 22}, 6, COLOR_CARD_HI);
            ui->fill_round_rect({static_cast<int16_t>(x + 9), static_cast<int16_t>(ry + 5),
                                 3, 12}, 1, _accent);
        }
        ui->draw_text_fit(static_cast<int16_t>(x + 18), static_cast<int16_t>(ry + 5), label,
                          COLOR_TEXT, label_max);
        if (sw) {
            _draw_switch(ui, x + w - 12 - 34, ry + 4, sw_on);
        } else {
            const int vmax = w - 150;
            const int vw = window::UIRenderer::text_width_utf8(value);
            ui->draw_text_fit(static_cast<int16_t>(x + w - 12 - std::min(vw, vmax)),
                              static_cast<int16_t>(ry + 5), value,
                              sel ? _accent : COLOR_TEXT_DIM, vmax);
        }
    }

    void _render_net(window::UIRenderer* ui) {
        const char* sub_name[3] = {"WiFi", "热点", "蓝牙"};
        draw_title(ui, "SETUP 网络", sub_name[_net_sub]);
        char line[80];

        /* 子页标签胶囊：选中 = 提亮面 + 强调描边 */
        int tx = 8;
        for (int i = 0; i < 3; i++) {
            const int tw = window::UIRenderer::text_width_utf8(sub_name[i]);
            window::Rect tab{static_cast<int16_t>(tx), 34, static_cast<int16_t>(tw + 16), 18};
            const bool on = (i == _net_sub);
            ui->fill_round_rect(tab, 9, on ? COLOR_CARD_HI : COLOR_CARD);
            ui->draw_round_rect(tab, 9, on ? _accent : COLOR_STROKE, 1);
            ui->draw_text(static_cast<int16_t>(tx + 8), 37, sub_name[i],
                          on ? COLOR_TEXT : COLOR_TEXT_DIM);
            tx += tw + 16 + 6;
        }

        auto* wm = WifiManager::instance();
        if (_net_sub == 0) {
            /* ---- WiFi：状态卡 + 扫描列表（卡片行） ---- */
            window::Rect st{6, 58, static_cast<int16_t>(SCREEN_W - 12), 46};
            ui->card(st, 8, COLOR_CARD, COLOR_STROKE);
            std::string sv = wm->saved_ssid();
            if (wm->connected()) {
                snprintf(line, sizeof(line), "已连 %s", wm->current_ssid().c_str());
                ui->draw_text_fit(16, 66, line, COLOR_OK, st.w - 20);
                snprintf(line, sizeof(line), "%s  %ddBm", wm->ip().c_str(), wm->rssi());
                ui->draw_text_fit(16, 84, line, COLOR_TEXT_DIM, st.w - 20);
            } else {
                if (wm->ap_active())
                    ui->draw_text_fit(16, 66, "热点模式（STA 未连接）", COLOR_WARN, st.w - 20);
                else
                    ui->draw_text_fit(16, 66, "未连接", COLOR_TEXT_DIM, st.w - 20);
                snprintf(line, sizeof(line), "已存 %s", sv.empty() ? "无" : sv.c_str());
                ui->draw_text_fit(16, 84, line, COLOR_TEXT_DIM, st.w - 20);
            }

            if (s_net_scanning) {
                ui->draw_text_fit(10, 112, "扫描中...", COLOR_WARN, SCREEN_W - 20);
            } else if (!s_net_scan_done) {
                ui->draw_text_fit(10, 112, "按 Enter 扫描附近 WiFi", COLOR_TEXT, SCREEN_W - 20);
            } else {
                size_t cnt = wm->scan_count();
                if (cnt == 0) {
                    ui->draw_text_fit(10, 112, "未找到 WiFi（Enter 重扫）", COLOR_TEXT, SCREEN_W - 20);
                } else {
                    const int shown = cnt < 12 ? (int)cnt : 12;
                    const int row_h = 22, gap = 3;
                    int start = _net_sel > 5 ? _net_sel - 5 : 0;
                    if (start > shown - 1) start = shown > 0 ? shown - 1 : 0;
                    int y = 112;
                    for (int i = start; i < shown && y + row_h <= SCREEN_H - 38; i++, y += row_h + gap) {
                        const ScanAp* ap = wm->scan_at(i);
                        snprintf(line, sizeof(line), "%ddBm%s", ap->rssi, ap->authmode ? " 锁" : "");
                        _net_row(ui, 6, y, SCREEN_W - 12, ap->ssid, line, i == _net_sel,
                                 false, false, 250);
                    }
                }
            }
            draw_hint(ui, "左右 切页 | 上下 选网 | Enter 扫描/连接");
        } else if (_net_sub == 1) {
            /* ---- 热点：开关 + SSID/密码 ---- */
            const int w = SCREEN_W - 12;
            ui->card({6, 58, static_cast<int16_t>(w), static_cast<int16_t>(28 + 3 * SET_ROW_H)},
                     8, COLOR_CARD, COLOR_STROKE);
            std::string apw = wm->ap_pass();
            _net_row(ui, 6, 58 + 24, w, "热点开关", "", _net_sel == 0, true, wm->ap_active(), 120);
            _net_row(ui, 6, 58 + 24 + SET_ROW_H, w, "热点名称", wm->ap_ssid(),
                     _net_sel == 1, false, false, 120);
            _net_row(ui, 6, 58 + 24 + 2 * SET_ROW_H, w, "热点密码",
                     apw.empty() ? "(无)" : apw, _net_sel == 2, false, false, 120);
            ui->draw_text_fit(8, 168, "Enter 开/关热点 | 上下 选行再 Enter 编辑",
                              COLOR_TEXT_DIM, SCREEN_W - 16);
            if (wm->ap_active())
                ui->draw_text_fit(8, 184, "设备连此热点后地址 192.168.4.1",
                                  COLOR_TEXT_DIM, SCREEN_W - 16);
            draw_hint(ui, "左右 切页 | Enter 开关/编辑");
        } else {
            /* ---- 蓝牙：开关 + 名称 ---- */
            auto* bm = BleManager::instance();
            const int w = SCREEN_W - 12;
            ui->card({6, 58, static_cast<int16_t>(w), static_cast<int16_t>(28 + 2 * SET_ROW_H)},
                     8, COLOR_CARD, COLOR_STROKE);
            _net_row(ui, 6, 58 + 24, w, "蓝牙开关", "", _net_sel == 0, true, bm->enabled(), 120);
            _net_row(ui, 6, 58 + 24 + SET_ROW_H, w, "设备名称", bm->device_name(),
                     _net_sel == 1, false, false, 120);
            ui->draw_text_fit(8, 140, "重启后生效", COLOR_TEXT_DIM, SCREEN_W - 16);
            draw_hint(ui, "左右 切页 | Enter 开关/编辑名称");
        }

        /* 编辑输入框 */
        if (_net_edit != 0) {
            window::Rect box{6, 238, static_cast<int16_t>(SCREEN_W - 12), 32};
            ui->card(box, 8, COLOR_CARD, _accent);
            char in[80];
            if (_net_edit == 1 || _net_edit == 3) {
                snprintf(in, sizeof(in), "输入: %s", _net_masked());
            } else {
                snprintf(in, sizeof(in), "输入: %s", _net_edit_buf);
            }
            ui->draw_text_fit(14, 246, in, COLOR_TEXT, box.w - 20);
            ui->draw_text_fit(8, 274, "Enter 确认 | Esc 取消", COLOR_TEXT_DIM, SCREEN_W - 16);
            return;
        }

        /* 瞬时提示 */
        if (_net_msg_on) {
            uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
            if (now - _net_msg_tick < 2500) {
                ui->draw_text_fit(8, 292, _net_msg, COLOR_WARN, SCREEN_W - 16);
            } else {
                _net_msg_on = false;
            }
        }
    }

    bool _net_nav(const window::NavInput& ni) {
        if (_net_edit != 0) {
            if (ni.dir == window::NavEvent::NavEnter) { _net_commit_edit(); return true; }
            return true;   /* 编辑期间吞掉其它方向（Esc 由 on_key 消费） */
        }
        switch (ni.dir) {
            case window::NavEvent::NavLeft:
                _net_sub = (_net_sub + 2) % 3; _net_sel = 0; return true;
            case window::NavEvent::NavRight:
                _net_sub = (_net_sub + 1) % 3; _net_sel = 0; return true;
            case window::NavEvent::NavUp: {
                int lim = _net_sub == 0 ? (int)(s_net_scan_done ? WifiManager::instance()->scan_count() : 0)
                                        : (_net_sub == 1 ? 3 : 2);
                if (lim > 0) _net_sel = (_net_sel - 1 + lim) % lim;
                return true;
            }
            case window::NavEvent::NavDown: {
                int lim = _net_sub == 0 ? (int)(s_net_scan_done ? WifiManager::instance()->scan_count() : 0)
                                        : (_net_sub == 1 ? 3 : 2);
                if (lim > 0) _net_sel = (_net_sel + 1) % lim;
                return true;
            }
            case window::NavEvent::NavEnter: _net_enter(); return true;
            default: return false;
        }
    }

    void _net_enter() {
        auto* wm = WifiManager::instance();
        if (_net_sub == 0) {
            /* WiFi：扫描 / 连接 */
            if (s_net_scanning) return;
            if (!s_net_scan_done) { _net_scan_async(); return; }
            size_t cnt = wm->scan_count();
            if (cnt == 0) { _net_scan_async(); return; }
            if ((size_t)_net_sel >= cnt) return;
            const ScanAp* ap = wm->scan_at(_net_sel);
            if (ap->authmode == 0) {
                esp_err_t r = wm->connect(ap->ssid, "");
                _net_toast(r == ESP_OK ? "连接中..." : "连接失败");
            } else if (ap->saved) {
                esp_err_t r = wm->connect(ap->ssid, wm->saved_pass());
                _net_toast(r == ESP_OK ? "连接中..." : "连接失败");
            } else {
                std::strncpy(_net_target_ssid, ap->ssid, 32);
                _net_target_ssid[32] = 0;
                _net_edit_buf[0] = 0; _net_edit_len = 0;
                _net_edit = 1;   /* 密码输入 */
            }
        } else if (_net_sub == 1) {
            /* 热点 */
            if (_net_sel == 0) {
                if (wm->ap_active()) {
                    wm->stop_ap();
                    _net_toast("热点已关闭");
                } else {
                    esp_err_t r = wm->start_ap(wm->ap_ssid(), wm->ap_pass());
                    _net_toast(r == ESP_OK ? "热点已开启" : "启动失败");
                }
            } else {
                _net_edit_buf[0] = 0; _net_edit_len = 0;
                _net_edit = (_net_sel == 1) ? 2 : 3;   /* SSID / 密码 */
            }
        } else {
            /* 蓝牙 */
            auto* bm = BleManager::instance();
            if (_net_sel == 0) {
                bm->set_enabled(!bm->enabled());
                _net_toast("已设置，重启生效");
            } else {
                _net_edit_buf[0] = 0; _net_edit_len = 0;
                _net_edit = 4;   /* 名称 */
            }
        }
    }

    void _net_commit_edit() {
        auto* wm = WifiManager::instance();
        auto* bm = BleManager::instance();
        int kind = _net_edit;
        std::string v(_net_edit_buf);
        _net_edit = 0;
        switch (kind) {
            case 1: {   /* WiFi 密码 → 连接 */
                esp_err_t r = wm->connect(_net_target_ssid, v);
                _net_toast(r == ESP_OK ? "连接中..." : "连接失败");
                break;
            }
            case 2: {   /* 热点 SSID */
                std::string apw = wm->ap_pass();
                wm->set_ap_config(v, apw);
                if (wm->ap_active()) {
                    esp_err_t r = wm->start_ap(v, apw);
                    _net_toast(r == ESP_OK ? "热点已重启" : "应用失败");
                } else {
                    _net_toast("热点配置已保存");
                }
                break;
            }
            case 3: {   /* 热点密码 */
                std::string apn = wm->ap_ssid();
                wm->set_ap_config(apn, v);
                if (wm->ap_active()) {
                    esp_err_t r = wm->start_ap(apn, v);
                    _net_toast(r == ESP_OK ? "热点已重启" : "应用失败");
                } else {
                    _net_toast("热点配置已保存");
                }
                break;
            }
            case 4: {   /* 蓝牙名称 */
                bm->set_device_name(v);
                _net_toast("名称已保存，重启生效");
                break;
            }
            default: break;
        }
    }

    void _net_cancel_edit() {
        _net_edit = 0;
        _net_toast("已取消");
    }

    void _net_toast(const char* s) {
        std::strncpy(_net_msg, s, sizeof(_net_msg) - 1);
        _net_msg[sizeof(_net_msg) - 1] = 0;
        _net_msg_on = true;
        _net_msg_tick = (uint32_t)(esp_timer_get_time() / 1000);
    }

    const char* _net_masked() {
        static char m[65];
        size_t n = _net_edit_len < 64 ? _net_edit_len : 64;
        for (size_t i = 0; i < n; i++) m[i] = '*';
        m[n] = 0;
        return m;
    }

    /* 后台扫描：射频按需启动（幂等），boot 零射频不破 */
    void _net_scan_async() {
        if (s_net_scanning) return;
        WifiManager::instance()->start_rf();   /* 需求侧触发射频（仅用户操作时） */
        s_net_scanning = true;
        s_net_scan_done = false;
        xTaskCreate(_net_scan_task, "net_scan", 4096, nullptr, 2, nullptr);
    }

    static void _net_scan_task(void*) {
        WifiManager::instance()->scan();
        s_net_scanning = false;
        s_net_scan_done = true;
        vTaskDelete(NULL);   /* 任务主体不允许 return（#87 同款） */
    }

    static bool s_net_scanning;
    static bool s_net_scan_done;

    std::vector<Item> _items;
    bool _auto_update = true;
    bool _hour12 = false;
    uint8_t _sleep_min = 3;
    std::string _upd_msg;      /* 更新状态缓存 */
    std::string _upd_url;
    std::string _upd_sha;      /* 清单里的固件 sha256，空 = 清单没给 */
    bool _upd_ready = false;

    /* 开机密码子界面状态 */
    bool _pw_active = false;
    std::string _pw_buf;           /* 正在输入的 8 位密码 */
    std::string _pw_msg;           /* 提示/错误 */

    /* 网络管理子界面状态 */
    bool _net_active = false;
    int _net_sub = 0;              /* 0=WiFi 1=热点 2=蓝牙 */
    int _net_sel = 0;              /* 列表/行选中 */
    int _net_edit = 0;             /* 0=无 1=WiFi密码 2=热点SSID 3=热点密码 4=蓝牙名称 */
    char _net_edit_buf[65] = {};
    size_t _net_edit_len = 0;
    char _net_target_ssid[33] = {};
    char _net_msg[48] = {};
    bool _net_msg_on = false;
    uint32_t _net_msg_tick = 0;
};

bool SetupMode::s_net_scanning = false;
bool SetupMode::s_net_scan_done = false;

static std::shared_ptr<window::Window> setup_create() {
    return std::make_shared<SetupMode>();
}

void setup_mode_register() {
    ModeDesc d{};
    d.id = "setup"; d.name = "SETUP"; d.cn = "设置";
    d.color = rgb565(251, 191, 36);
    d.create = setup_create;
    ModeManager::instance()->register_mode(d);
}

} // namespace modes
} // namespace memoria
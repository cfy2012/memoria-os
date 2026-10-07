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

        draw_title(ui, "SETUP 设置", std::to_string(_cur + 1).append("/").append(std::to_string(_items.size())).c_str());

        const Item& it = _items[_cur];
        ui->draw_text(6, 44, it.name.c_str(), COLOR_LIGHT_GRAY);
        char val[24];
        snprintf(val, sizeof(val), "%s", it.value().c_str());
        ui->draw_text((SCREEN_W - 8 - strlen(val) * 8) / 2, 60, val, COLOR_YELLOW);

        /* 分组指示条 */
        ui->fill_rect({6, 82, 18, 3}, _group_color(_cur));
        ui->draw_text(6, 96, _group_name(_cur), COLOR_WHITE);

        draw_hint(ui, focused ? "左右 切换值 · Enter 保存 · 长按返回" : "");
    }

    bool mode_nav(const window::NavInput& ni) override {
        if (_net_active) return _net_nav(ni);
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
        if (ni.dir == window::NavEvent::NavBack && _net_active) {
            if (_net_edit != 0) { _net_cancel_edit(); }   /* 编辑中先取消编辑 */
            else _net_active = false;
            return true;
        }
        return ModeWindow::on_nav(ni);
    }

    /* 键盘字符输入：网络子界面编辑模式消费（WiFi 密码 / 热点配置 / 蓝牙名称） */
    bool on_key(uint16_t key, bool pressed) override {
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
        if (_net_active) {
            static const char* nf[6] = {"WiFi", "热点", "蓝牙", "扫描", "-", "返回"};
            return nf;
        }
        static const char* f[6] = {"-", "确认", "+", "默认", "保存", "菜单"};
        return f;
    }

    void on_fn(int idx) override {
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

    static const char* _group_name(int i) {
        static const char* g[4] = {"显示", "声音 · 网络", "系统", "商店 · 源"};
        return g[i < 1 ? 0 : (i < 4 ? 1 : (i < 9 ? 2 : 3))];
    }
    static uint16_t _group_color(int i) {
        static const uint16_t c[4] = {rgb565(96, 165, 250), rgb565(52, 211, 153), rgb565(167, 139, 250), rgb565(34, 211, 238)};
        return c[i < 1 ? 0 : (i < 4 ? 1 : (i < 9 ? 2 : 3))];
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
        } else {
            _upd_msg = "已是最新 " + info.latest;
            _upd_ready = false;
        }
    }

    /* 立即更新：后台任务下载安装（成功自动重启），UI 保持响应 */
    void _apply_update() {
        if (!_upd_ready || _upd_url.empty()) { _upd_msg = "先检查更新"; return; }
        _upd_msg = "下载更新中…";
        std::string* url = new std::string(_upd_url);
        BaseType_t ok = xTaskCreate([](void* arg) {
            std::string* u = static_cast<std::string*>(arg);
            package::firmware_apply(*u);   /* 成功即重启，不返回 */
            delete u;
            vTaskDelete(nullptr);
        }, "fw_ota", 8192, url, 3, nullptr);
        if (ok != pdPASS) { delete url; _upd_msg = "任务创建失败"; }
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
            case 6: break; /* 软件源：由商店页/TF卡管理 */
        }
    }

    void _save_all() {
        _save(0); _save(1); _save(2); _save(3); _save(4); _save(5); _save(6);
    }

    void _reset() {
        drivers::Ili9341::instance()->set_backlight(200);
        drivers::I2sAudio::instance()->set_volume(50);
        _auto_update = true; _hour12 = false; _sleep_min = 3;
        _save_all();
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

    void _render_net(window::UIRenderer* ui) {
        const char* sub_name[3] = {"WiFi", "热点", "蓝牙"};
        draw_title(ui, "SETUP · 网络", sub_name[_net_sub]);
        char line[56];

        auto* wm = WifiManager::instance();
        if (_net_sub == 0) {
            /* ---- WiFi：扫描列表 / 连接 ---- */
            if (wm->connected()) {
                snprintf(line, sizeof(line), "已连 %s  %s  %ddBm",
                         wm->current_ssid().c_str(), wm->ip().c_str(), wm->rssi());
                ui->draw_text(6, 40, line, COLOR_GREEN);
            } else if (wm->ap_active()) {
                ui->draw_text(6, 40, "热点模式（STA 未连接）", COLOR_YELLOW);
            } else {
                ui->draw_text(6, 40, "未连接", COLOR_LIGHT_GRAY);
            }
            std::string sv = wm->saved_ssid();
            snprintf(line, sizeof(line), "已存 %s", sv.empty() ? "无" : sv.c_str());
            ui->draw_text(6, 56, line, COLOR_LIGHT_GRAY);

            if (s_net_scanning) {
                ui->draw_text(6, 80, "扫描中...", COLOR_YELLOW);
            } else if (!s_net_scan_done) {
                ui->draw_text(6, 80, "按 Enter 扫描附近 WiFi", COLOR_WHITE);
            } else {
                size_t cnt = wm->scan_count();
                if (cnt == 0) {
                    ui->draw_text(6, 80, "未找到 WiFi（Enter 重扫）", COLOR_WHITE);
                } else {
                    size_t shown = cnt < 12 ? cnt : 12;
                    for (size_t i = 0; i < shown; i++) {
                        const ScanAp* ap = wm->scan_at(i);
                        snprintf(line, sizeof(line), "%s %s  %ddBm%s",
                                 i == (size_t)_net_sel ? "▶" : " ", ap->ssid, ap->rssi,
                                 ap->authmode ? " 锁" : "");
                        ui->draw_text(6, 80 + (int)i * 14, line,
                                      (int)i == _net_sel ? COLOR_WHITE : COLOR_LIGHT_GRAY);
                    }
                }
            }
            draw_hint(ui, "←→ 切页 · ↑↓ 选网 · Enter 扫描/连接");
        } else if (_net_sub == 1) {
            /* ---- 热点：开关 + SSID/密码编辑 ---- */
            ui->draw_text(6, 40, wm->ap_active() ? "热点: 开" : "热点: 关",
                          wm->ap_active() ? COLOR_GREEN : COLOR_LIGHT_GRAY);
            snprintf(line, sizeof(line), "SSID: %s", wm->ap_ssid().c_str());
            ui->draw_text(6, 60, line, _net_sel == 1 ? COLOR_WHITE : COLOR_LIGHT_GRAY);
            std::string apw = wm->ap_pass();
            snprintf(line, sizeof(line), "密码: %s", apw.empty() ? "(无)" : apw.c_str());
            ui->draw_text(6, 76, line, _net_sel == 2 ? COLOR_WHITE : COLOR_LIGHT_GRAY);
            ui->draw_text(6, 100, "Enter 开/关热点 · ↑↓ 选行再 Enter 编辑", COLOR_LIGHT_GRAY);
            if (wm->ap_active())
                ui->draw_text(6, 116, "设备连此热点后地址 192.168.4.1", COLOR_DARK_GRAY);
            draw_hint(ui, "←→ 切页 · Enter 开关/编辑");
        } else {
            /* ---- 蓝牙：开关 + 名称 ---- */
            auto* bm = BleManager::instance();
            ui->draw_text(6, 40, bm->enabled() ? "蓝牙: 开（重启生效）" : "蓝牙: 关",
                          bm->enabled() ? COLOR_GREEN : COLOR_LIGHT_GRAY);
            snprintf(line, sizeof(line), "名称: %s", bm->device_name().c_str());
            ui->draw_text(6, 60, line, _net_sel == 1 ? COLOR_WHITE : COLOR_LIGHT_GRAY);
            draw_hint(ui, "←→ 切页 · Enter 开关/编辑名称");
        }

        /* 编辑输入框 */
        if (_net_edit != 0) {
            ui->fill_rect({6, 240, 468, 30}, COLOR_DARK_GRAY);
            char in[80];
            if (_net_edit == 1 || _net_edit == 3) {
                snprintf(in, sizeof(in), "输入: %s", _net_masked());
            } else {
                snprintf(in, sizeof(in), "输入: %s", _net_edit_buf);
            }
            ui->draw_text(10, 246, in, COLOR_WHITE);
            ui->draw_text(6, 274, "Enter 确认 · Esc 取消", COLOR_LIGHT_GRAY);
            return;
        }

        /* 瞬时提示 */
        if (_net_msg_on) {
            uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
            if (now - _net_msg_tick < 2500) {
                ui->draw_text(6, 292, _net_msg, COLOR_YELLOW);
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
    }

    static bool s_net_scanning;
    static bool s_net_scan_done;

    std::vector<Item> _items;
    bool _auto_update = true;
    bool _hour12 = false;
    uint8_t _sleep_min = 3;
    std::string _upd_msg;      /* 更新状态缓存 */
    std::string _upd_url;
    bool _upd_ready = false;

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
/**
 * @file sys_mode.cpp
 * @brief SYS 系统模式
 *
 * 概览 + 系统信息 / 内存 / 自检动画 / 三层恢复出厂 / 关于。
 */

#include "mode_manager.hpp"
#include "battery_adc.hpp"
#include "private_fs.hpp"
#include "joystick.hpp"
#include "system_info.h"
#include "wifi_manager.hpp"
#include "ble_manager.hpp"
#include "keyboard_keymap.hpp"

extern "C" {
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
}

#include <cstdio>
#include <cstring>

namespace memoria {
using namespace drivers;
namespace modes {

class SysMode : public ModeWindow {
public:
    SysMode() {}

    void mode_render(window::UIRenderer* ui, bool focused) override {
        draw_title(ui, "SYS 系统", std::to_string(_tab + 1).append("/6").c_str());
        switch (_tab) {
            case 0: _render_overview(ui); break;
            case 1: _render_info(ui); break;
            case 2: _render_mem(ui); break;
            case 3: _render_test(ui); break;
            case 4: _render_about(ui); break;
            case 5: _render_net(ui); break;
        }
    }

    bool mode_nav(const window::NavInput& ni) override {
        if (_tab == 5) return _net_nav(ni);
        switch (ni.dir) {
            case window::NavEvent::NavUp:    _tab = (_tab - 1 + 6) % 6; break;
            case window::NavEvent::NavDown:  _tab = (_tab + 1) % 6; break;
            case window::NavEvent::NavEnter:
                if (_tab == 3) { _test_step++; if (_test_step > 3) _test_step = 0; }
                if (_tab == 4) request_confirm("恢复出厂? 此操作将清空记事与照片");
                break;
            default: return false;
        }
        return true;
    }

    const char* const* fn_labels() override {
        static const char* f[6] = {"信息", "内存", "自检", "恢复", "关于", "网络"};
        return f;
    }

    void on_fn(int idx) override {
        switch (idx) {
            case 0: _tab = 1; break;
            case 1: _tab = 2; break;
            case 2: _tab = 3; _test_step = 0; break;
            case 3: request_confirm("恢复出厂? 此操作将清空记事与照片"); break;
            case 4: _tab = 4; break;
            case 5: _tab = 5; break;
        }
    }

    void on_confirm_ok() override {
        if (_factory_armed) {
            /* 第三层确认后执行 */
            if (fs::PrivateFs::instance()->is_inited()) fs::PrivateFs::instance()->format();
            _factory_armed = false;
            _tab = 0;
        } else {
            _factory_armed = true;
            request_confirm("再次确认: 清空所有数据?");
        }
    }

    /* 键盘字符输入：仅网络页编辑模式消费（WiFi 密码 / 热点配置 / 蓝牙名称） */
    bool on_key(uint16_t key, bool pressed) override {
        if (_net_edit == 0) return false;
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

private:
    void _render_overview(window::UIRenderer* ui) {
        char line[40];
        snprintf(line, sizeof(line), "%s  v%s", MEMORIA_OS_NAME, MEMORIA_OS_VERSION);
        ui->draw_text(6, 40, line, COLOR_WHITE);
        uint32_t heap = esp_get_free_heap_size() / 1024;
        snprintf(line, sizeof(line), "内存 %u KB  电池 %d%%", (unsigned)heap, drivers::BatteryAdc::instance()->percent());
        ui->draw_text(6, 56, line, COLOR_LIGHT_GRAY);
        snprintf(line, sizeof(line), "运行 %d 秒", (int)(esp_timer_get_time() / 1000000));
        ui->draw_text(6, 72, line, COLOR_LIGHT_GRAY);
        draw_hint(ui, "↑↓ 切页 · Enter 自检/恢复");
    }

    void _render_info(window::UIRenderer* ui) {
        ui->draw_text(6, 40, "SoC   ESP32-S3  (Xtensa LX7)", COLOR_WHITE);
        ui->draw_text(6, 56, "屏    ILI9488 480x320", COLOR_WHITE);
        ui->draw_text(6, 72, "存储  TF 卡 + 私有区", COLOR_WHITE);
        ui->draw_text(6, 88, "音频  MAX98357A I2S", COLOR_WHITE);
        ui->draw_text(6, 104, "输入  KY-023 摇杆", COLOR_WHITE);
        /* 补充键盘条目（2×MCP23017 已定版） */
        ui->draw_text(6, 120, "键盘  2×MCP23017 60键", COLOR_WHITE);
    }

    void _render_mem(window::UIRenderer* ui) {
        uint32_t free_h = esp_get_free_heap_size() / 1024;
        /* 大块为最大连续空闲块（原先误用总空闲堆） */
        uint32_t big = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT) / 1024;
        uint32_t used = 0, total = 0, ue = 0, te = 0;
        if (fs::PrivateFs::instance()->is_inited())
            fs::PrivateFs::instance()->stats(used, total, ue, te);
        char line[40];
        snprintf(line, sizeof(line), "堆空闲  %u KB", (unsigned)free_h);
        ui->draw_text(6, 40, line, COLOR_WHITE);
        snprintf(line, sizeof(line), "大块    %u KB", (unsigned)big);
        ui->draw_text(6, 56, line, COLOR_LIGHT_GRAY);
        snprintf(line, sizeof(line), "私区    %u/%u KB", (unsigned)(used / 1024), (unsigned)(total / 1024));
        ui->draw_text(6, 72, line, COLOR_LIGHT_GRAY);
        snprintf(line, sizeof(line), "条目    %u/%u", (unsigned)ue, (unsigned)te);
        ui->draw_text(6, 88, line, COLOR_LIGHT_GRAY);
    }

    void _render_test(window::UIRenderer* ui) {
        char line[48];
        switch (_test_step) {
            case 0:
                ui->draw_text(6, 40, "按 Enter 开始自检", COLOR_WHITE);
                break;
            case 1: {   /* 屏幕：三色条目视确认 */
                ui->fill_rect({6, 40, 96, 60}, rgb565(255, 0, 0));
                ui->fill_rect({106, 40, 96, 60}, rgb565(0, 255, 0));
                ui->fill_rect({206, 40, 96, 60}, rgb565(0, 0, 255));
                ui->draw_text(6, 108, "看到红/绿/蓝三色条 → Enter", COLOR_LIGHT_GRAY);
                break;
            }
            case 2: {   /* 摇杆：实时读数 */
                int32_t x = 0, y = 0;
                bool btn = false;
                drivers::Joystick::instance()->read(x, y, btn);
                snprintf(line, sizeof(line), "X=%d  Y=%d  按钮=%s", (int)x, (int)y, btn ? "按下" : "松开");
                ui->draw_text(6, 40, line, COLOR_WHITE);
                ui->draw_text(6, 56, "拨动摇杆看数值变化 → Enter", COLOR_LIGHT_GRAY);
                break;
            }
            case 3: {   /* 电池：真实读数 */
                snprintf(line, sizeof(line), "电池 %d%%", (int)drivers::BatteryAdc::instance()->percent());
                ui->draw_text(6, 40, line, COLOR_WHITE);
                ui->draw_text(6, 56, "电池读数正常 → Enter 完成", COLOR_LIGHT_GRAY);
                break;
            }
        }
        /* 自检进度条 */
        ui->fill_rect({6, 76, 308, 6}, COLOR_DARK_GRAY);
        ui->fill_rect({6, 76, (int16_t)(308 * _test_step / 3), 6}, COLOR_GREEN);
    }

    void _render_about(window::UIRenderer* ui) {
        char line[44];
        snprintf(line, sizeof(line), "%s v%s", MEMORIA_OS_NAME, MEMORIA_OS_VERSION);
        ui->draw_text(6, 40, line, COLOR_WHITE);
        ui->draw_text(6, 56, "2026 · Open Source · MIT", COLOR_YELLOW);
        ui->draw_text(6, 76, "硬件  ESP32-S3 · ILI9488 480x320", COLOR_LIGHT_GRAY);
        ui->draw_text(6, 90, "存储  TF 卡 + 私有分区(加密表)", COLOR_LIGHT_GRAY);
        ui->draw_text(6, 104, "音频  MAX98357A I2S · 摇杆 KY-023", COLOR_LIGHT_GRAY);
        ui->draw_text(6, 118, "连接  WiFi · BLE · 软件商店/OTA", COLOR_LIGHT_GRAY);
        ui->draw_text(6, 138, "模式  相册/音乐/视频/时钟/记事", COLOR_WHITE);
        ui->draw_text(6, 152, "      计算器/脚本/商店/设置/文件/系统", COLOR_WHITE);
        ui->draw_text(6, 172, "内核  FreeRTOS + 模式管理器", COLOR_DARK_GRAY);
        ui->draw_text(6, 186, "脚本  memoria-script · .ms 解释器", COLOR_DARK_GRAY);
        draw_hint(ui, "Enter 恢复出厂（三层确认）");
    }

    /* ============================================================
     *  网络页：WiFi 扫描连接 / 热点 / 蓝牙（三子页 ←→ 切换）
     * ============================================================ */
    void _render_net(window::UIRenderer* ui) {
        char line[56];
        /* 子页标签 */
        const char* subs[3] = {"WiFi", "热点", "蓝牙"};
        int x = 6;
        for (int i = 0; i < 3; i++) {
            snprintf(line, sizeof(line), "%s%s", i == _net_sub ? "▶" : " ", subs[i]);
            ui->draw_text(x, 40, line, i == _net_sub ? COLOR_WHITE : COLOR_LIGHT_GRAY);
            x += 8 + 8 * (std::strlen(subs[i]) + (i == _net_sub ? 2 : 1)) + 6;
        }
        ui->draw_rect({6, 54, 468, 1}, COLOR_DARK_GRAY);

        auto* wm = WifiManager::instance();
        if (_net_sub == 0) {
            /* ---- WiFi ---- */
            if (wm->connected()) {
                snprintf(line, sizeof(line), "已连 %s  %s  %ddBm",
                         wm->current_ssid().c_str(), wm->ip().c_str(), wm->rssi());
                ui->draw_text(6, 60, line, COLOR_GREEN);
            } else if (wm->ap_active()) {
                ui->draw_text(6, 60, "热点模式（STA 未连接）", COLOR_YELLOW);
            } else {
                ui->draw_text(6, 60, "未连接", COLOR_LIGHT_GRAY);
            }
            std::string sv = wm->saved_ssid();
            snprintf(line, sizeof(line), "已存 %s", sv.empty() ? "无" : sv.c_str());
            ui->draw_text(6, 76, line, COLOR_LIGHT_GRAY);

            if (s_net_scanning) {
                ui->draw_text(6, 100, "扫描中...", COLOR_YELLOW);
            } else if (!s_net_scan_done) {
                ui->draw_text(6, 100, "按 Enter 扫描附近 WiFi", COLOR_WHITE);
            } else {
                size_t cnt = wm->scan_count();
                if (cnt == 0) {
                    ui->draw_text(6, 100, "未找到 WiFi（Enter 重扫）", COLOR_WHITE);
                } else {
                    size_t shown = cnt < 12 ? cnt : 12;
                    for (size_t i = 0; i < shown; i++) {
                        const ScanAp* ap = wm->scan_at(i);
                        snprintf(line, sizeof(line), "%s %s  %ddBm%s",
                                 i == (size_t)_net_sel ? "▶" : " ", ap->ssid, ap->rssi,
                                 ap->authmode ? " 锁" : "");
                        ui->draw_text(6, 100 + (int)i * 14, line,
                                      (int)i == _net_sel ? COLOR_WHITE : COLOR_LIGHT_GRAY);
                    }
                }
            }
            draw_hint(ui, "←→ 切页 · ↑↓ 选择 · Enter 扫描/连接");
        } else if (_net_sub == 1) {
            /* ---- 热点 ---- */
            ui->draw_text(6, 60, wm->ap_active() ? "热点: 开" : "热点: 关",
                          wm->ap_active() ? COLOR_GREEN : COLOR_LIGHT_GRAY);
            snprintf(line, sizeof(line), "SSID: %s", wm->ap_ssid().c_str());
            ui->draw_text(6, 76, line, _net_sel == 1 ? COLOR_WHITE : COLOR_LIGHT_GRAY);
            std::string apw = wm->ap_pass();
            snprintf(line, sizeof(line), "密码: %s", apw.empty() ? "(无)" : apw.c_str());
            ui->draw_text(6, 92, line, _net_sel == 2 ? COLOR_WHITE : COLOR_LIGHT_GRAY);
            ui->draw_text(6, 112, "Enter 开/关热点 · ↑↓ 选行再 Enter 编辑", COLOR_LIGHT_GRAY);
            if (wm->ap_active())
                ui->draw_text(6, 128, "设备连此热点后地址 192.168.4.1", COLOR_DARK_GRAY);
            draw_hint(ui, "←→ 切页 · Enter 开关/编辑");
        } else {
            /* ---- 蓝牙 ---- */
            auto* bm = BleManager::instance();
            ui->draw_text(6, 60, bm->enabled() ? "蓝牙: 开（重启生效）" : "蓝牙: 关",
                          bm->enabled() ? COLOR_GREEN : COLOR_LIGHT_GRAY);
            snprintf(line, sizeof(line), "名称: %s", bm->device_name().c_str());
            ui->draw_text(6, 76, line, _net_sel == 1 ? COLOR_WHITE : COLOR_LIGHT_GRAY);
            draw_hint(ui, "←→ 切页 · Enter 开关/编辑名称");
        }

        /* 编辑输入框 */
        if (_net_edit != 0) {
            ui->fill_rect({6, 240, 468, 30}, COLOR_DARK_GRAY);
            char in[80];
            if (_net_edit == 1 || _net_edit == 3) {
                snprintf(in, sizeof(in), "输入: %s", _net_masked());
            } else if (_net_edit == 2) {
                snprintf(in, sizeof(in), "输入: %s", _net_edit_buf);
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
            if (ni.dir == window::NavEvent::NavBack)  { _net_cancel_edit(); return true; }
            return true;   /* 编辑期间吞掉其它方向 */
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

    void _net_scan_async() {
        if (s_net_scanning) return;
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

    int _tab = 0;
    int _test_step = 0;
    bool _factory_armed = false;

    /* 网络页状态 */
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

bool SysMode::s_net_scanning = false;
bool SysMode::s_net_scan_done = false;

static std::shared_ptr<window::Window> sys_create() {
    return std::make_shared<SysMode>();
}

void sys_mode_register() {
    ModeDesc d{};
    d.id = "sys"; d.name = "SYS"; d.cn = "系统";
    d.color = rgb565(148, 163, 184);
    d.create = sys_create;
    ModeManager::instance()->register_mode(d);
}

} // namespace modes
} // namespace memoria
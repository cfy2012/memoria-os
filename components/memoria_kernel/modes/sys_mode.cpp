/**
 * @file sys_mode.cpp
 * @brief SYS 系统模式
 *
 * 概览 + 系统信息 / 内存 / 自检动画 / 三层恢复出厂 / 关于。
 */

#include "mode_manager.hpp"
#include "ili9341.hpp"
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
#include <string>
#include <algorithm>

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
    /* 网络子页设置行高 */
    static const int SET_ROW_H = 26;

    /* 信息小卡：标签（次文字）+ 数值（状态色）；permille>=0 时补一条占用率进度 */
    void _info_card(window::UIRenderer* ui, int x, int y, int w, int h,
                    const char* label, const std::string& value, uint16_t vc, int permille = -1) {
        window::Rect rc{static_cast<int16_t>(x), static_cast<int16_t>(y),
                        static_cast<int16_t>(w), static_cast<int16_t>(h)};
        ui->card(rc, 8, COLOR_CARD, COLOR_STROKE);
        ui->draw_text_fit(static_cast<int16_t>(x + 10), static_cast<int16_t>(y + 6), label,
                          COLOR_TEXT_DIM, w - 20);
        ui->draw_text_fit(static_cast<int16_t>(x + 10),
                          static_cast<int16_t>(y + (permille >= 0 ? 20 : 22)), value, vc, w - 20);
        if (permille >= 0)
            ui->progress({static_cast<int16_t>(x + 10), static_cast<int16_t>(y + h - 9),
                          static_cast<int16_t>(w - 20), 4}, permille,
                         vc == COLOR_TEXT ? _accent : vc, COLOR_BG_SOFT, 2);
    }

    void _render_overview(window::UIRenderer* ui) {
        char line[48];
        const int cw = (SCREEN_W - 12 - 8) / 2;
        const int x2 = 6 + cw + 8;
        const int ch = 54;
        const uint32_t heap = esp_get_free_heap_size() / 1024;
        const int bat = drivers::BatteryAdc::instance()->percent();
        const uint16_t bc = bat <= 10 ? COLOR_DANGER : (bat <= 20 ? COLOR_WARN : COLOR_TEXT);

        snprintf(line, sizeof(line), "%s v%s", MEMORIA_OS_NAME, MEMORIA_OS_VERSION);
        _info_card(ui, 6, 34, cw, ch, "系统版本", line, COLOR_TEXT);
        snprintf(line, sizeof(line), "%u KB", (unsigned)heap);
        _info_card(ui, x2, 34, cw, ch, "可用内存", line,
                   heap < 20 ? COLOR_WARN : COLOR_TEXT);
        snprintf(line, sizeof(line), "%d%%", bat);
        _info_card(ui, 6, 34 + ch + 4, cw, ch, "电池电量", line, bc,
                   bat < 0 ? -1 : bat * 10);
        snprintf(line, sizeof(line), "%d 秒", (int)(esp_timer_get_time() / 1000000));
        _info_card(ui, x2, 34 + ch + 4, cw, ch, "运行时间", line, COLOR_TEXT);
        draw_hint(ui, "上下 切页 | Enter 自检/恢复");
    }

    void _render_info(window::UIRenderer* ui) {
        const int cw = (SCREEN_W - 12 - 8) / 2;
        const int x2 = 6 + cw + 8;
        const int ch = 48;
        _info_card(ui, 6, 34, cw, ch, "SoC", "ESP32-S3 (Xtensa LX7)", COLOR_TEXT);
        _info_card(ui, x2, 34, cw, ch, "屏幕", "ILI9488 480x320", COLOR_TEXT);
        _info_card(ui, 6, 34 + ch + 4, cw, ch, "存储", "TF 卡 + 私有区", COLOR_TEXT);
        _info_card(ui, x2, 34 + ch + 4, cw, ch, "音频", "MAX98357A I2S", COLOR_TEXT);
        _info_card(ui, 6, 34 + (ch + 4) * 2, cw, ch, "输入", "KY-023 摇杆", COLOR_TEXT);
        _info_card(ui, x2, 34 + (ch + 4) * 2, cw, ch, "键盘", "MCP23017 x2 60 键", COLOR_TEXT);
    }

    void _render_mem(window::UIRenderer* ui) {
        const int cw = (SCREEN_W - 12 - 8) / 2;
        const int x2 = 6 + cw + 8;
        const int ch = 56;
        const uint32_t free_h = esp_get_free_heap_size() / 1024;
        /* 大块为最大连续空闲块（原先误用总空闲堆） */
        const uint32_t big = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT) / 1024;
        uint32_t used = 0, total = 0, ue = 0, te = 0;
        if (fs::PrivateFs::instance()->is_inited())
            fs::PrivateFs::instance()->stats(used, total, ue, te);

        char line[40];
        snprintf(line, sizeof(line), "%u KB", (unsigned)free_h);
        _info_card(ui, 6, 34, cw, ch, "堆空闲", line,
                   free_h < 10 ? COLOR_DANGER : (free_h < 20 ? COLOR_WARN : COLOR_TEXT));
        snprintf(line, sizeof(line), "%u KB", (unsigned)big);
        _info_card(ui, x2, 34, cw, ch, "最大连续块", line,
                   big < 10 ? COLOR_WARN : COLOR_TEXT);
        snprintf(line, sizeof(line), "%u/%u KB", (unsigned)(used / 1024), (unsigned)(total / 1024));
        _info_card(ui, 6, 34 + ch + 4, cw, ch, "私有区", line, COLOR_TEXT,
                   total ? (int)((uint64_t)used * 1000 / total) : 0);
        snprintf(line, sizeof(line), "%u/%u", (unsigned)ue, (unsigned)te);
        _info_card(ui, x2, 34 + ch + 4, cw, ch, "条目", line, COLOR_TEXT,
                   te ? (int)((uint64_t)ue * 1000 / te) : 0);
    }

    void _render_test(window::UIRenderer* ui) {
        char line[48];
        window::Rect box{6, 34, static_cast<int16_t>(SCREEN_W - 12), 160};
        ui->card(box, 8, COLOR_CARD, COLOR_STROKE);
        switch (_test_step) {
            case 0:
                ui->draw_text_fit(16, 50, "按 Enter 开始自检", COLOR_TEXT, box.w - 20);
                ui->draw_text_fit(16, 70, "屏幕 | 摇杆 | 电池 三步", COLOR_TEXT_DIM, box.w - 20);
                break;
            case 1: {   /* 屏幕：三色条目视确认（纯色为测试项，不做着色/圆角） */
                ui->fill_rect({16, 50, 140, 56}, rgb565(255, 0, 0));
                ui->fill_rect({166, 50, 140, 56}, rgb565(0, 255, 0));
                ui->fill_rect({316, 50, 140, 56}, rgb565(0, 0, 255));
                ui->draw_text_fit(16, 120, "看到 红/绿/蓝 三色条 | Enter 下一步",
                                  COLOR_TEXT_DIM, box.w - 24);
                break;
            }
            case 2: {   /* 摇杆：实时读数 */
                int32_t x = 0, y = 0;
                bool btn = false;
                drivers::Joystick::instance()->read(x, y, btn);
                snprintf(line, sizeof(line), "X=%d  Y=%d  按钮=%s", (int)x, (int)y, btn ? "按下" : "松开");
                ui->draw_text_fit(16, 50, line, COLOR_TEXT, box.w - 20);
                ui->draw_text_fit(16, 70, "拨动摇杆看数值变化 | Enter 下一步",
                                  COLOR_TEXT_DIM, box.w - 20);
                break;
            }
            case 3: {   /* 电池：真实读数 */
                const int p = (int)drivers::BatteryAdc::instance()->percent();
                snprintf(line, sizeof(line), "电池 %d%%", p);
                ui->draw_text_fit(16, 50, line,
                                  p <= 10 ? COLOR_DANGER : (p <= 20 ? COLOR_WARN : COLOR_TEXT),
                                  box.w - 20);
                ui->draw_text_fit(16, 70, "电池读数正常 | Enter 完成", COLOR_TEXT_DIM, box.w - 20);
                break;
            }
        }
        /* 自检整体进度 */
        ui->draw_text_fit(8, 208, "自检进度", COLOR_TEXT_DIM, 80);
        ui->progress({6, 226, static_cast<int16_t>(SCREEN_W - 12), 8},
                     _test_step * 1000 / 3, _accent, COLOR_BG_SOFT, 4);
        draw_hint(ui, "Enter 下一步 | 上下 切页");
    }

    void _render_about(window::UIRenderer* ui) {
        char line[64];
        window::Rect box{6, 34, static_cast<int16_t>(SCREEN_W - 12), 196};
        ui->card(box, 8, COLOR_CARD, COLOR_STROKE);
        snprintf(line, sizeof(line), "%s v%s", MEMORIA_OS_NAME, MEMORIA_OS_VERSION);
        ui->draw_text_fit(16, 44, line, COLOR_TEXT, box.w - 20);
        ui->draw_text_fit(16, 62, "2026 | Open Source | MIT", COLOR_TEXT_DIM, box.w - 20);
        ui->divider_h(16, 82, static_cast<int16_t>(box.w - 32), COLOR_STROKE);
        ui->draw_text_fit(16, 92, "硬件  ESP32-S3 | ILI9488 480x320", COLOR_TEXT_DIM, box.w - 24);
        ui->draw_text_fit(16, 110, "存储  TF 卡 + 私有分区(加密表)", COLOR_TEXT_DIM, box.w - 24);
        ui->draw_text_fit(16, 128, "音频  MAX98357A I2S | 摇杆 KY-023", COLOR_TEXT_DIM, box.w - 24);
        ui->draw_text_fit(16, 146, "连接  WiFi | BLE | 软件商店/OTA", COLOR_TEXT_DIM, box.w - 24);
        ui->draw_text_fit(16, 164, "模式  相册/音乐/视频/时钟/记事", COLOR_TEXT, box.w - 24);
        ui->draw_text_fit(16, 182, "      计算器/脚本/商店/设置/文件/系统", COLOR_TEXT, box.w - 24);
        ui->draw_text_fit(16, 200, "内核  FreeRTOS + 模式管理器 | .ms 解释器",
                          COLOR_TEXT_DIM, box.w - 24);
        draw_hint(ui, "Enter 恢复出厂（三层确认）");
    }

    /* ============================================================
     *  网络页：WiFi 扫描连接 / 热点 / 蓝牙（三子页 ←→ 切换）
     * ============================================================ */
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
            const int sx = x + w - 12 - 34;
            window::Rect tr{static_cast<int16_t>(sx), static_cast<int16_t>(ry + 4), 34, 14};
            ui->fill_round_rect(tr, 7, COLOR_BG_SOFT);
            ui->draw_round_rect(tr, 7, sw_on ? _accent : COLOR_STROKE_HI, 1);
            drivers::Ili9341::instance()->fill_circle(
                static_cast<int16_t>(sw_on ? sx + 25 : sx + 9), static_cast<int16_t>(ry + 11), 5,
                sw_on ? _accent : COLOR_STROKE_HI);
        } else {
            const int vmax = w - 150;
            const int vw = window::UIRenderer::text_width_utf8(value);
            ui->draw_text_fit(static_cast<int16_t>(x + w - 12 - std::min(vw, vmax)),
                              static_cast<int16_t>(ry + 5), value,
                              sel ? _accent : COLOR_TEXT_DIM, vmax);
        }
    }

    void _render_net(window::UIRenderer* ui) {
        char line[80];
        const char* subs[3] = {"WiFi", "热点", "蓝牙"};

        /* 子页标签胶囊：选中 = 提亮面 + 强调描边 */
        int tx = 8;
        for (int i = 0; i < 3; i++) {
            const int tw = window::UIRenderer::text_width_utf8(subs[i]);
            window::Rect tab{static_cast<int16_t>(tx), 34, static_cast<int16_t>(tw + 16), 18};
            const bool on = (i == _net_sub);
            ui->fill_round_rect(tab, 9, on ? COLOR_CARD_HI : COLOR_CARD);
            ui->draw_round_rect(tab, 9, on ? _accent : COLOR_STROKE, 1);
            ui->draw_text(static_cast<int16_t>(tx + 8), 37, subs[i],
                          on ? COLOR_TEXT : COLOR_TEXT_DIM);
            tx += tw + 16 + 6;
        }

        auto* wm = WifiManager::instance();
        if (_net_sub == 0) {
            /* ---- WiFi：状态卡 + 扫描列表 ---- */
            window::Rect st{6, 58, static_cast<int16_t>(SCREEN_W - 12), 46};
            ui->card(st, 8, COLOR_CARD, COLOR_STROKE);
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
                std::string sv = wm->saved_ssid();
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
            draw_hint(ui, "左右 切页 | 上下 选择 | Enter 扫描/连接");
        } else if (_net_sub == 1) {
            /* ---- 热点 ---- */
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
            /* ---- 蓝牙 ---- */
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
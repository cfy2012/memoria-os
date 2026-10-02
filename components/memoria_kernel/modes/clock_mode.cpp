/**
 * @file clock_mode.cpp
 * @brief CLOCK 时钟模式
 *
 * 子页：时钟 / 秒表 / 倒计时 / 闹钟 / 纪念日
 * 功能键：F1 样式切换 · F2 秒表 · F3 倒计时 · F4 闹钟 · F5 纪念日 · F6 菜单
 */

#include "mode_manager.hpp"
#include "rtc_clock.hpp"
#include "ili9341.hpp"

extern "C" {
#include <esp_timer.h>
}

#include <cstdio>
#include <cstring>

#include <cmath>
#include "system_info.h"

namespace memoria {
using namespace drivers;
namespace modes {

class ClockMode : public ModeWindow {
public:
    ClockMode() { _reload_dday(); }

    void mode_render(window::UIRenderer* ui, bool focused) override {
        char right[24];
        snprintf(right, sizeof(right), "%d/5 %s", _tab + 1, _h24 ? "24h" : "12h");
        draw_title(ui, "CLOCK 时钟", right);
        switch (_tab) {
            case 0: _render_clock(ui); break;
            case 1: _render_sw(ui); break;
            case 2: _render_cd(ui); break;
            case 3: _render_alarm(ui); break;
            case 4: _render_dday(ui); break;
        }
    }

    bool mode_nav(const window::NavInput& ni) override {
        switch (_tab) {
            case 0: if (ni.dir == window::NavEvent::NavEnter) _style ^= 1; break;
            case 1:
                if (ni.dir == window::NavEvent::NavEnter) {
                    if (!_sw_on) _sw_start = (uint32_t)(esp_timer_get_time() / 1000);
                    _sw_on = !_sw_on;
                }
                break;
            case 2:
                if (ni.dir == window::NavEvent::NavEnter) {
                    if (!_cd_on) _cd_start = (uint32_t)(esp_timer_get_time() / 1000);
                    _cd_on = !_cd_on;
                }
                break;
            case 3: if (ni.dir == window::NavEvent::NavEnter) _alarm_on = !_alarm_on; break;
            default: break;
        }
        return true;
    }

    const char* const* fn_labels() override {
        static const char* f[6] = {"样式", "秒表", "倒计时", "闹钟", "纪念日", "菜单"};
        return f;
    }

    void on_fn(int idx) override {
        switch (idx) {
            case 0: if (_tab == 0) _style ^= 1; else _tab = 0; break;
            case 1: _tab = 1; _sw_on = false; break;
            case 2: _tab = 2; break;
            case 3: _tab = 3; break;
            case 4: _tab = 4; break;
            case 5: _h24 = !_h24; break;
        }
    }

private:
    static const char* _wdays(uint8_t d) {
        static const char* w[7] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
        return w[d % 7];
    }

    void _render_clock(window::UIRenderer* ui) {
        auto t = drivers::RtcClock::instance()->now();
        if (_style == 1) { _render_face(ui, t); return; }
        char big[16];
        if (_h24) snprintf(big, sizeof(big), "%02u:%02u", t.hour, t.minute);
        else {
            uint8_t h = t.hour % 12; if (h == 0) h = 12;
            snprintf(big, sizeof(big), "%02u:%02u", h, t.minute);
        }
        ui->draw_text((SCREEN_W - strlen(big) * 8) / 2, 44, big, COLOR_WHITE);
        char date[24];
        snprintf(date, sizeof(date), "%04u-%02u-%02u %s", t.year, t.month, t.day, _wdays(t.day_of_week));
        ui->draw_text((SCREEN_W - strlen(date) * 8) / 2, 62, date, COLOR_LIGHT_GRAY);
    }

    /* 模拟表盘：圆 + 12 刻度 + 时/分指针（ILI9341 驱动原语） */
    void _render_face(window::UIRenderer* ui, const drivers::RtcTime& t) {
        (void)ui;
        auto* lcd = drivers::Ili9341::instance();
        const int cx = SCREEN_W / 2, cy = 112, r = 72;
        lcd->draw_circle(cx, cy, r, COLOR_WHITE);
        for (int i = 0; i < 12; i++) {
            double a = i * 30.0 * 3.14159265 / 180.0;
            int x1 = cx + (int)((r - 6) * sin(a)), y1 = cy - (int)((r - 6) * cos(a));
            int x2 = cx + (int)((r - 1) * sin(a)),  y2 = cy - (int)((r - 1) * cos(a));
            lcd->draw_line(x1, y1, x2, y2, COLOR_LIGHT_GRAY);
        }
        double ha = (t.hour % 12 + t.minute / 60.0) * 30.0 * 3.14159265 / 180.0;
        double ma = t.minute * 6.0 * 3.14159265 / 180.0;
        lcd->draw_line(cx, cy, cx + (int)(36 * sin(ha)), cy - (int)(36 * cos(ha)), COLOR_YELLOW);
        lcd->draw_line(cx, cy, cx + (int)(52 * sin(ma)), cy - (int)(52 * cos(ma)), COLOR_WHITE);
        lcd->fill_circle(cx, cy, 3, COLOR_WHITE);
    }

    void _render_sw(window::UIRenderer* ui) {
        if (_sw_on) {
            uint32_t ms = (uint32_t)(esp_timer_get_time() / 1000) - _sw_start;
            char s[16]; snprintf(s, sizeof(s), "%02u:%02u.%u", (unsigned)(ms / 60000), (unsigned)((ms / 1000) % 60), (unsigned)((ms / 100) % 10));
            ui->draw_text((SCREEN_W - strlen(s) * 8) / 2, 46, s, COLOR_WHITE);
        } else {
            ui->draw_text((SCREEN_W - 5 * 8) / 2, 46, "00:00.0", COLOR_LIGHT_GRAY);
        }
        draw_hint(ui, _sw_on ? "Enter 暂停" : "Enter 开始");
    }

    void _render_cd(window::UIRenderer* ui) {
        int left = _cd_total;
        if (_cd_on) {
            uint32_t elapsed = (uint32_t)(esp_timer_get_time() / 1000) - _cd_start;
            left = _cd_total - (int)(elapsed / 1000);
            if (left < 0) { left = 0; _cd_on = false; }
        }
        char s[16]; snprintf(s, sizeof(s), "%02d:%02d", left / 60, left % 60);
        ui->draw_text((SCREEN_W - strlen(s) * 8) / 2, 46, s, COLOR_WHITE);
        draw_hint(ui, _cd_on ? "Enter 暂停" : "Enter 开始");
    }

    void _render_alarm(window::UIRenderer* ui) {
        ui->draw_text(20, 44, "07:00 起床", _alarm_on ? COLOR_YELLOW : COLOR_LIGHT_GRAY);
        ui->draw_text(20, 60, "22:00 晚安", COLOR_WHITE);
        char st[12]; snprintf(st, sizeof(st), "ALARM %s", _alarm_on ? "ON" : "OFF");
        draw_hint(ui, st);
    }

    void _render_dday(window::UIRenderer* ui) {
        char s[24];
        snprintf(s, sizeof(s), "距离 %u 月 %u 日", MEMORIA_BIRTHDAY_MMDD / 100, MEMORIA_BIRTHDAY_MMDD % 100);
        ui->draw_text((SCREEN_W - strlen(s) * 8) / 2, 42, s, COLOR_LIGHT_GRAY);
        char d[16]; snprintf(d, sizeof(d), "%d 天", _dday_left);
        ui->draw_text((SCREEN_W - strlen(d) * 8) / 2, 58, d, COLOR_YELLOW);
    }

    void _reload_dday() {
        /* 目标日期：MEMORIA_BIRTHDAY_MMDD 12月25日，取今年（若已过取明年） */
        auto now = drivers::RtcClock::instance()->now();
        uint16_t y = now.year;
        int mm = MEMORIA_BIRTHDAY_MMDD / 100, dd = MEMORIA_BIRTHDAY_MMDD % 100;
        struct tm target{};
        target.tm_year = y - 1900; target.tm_mon = mm - 1; target.tm_mday = dd;
        time_t tgt = mktime(&target);
        time_t nowt = time(nullptr);
        if (tgt < nowt) { target.tm_year = (y + 1) - 1900; tgt = mktime(&target); }
        _dday_left = (int)((tgt - nowt + 86399) / 86400);
    }

    int _tab = 0;
    int _style = 0;          /* 0 数字 / 1 表盘（预留） */
    bool _h24 = true;

    /* 秒表 */
    bool _sw_on = false;
    uint32_t _sw_start = 0;

    /* 倒计时 */
    bool _cd_on = false;
    int _cd_total = 300;
    uint32_t _cd_start = 0;

    /* 闹钟 */
    bool _alarm_on = true;

    int _dday_left = 0;
};

static std::shared_ptr<window::Window> clock_create() {
    return std::make_shared<ClockMode>();
}

void clock_mode_register() {
    ModeDesc d{};
    d.id = "clock"; d.name = "CLOCK"; d.cn = "时钟";
    d.color = rgb565(248, 113, 113);
    d.create = clock_create;
    ModeManager::instance()->register_mode(d);
}

} // namespace modes
} // namespace memoria
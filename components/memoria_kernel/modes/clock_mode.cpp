/**
 * @file clock_mode.cpp
 * @brief CLOCK 时钟模式
 *
 * 子页：时钟 / 秒表 / 倒计时 / 闹钟 / 纪念日
 * 功能键：F1 样式切换 | F2 秒表 | F3 倒计时 | F4 闹钟 | F5 纪念日 | F6 菜单
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

    /* ---------- 绘制小工具 ---------- */
    static void _center(window::UIRenderer* ui, int16_t y, const char* s,
                        uint16_t c, int scale = 1) {
        const int w = window::UIRenderer::text_width_utf8(s, scale);
        ui->draw_text_utf8(static_cast<int16_t>((SCREEN_W - w) / 2), y, s, c, scale);
    }

    /* 小胶囊：底部圆角块 + 单行小字（只用 ASCII，避免中文字高溢出） */
    static void _chip(window::UIRenderer* ui, int16_t x, int16_t y, const char* s,
                      uint16_t fg, uint16_t bg = COLOR_CARD_HI) {
        const int w = window::UIRenderer::text_width_utf8(s) + 12;
        ui->fill_round_rect({x, y, static_cast<int16_t>(w), 16}, 8, bg);
        ui->draw_text_fit(static_cast<int16_t>(x + 6), static_cast<int16_t>(y + 2),
                          s, fg, w - 8);
    }

    static void _chip_right(window::UIRenderer* ui, int16_t right_x, int16_t y,
                            const char* s, uint16_t fg, uint16_t bg = COLOR_CARD_HI) {
        const int w = window::UIRenderer::text_width_utf8(s) + 12;
        _chip(ui, static_cast<int16_t>(right_x - w), y, s, fg, bg);
    }

    /* 细弧线：0 度 = 12 点方向，顺时针 a0..a1 度 */
    static void _arc(int16_t cx, int16_t cy, int16_t r, int a0, int a1, uint16_t c) {
        auto* lcd = Ili9341::instance();
        int px = 0, py = 0;
        for (int a = a0; a <= a1; a += 2) {
            const double rad = a * 3.14159265 / 180.0;
            const int x = cx + static_cast<int>(r * std::sin(rad));
            const int y = cy - static_cast<int>(r * std::cos(rad));
            if (a > a0) lcd->draw_line((uint16_t)px, (uint16_t)py, (uint16_t)x, (uint16_t)y, c);
            px = x; py = y;
        }
    }

    static void _thick_line(int x0, int y0, int x1, int y1, uint16_t c, int w) {
        auto* lcd = Ili9341::instance();
        for (int i = 0; i < w; i++)
            lcd->draw_line((uint16_t)(x0 + i), (uint16_t)y0, (uint16_t)(x1 + i), (uint16_t)y1, c);
    }

    /* 小卡片骨架：圆角面 + 描边 + 左侧强调竖条 + 图标块 + 标题 + 右上状态胶囊 */
    void _mini_card(window::UIRenderer* ui, const window::Rect& r, const char* title,
                    const char* pill, uint16_t pill_fg) {
        ui->card(r, 10, COLOR_CARD, COLOR_STROKE);
        ui->fill_round_rect({static_cast<int16_t>(r.x + 1), static_cast<int16_t>(r.y + 14), 3, 20},
                            1, _accent);
        ui->fill_round_rect({static_cast<int16_t>(r.x + 12), static_cast<int16_t>(r.y + 12), 26, 26},
                            8, COLOR_CARD_HI);
        ui->fill_round_rect({static_cast<int16_t>(r.x + 21), static_cast<int16_t>(r.y + 21), 8, 8},
                            4, _accent);
        ui->draw_text_utf8(static_cast<int16_t>(r.x + 46), static_cast<int16_t>(r.y + 17),
                           title, COLOR_TEXT_DIM);
        _chip_right(ui, static_cast<int16_t>(r.x + r.w - 10),
                    static_cast<int16_t>(r.y + 14), pill, pill_fg);
    }

    uint32_t _sw_ms() const {
        if (!_sw_on) return 0;
        return (uint32_t)(esp_timer_get_time() / 1000) - _sw_start;
    }

    int _cd_left() {
        int left = _cd_total;
        if (_cd_on) {
            const uint32_t elapsed = (uint32_t)(esp_timer_get_time() / 1000) - _cd_start;
            left = _cd_total - (int)(elapsed / 1000);
            /* 与旧版一致：归零即自动停止 */
            if (left < 0) { left = 0; _cd_on = false; }
        }
        return left;
    }

    /* 只读预览：主界面不改变倒计时运行状态 */
    int _cd_left_peek() const {
        int left = _cd_total;
        if (_cd_on) {
            const uint32_t elapsed = (uint32_t)(esp_timer_get_time() / 1000) - _cd_start;
            left = _cd_total - (int)(elapsed / 1000);
            if (left < 0) left = 0;
        }
        return left;
    }

    void _render_clock(window::UIRenderer* ui) {
        auto t = drivers::RtcClock::instance()->now();
        if (_style == 1) {
            _render_face(ui, t);
            draw_hint(ui, "Enter 返回数字");
            return;
        }

        char big[8];
        const bool pm = (t.hour >= 12);
        if (_h24) {
            snprintf(big, sizeof(big), "%02u:%02u", t.hour, t.minute);
        } else {
            uint8_t h = t.hour % 12; if (h == 0) h = 12;
            snprintf(big, sizeof(big), "%02u:%02u", h, t.minute);
        }

        /* 主卡片：大号时间 + 日期 + 当天进度 */
        const window::Rect main{12, 38, 456, 104};
        ui->card(main, 12, COLOR_CARD, COLOR_STROKE);
        ui->fill_round_rect({13, 54, 3, 72}, 1, _accent);
        ui->draw_text(24, 46, _h24 ? "24H" : "12H", COLOR_TEXT_DIM);
        if (_h24) _chip_right(ui, 458, 44, "LOCAL", COLOR_TEXT_DIM);
        else      _chip_right(ui, 458, 44, pm ? "PM" : "AM", _accent);

        _center(ui, 62, big, COLOR_TEXT, 3);

        char date[40];
        snprintf(date, sizeof(date), "%04u-%02u-%02u  %s",
                 t.year, t.month, t.day, _wdays(t.day_of_week));
        _center(ui, 106, date, COLOR_TEXT_DIM);

        const int day_sec = t.hour * 3600 + t.minute * 60 + t.second;
        ui->progress({28, 130, 424, 4}, day_sec * 1000 / 86400, _accent, COLOR_CARD_HI, 2);

        /* 两张小卡片：闹钟 / 计时 */
        const window::Rect lc{12, 154, 220, 108};
        _mini_card(ui, lc, "闹钟", _alarm_on ? "ON" : "OFF",
                   _alarm_on ? COLOR_OK : COLOR_TEXT_DIM);
        ui->draw_text_utf8(24, 196, "07:00", COLOR_TEXT, 2);
        ui->draw_text_utf8(108, 202, "起床", COLOR_TEXT_DIM);
        ui->draw_text_utf8(24, 226, "22:00 晚安", COLOR_TEXT_DIM);

        const window::Rect rc{248, 154, 220, 108};
        const uint32_t sw_ms = _sw_ms();
        const bool sw_used = (_sw_on || sw_ms > 0);
        _mini_card(ui, rc, "计时",
                   _sw_on ? "RUN" : (sw_used ? "PAUSE" : "READY"),
                   _sw_on ? COLOR_OK : (sw_used ? COLOR_WARN : COLOR_TEXT_DIM));

        char sws[20];
        snprintf(sws, sizeof(sws), "%02u:%02u.%u",
                 (unsigned)(sw_ms / 60000), (unsigned)((sw_ms / 1000) % 60),
                 (unsigned)((sw_ms / 100) % 10));
        ui->draw_text_utf8(260, 196, sws, COLOR_TEXT, 2);

        const int cd_left = _cd_left_peek();
        char cds[16];
        snprintf(cds, sizeof(cds), "%02d:%02d", cd_left / 60, cd_left % 60);
        ui->draw_text_utf8(260, 226, "倒计时", COLOR_TEXT_DIM);
        ui->draw_text(260 + 56, 226, cds, _accent);

        const int cd_total = _cd_total > 0 ? _cd_total : 1;
        int cd_permille = cd_left * 1000 / cd_total;
        if (cd_permille < 0) cd_permille = 0;
        if (cd_permille > 1000) cd_permille = 1000;
        ui->progress({260, 246, 196, 5}, cd_permille, _accent, COLOR_CARD_HI, 2);

        draw_hint(ui, _style == 0 ? "Enter 切换表盘" : "Enter 返回数字");
    }

    /* 模拟表盘：细环 + 刻度 + 秒弧 + 时/分/秒指针（不填色块） */
    void _render_face(window::UIRenderer* ui, const drivers::RtcTime& t) {
        const window::Rect card{86, 36, 308, 244};
        ui->card(card, 12, COLOR_CARD, COLOR_STROKE);
        ui->fill_round_rect({static_cast<int16_t>(card.x + 1), static_cast<int16_t>(card.y + 18),
                             3, 20}, 1, _accent);
        ui->draw_text(static_cast<int16_t>(card.x + 12), static_cast<int16_t>(card.y + 18),
                      "ANALOG", COLOR_TEXT_DIM);
        _chip_right(ui, card.x + card.w - 10, card.y + 14, _h24 ? "24H" : "12H",
                    COLOR_TEXT_DIM);

        const int16_t cx = 240, cy = 148, r = 92;
        auto* lcd = Ili9341::instance();
        lcd->draw_circle(cx, cy, r, COLOR_STROKE);
        lcd->draw_circle(cx, cy, static_cast<int16_t>(r - 1), COLOR_CARD_HI);

        for (int i = 0; i < 12; i++) {
            const double a = i * 30.0 * 3.14159265 / 180.0;
            const int len = (i % 3 == 0) ? 10 : 5;
            const int x1 = cx + static_cast<int>((r - 4 - len) * std::sin(a));
            const int y1 = cy - static_cast<int>((r - 4 - len) * std::cos(a));
            const int x2 = cx + static_cast<int>((r - 4) * std::sin(a));
            const int y2 = cy - static_cast<int>((r - 4) * std::cos(a));
            lcd->draw_line((uint16_t)x1, (uint16_t)y1, (uint16_t)x2, (uint16_t)y2,
                           (i % 3 == 0) ? COLOR_TEXT_DIM : COLOR_STROKE_HI);
        }

        /* 秒弧：0..当前秒，强调色细弧 */
        if (t.second > 0) _arc(cx, cy, static_cast<int16_t>(r - 6), 0, t.second * 6, _accent);

        const double ha = (t.hour % 12 + t.minute / 60.0) * 30.0 * 3.14159265 / 180.0;
        const double ma = (t.minute + t.second / 60.0) * 6.0 * 3.14159265 / 180.0;
        const double sa = t.second * 6.0 * 3.14159265 / 180.0;

        _thick_line(cx, cy, cx + static_cast<int>(44 * std::sin(ha)),
                    cy - static_cast<int>(44 * std::cos(ha)), COLOR_TEXT, 3);
        _thick_line(cx, cy, cx + static_cast<int>(66 * std::sin(ma)),
                    cy - static_cast<int>(66 * std::cos(ma)), COLOR_TEXT, 2);
        lcd->draw_line((uint16_t)cx, (uint16_t)cy,
                       (uint16_t)(cx + static_cast<int>(78 * std::sin(sa))),
                       (uint16_t)(cy - static_cast<int>(78 * std::cos(sa))), _accent);

        lcd->fill_circle(cx, cy, 5, COLOR_CARD);
        lcd->fill_circle(cx, cy, 3, _accent);

        char date[40];
        snprintf(date, sizeof(date), "%04u-%02u-%02u  %s",
                 t.year, t.month, t.day, _wdays(t.day_of_week));
        _center(ui, 250, date, COLOR_TEXT_DIM);
    }

    void _render_sw(window::UIRenderer* ui) {
        const uint32_t ms = _sw_ms();
        char s[20];
        snprintf(s, sizeof(s), "%02u:%02u.%u",
                 (unsigned)(ms / 60000), (unsigned)((ms / 1000) % 60),
                 (unsigned)((ms / 100) % 10));

        const window::Rect main{12, 44, 456, 120};
        ui->card(main, 12, COLOR_CARD, COLOR_STROKE);
        ui->fill_round_rect({13, 60, 3, 88}, 1, _accent);
        ui->draw_text_utf8(24, 52, "秒表", COLOR_TEXT_DIM);
        _chip_right(ui, 458, 50, _sw_on ? "RUN" : "PAUSE",
                    _sw_on ? COLOR_OK : COLOR_WARN);
        _center(ui, 74, s, COLOR_TEXT, 3);
        ui->progress({28, 126, 424, 6}, (int)((ms % 60000) / 60), _accent, COLOR_CARD_HI, 3);

        const window::Rect info{12, 180, 456, 76};
        ui->card(info, 10, COLOR_CARD, COLOR_STROKE);
        ui->fill_round_rect({13, 196, 3, 44}, 1, _accent);
        ui->draw_text_utf8(24, 190, "Enter 开始 / 暂停", COLOR_TEXT);
        ui->draw_text_utf8(24, 212, "F2 秒表 | F3 倒计时 | F4 闹钟", COLOR_TEXT_DIM);

        draw_hint(ui, _sw_on ? "Enter 暂停" : "Enter 开始");
    }

    void _render_cd(window::UIRenderer* ui) {
        const int left = _cd_left();
        char s[16];
        snprintf(s, sizeof(s), "%02d:%02d", left / 60, left % 60);

        const window::Rect main{12, 44, 456, 120};
        ui->card(main, 12, COLOR_CARD, COLOR_STROKE);
        ui->fill_round_rect({13, 60, 3, 88}, 1, _accent);
        ui->draw_text_utf8(24, 52, "倒计时", COLOR_TEXT_DIM);
        _chip_right(ui, 458, 50, _cd_on ? "RUN" : "PAUSE",
                    _cd_on ? COLOR_OK : COLOR_WARN);
        _center(ui, 74, s, COLOR_TEXT, 3);

        const int total = _cd_total > 0 ? _cd_total : 1;
        int permille = left * 1000 / total;
        if (permille < 0) permille = 0;
        if (permille > 1000) permille = 1000;
        ui->progress({28, 126, 424, 6}, permille, _accent, COLOR_CARD_HI, 3);

        const window::Rect info{12, 180, 456, 76};
        ui->card(info, 10, COLOR_CARD, COLOR_STROKE);
        ui->fill_round_rect({13, 196, 3, 44}, 1, _accent);
        ui->draw_text_utf8(24, 190, "Enter 开始 / 暂停", COLOR_TEXT);
        char total_s[64];
        snprintf(total_s, sizeof(total_s), "总时长 %02d:%02d | 剩余 %d 秒",
                 _cd_total / 60, _cd_total % 60, left);
        ui->draw_text_fit(24, 212, total_s, COLOR_TEXT_DIM, 420);

        draw_hint(ui, _cd_on ? "Enter 暂停" : "Enter 开始");
    }

    /* 闹钟行：卡片 + 左侧强调竖条 + 时间 + 说明 + 状态胶囊 */
    void _alarm_row(window::UIRenderer* ui, int16_t y, const char* hm, const char* note) {
        const window::Rect r{12, y, 456, 60};
        ui->card(r, 10, COLOR_CARD, COLOR_STROKE);
        ui->fill_round_rect({13, static_cast<int16_t>(y + 15), 3, 30}, 1, _accent);
        ui->draw_text_utf8(24, static_cast<int16_t>(y + 16), hm, COLOR_TEXT, 2);
        ui->draw_text_utf8(24 + 88, static_cast<int16_t>(y + 22), note, COLOR_TEXT_DIM);
        _chip_right(ui, 458, static_cast<int16_t>(y + 22), _alarm_on ? "ON" : "OFF",
                    _alarm_on ? COLOR_OK : COLOR_TEXT_DIM);
    }

    void _render_alarm(window::UIRenderer* ui) {
        _alarm_row(ui, 44, "07:00", "起床");
        _alarm_row(ui, 112, "22:00", "晚安");

        const window::Rect info{12, 184, 456, 68};
        ui->card(info, 10, COLOR_CARD, COLOR_STROKE);
        ui->fill_round_rect({13, 198, 3, 40}, 1, _accent);
        ui->draw_text_utf8(24, 192, "Enter 开关闹钟", COLOR_TEXT);
        ui->draw_text_fit(24, 214, "闹钟响铃由系统事件统一处理，F4 可回到本页",
                          COLOR_TEXT_DIM, 420);

        char st[20];
        snprintf(st, sizeof(st), "ALARM %s", _alarm_on ? "ON" : "OFF");
        draw_hint(ui, st);
    }

    void _render_dday(window::UIRenderer* ui) {
        const window::Rect card{12, 44, 456, 184};
        ui->card(card, 12, COLOR_CARD, COLOR_STROKE);
        ui->fill_round_rect({13, 58, 3, 120}, 1, _accent);
        ui->draw_text_utf8(24, 54, "生日倒数", COLOR_TEXT_DIM);
        _chip_right(ui, 458, 52, "D-DAY", _accent);

        char sub[40];
        snprintf(sub, sizeof(sub), "距离 %u 月 %u 日",
                 MEMORIA_BIRTHDAY_MMDD / 100, MEMORIA_BIRTHDAY_MMDD % 100);
        ui->draw_text_utf8(24, 76, sub, COLOR_TEXT_DIM);

        char num[16];
        snprintf(num, sizeof(num), "%d", _dday_left);
        const int nw = window::UIRenderer::text_width_utf8(num, 3);
        const int uw = window::UIRenderer::text_width_utf8("天", 2);
        const int x0 = (SCREEN_W - (nw + 8 + uw)) / 2;
        ui->draw_text_utf8(static_cast<int16_t>(x0), 104, num, COLOR_TEXT, 3);
        ui->draw_text_utf8(static_cast<int16_t>(x0 + nw + 8), 110, "天", COLOR_TEXT_DIM, 2);

        int permille = 1000 - _dday_left * 1000 / 365;
        if (permille < 0) permille = 0;
        if (permille > 1000) permille = 1000;
        ui->progress({36, 176, 408, 6}, permille, _accent, COLOR_CARD_HI, 3);

        char tail[40];
        snprintf(tail, sizeof(tail), "下一个生日 %02u-%02u",
                 MEMORIA_BIRTHDAY_MMDD / 100, MEMORIA_BIRTHDAY_MMDD % 100);
        _center(ui, 194, tail, COLOR_TEXT_DIM);
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
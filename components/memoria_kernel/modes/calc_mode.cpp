/**
 * @file calc_mode.cpp
 * @brief CALC 计算器（重写版）+ 开机锁屏入口
 *
 * 与旧版（4×6 摇杆选键网格）完全不同：
 *   - 直接吃键盘字符流，输入框自由打式子，Enter 求值
 *   - a/b 自动排成分数块（分子 / 横线 / 分母），继续打 / 嵌套成 (1/2)/3
 *   - 光标默认停在最内层分母末尾，TAB 把光标移到整个分数块右侧
 *   - 含 x/y/z 的等式走 memoria_math 解方程，含变量非等式当函数式
 *   - 结果出来后输入 DRAW 画 y=f(x)，ESC 退回输入区
 *   - 锁屏实例：输入框打 #login 切密码页，密码对了解锁进桌面
 */

#include "mode_manager.hpp"
#include "math_solver.hpp"
#include "ili9341.hpp"
#include "keyboard_keymap.hpp"

extern "C" {
#include <esp_timer.h>
}

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <string>
#include <vector>

namespace memoria {
using namespace drivers;
using namespace input;
namespace modes {

namespace {

/* 字号倍率：输入/结果区用 2 倍（ASCII 16×24） */
constexpr int S = 2;
constexpr int CELL_H = window::UIRenderer::FONT_H * S;   /* 24 */

/* ---------- 数值格式化（去掉浮点尾噪与 -0） ---------- */
std::string fmt_num(double v) {
    if (std::fabs(v) < 1e-12) v = 0.0;
    char b[40];
    std::snprintf(b, sizeof(b), "%.10g", v);
    return b;
}

/* ---------- 带变量 x 的递归下降求值（算术与绘图共用） ---------- */
class Eval {
public:
    Eval(const std::string& s, double xv) : _s(s), _x(xv) {}

    double run(bool& err) {
        _p = 0; _err = false;
        double v = _expr();
        _skip();
        if (_p < _s.size()) _err = true;
        err = _err;
        return v;
    }

private:
    void _skip() { while (_p < _s.size() && _s[_p] == ' ') _p++; }

    double _expr() {
        double v = _term();
        for (;;) {
            _skip();
            if (_p < _s.size() && (_s[_p] == '+' || _s[_p] == '-')) {
                const char op = _s[_p++];
                const double r = _term();
                v = (op == '+') ? v + r : v - r;
            } else break;
        }
        return v;
    }

    double _term() {
        double v = _unary();
        for (;;) {
            _skip();
            if (_p < _s.size() && (_s[_p] == '*' || _s[_p] == '/')) {
                const char op = _s[_p++];
                const double r = _unary();
                if (op == '*') {
                    v *= r;
                } else {
                    if (r == 0.0) { _err = true; return 0.0; }
                    v /= r;
                }
            } else break;
        }
        return v;
    }

    double _unary() {
        _skip();
        if (_p < _s.size() && _s[_p] == '-') { _p++; return -_unary(); }
        if (_p < _s.size() && _s[_p] == '+') { _p++; return _unary(); }
        return _power();
    }

    /* ^ 右结合：2^3^2 = 2^9 */
    double _power() {
        const double base = _primary();
        _skip();
        if (_p < _s.size() && _s[_p] == '^') {
            _p++;
            const double e = _unary();
            return std::pow(base, e);
        }
        return base;
    }

    double _primary() {
        _skip();
        if (_p < _s.size() && _s[_p] == '(') {
            _p++;
            const double v = _expr();
            _skip();
            if (_p < _s.size() && _s[_p] == ')') _p++; else _err = true;
            return v;
        }
        if (_p < _s.size() && (_s[_p] == 'x' || _s[_p] == 'X')) { _p++; return _x; }
        return _num();
    }

    double _num() {
        const size_t st = _p;
        while (_p < _s.size() && (std::isdigit((unsigned char)_s[_p]) || _s[_p] == '.')) _p++;
        if (st == _p) { _err = true; return 0.0; }
        return std::atof(_s.substr(st, _p - st).c_str());
    }

    const std::string& _s;
    double _x;
    size_t _p = 0;
    bool   _err = false;
};

/* ---------- 分数块排版 ---------- */
struct Box { int w, h; };

int text_w(const std::string& s) { return window::UIRenderer::text_width_utf8(s, S); }

/* 只含数字/小数点/斜杠，且至少一个斜杠 -> 可排成分数链 */
bool split_frac(const std::string& s, std::vector<std::string>& out) {
    bool slash = false;
    for (char ch : s) {
        if (ch == '/') { slash = true; continue; }
        if (std::isdigit((unsigned char)ch) || ch == '.') continue;
        return false;
    }
    if (!slash || s.empty()) return false;

    out.clear();
    std::string cur;
    for (char ch : s) {
        if (ch == '/') {
            out.push_back(cur.empty() ? std::string("?") : cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
    out.push_back(cur.empty() ? std::string("?") : cur);
    return out.size() >= 2;
}

/* 前 n 段构成的分数块尺寸：左结合，(a/b)/c 的外层分母是 c */
Box frac_measure(const std::vector<std::string>& v, int n) {
    if (n <= 1) return Box{ text_w(v[0]), CELL_H };
    const Box num = frac_measure(v, n - 1);
    const Box den{ text_w(v[n - 1]), CELL_H };
    return Box{ std::max(num.w, den.w) + 10, num.h + den.h + 6 };
}

/* 绘制分数块，回写光标坐标（最内层分母末尾 = 默认光标位） */
void frac_draw(window::UIRenderer* ui, const std::vector<std::string>& v, int n,
               int x, int y, uint16_t c, int& cx, int& cy) {
    if (n <= 1) {
        ui->draw_text_utf8((int16_t)x, (int16_t)y, v[0], c, S);
        cx = x + text_w(v[0]);
        cy = y;
        return;
    }
    const Box num = frac_measure(v, n - 1);
    const Box den{ text_w(v[n - 1]), CELL_H };
    const int w = std::max(num.w, den.w) + 10;

    frac_draw(ui, v, n - 1, x + (w - num.w) / 2, y, c, cx, cy);

    const int ly = y + num.h + 2;
    const int x2 = std::min(x + w, static_cast<int>(SCREEN_W) - 1);
    Ili9341::instance()->draw_line((uint16_t)x, (uint16_t)ly, (uint16_t)x2, (uint16_t)ly, c);
    Ili9341::instance()->draw_line((uint16_t)x, (uint16_t)(ly + 1), (uint16_t)x2, (uint16_t)(ly + 1), c);

    const int by = ly + 4;
    const int bx = x + (w - den.w) / 2;
    ui->draw_text_utf8((int16_t)bx, (int16_t)by, v[n - 1], c, S);
    cx = bx + den.w;
    cy = by;
}

} /* namespace */

/* ============================================================
 *  CalcWindow：计算器 / 锁屏 / 密码 / 绘图
 * ============================================================ */
class CalcWindow : public ModeWindow {
public:
    explicit CalcWindow(bool locked) : _is_lock(locked) {
        if (locked) {
            _rect = {0, 0, static_cast<int16_t>(SCREEN_W), static_cast<int16_t>(SCREEN_H)};
            set_rect(_rect);
        }
    }

    void on_render() override {
        auto* ui = window::UIRenderer::instance();
        const int top = _is_lock ? 0 : 16;
        ui->fill_rect({0, static_cast<int16_t>(top), SCREEN_W,
                       static_cast<int16_t>(SCREEN_H - top)}, COLOR_BG);
        /* 非锁屏沿用框架标题栏；锁屏全屏接管，不画标题 */
        if (!_is_lock) draw_title(ui, "CALC 计算器", "STD");

        if (_view == View::Plot)  { _render_plot(ui, top);  return; }
        if (_view == View::Pass)  { _render_pass(ui);       return; }
        _render_expr(ui, top);
    }

    bool on_nav(const window::NavInput& ni) override {
        if (_is_lock) return true;           /* 锁屏时摇杆一律不响应 */
        return ModeWindow::on_nav(ni);
    }

    bool on_key(uint16_t key, bool pressed) override {
        if (!pressed) return true;

        if (_view == View::Plot) {
            if (key == input::K_ESC) {
                _view = View::Expr;
                _expr.clear();
            }
            return true;
        }

        if (_view == View::Pass) {
            if (key == input::K_ESC) {
                _view = View::Expr;
                _pass.clear();
                _hint.clear();
                _expr.clear();
                return true;
            }
            if (key >= '0' && key <= '9') {
                if (_pass.size() < 8) _pass += static_cast<char>(key);
                _hint.clear();
                if (_pass.size() == 8) _check_pass();
                return true;
            }
            if (key == input::K_BACKSPACE) {
                if (!_pass.empty()) _pass.pop_back();
                _hint.clear();
                return true;
            }
            if (key == input::K_ENTER) { _check_pass(); return true; }
            return true;
        }

        /* ---- 算式输入区 ---- */
        if (key == input::K_ENTER)   { _evaluate(); return true; }
        if (key == input::K_BACKSPACE) {
            if (!_expr.empty()) _expr.pop_back();
            return true;
        }
        if (key == input::K_ESC) {
            _expr.clear();
            _result.clear();
            _hint.clear();
            _plot.clear();
            _frac_out = false;
            return true;
        }
        /* 空格 = 光标切换：分数分母末尾 <-> 分数块右侧，再按一次回来 */
        if (key == input::K_SPACE || key == input::K_TAB || key == input::K_F2) {
            _frac_out = !_frac_out;
            return true;
        }

        if (key >= 0x20 && key <= 0x7E) {
            if (_expr.size() >= 44) return true;
            _expr += static_cast<char>(key);
            _frac_out = false;
            /* #login 打完即切，不用等 Enter */
            if (_is_lock && _expr == "#login") { _enter_pass(); return true; }
            /* 结果出来后打 DRAW 立即进绘图 */
            if (!_plot.empty() && (_expr == "DRAW" || _expr == "draw")) {
                _expr.clear();
                _view = View::Plot;
                return true;
            }
            return true;
        }
        return true;
    }

    void mode_render(window::UIRenderer*, bool) override {}
    bool mode_nav(const window::NavInput&) override { return true; }
    const char* const* fn_labels() override {
        static const char* f[6] = {"", "", "", "", "", ""};
        return f;
    }
    void on_fn(int) override {}

private:
    enum class View { Expr, Pass, Plot };

    /* ---------- 绘制小工具 ---------- */
    static void _chip_right(window::UIRenderer* ui, int16_t right_x, int16_t y,
                            const char* s, uint16_t fg, uint16_t bg = COLOR_CARD_HI) {
        const int w = window::UIRenderer::text_width_utf8(s) + 12;
        const int16_t x = static_cast<int16_t>(right_x - w);
        ui->fill_round_rect({x, y, static_cast<int16_t>(w), 16}, 8, bg);
        ui->draw_text_fit(static_cast<int16_t>(x + 6), static_cast<int16_t>(y + 2),
                          s, fg, w - 8);
    }

    /* 底部说明行：1px 分隔 + 单行小字 */
    static void _footer(window::UIRenderer* ui, const char* help) {
        const int16_t y = static_cast<int16_t>(SCREEN_H - 22);
        ui->fill_rect({0, y, static_cast<int16_t>(SCREEN_W), 1}, COLOR_STROKE);
        ui->draw_text_fit(12, static_cast<int16_t>(y + 5), help, COLOR_TEXT_DIM,
                          SCREEN_W - 24);
    }

    /* ---------------- 算式视图 ---------------- */
    void _render_expr(window::UIRenderer* ui, int top) {
        (void)top;
        const int16_t dy = _is_lock ? 62 : 36;
        /* 键帽已移除，显示屏卡片纵向铺满内容区 */
        const int16_t dh = _is_lock ? 200 : 236;

        /* 锁屏顶栏：品牌点 + LOCKED 胶囊 */
        if (_is_lock) {
            ui->fill_round_rect({20, 14, 6, 6}, 3, _accent);
            ui->draw_text(32, 10, "MEMORIA OS", COLOR_TEXT);
            _chip_right(ui, 468, 12, "LOCKED", COLOR_WARN);
        }

        /* 显示屏卡片 */
        const window::Rect disp{12, dy, 456, dh};
        ui->card(disp, 10, COLOR_BG_SOFT, COLOR_STROKE);
        ui->fill_round_rect({static_cast<int16_t>(disp.x + 2),
                             static_cast<int16_t>(dy + 3), 3, 12}, 1, _accent);
        ui->draw_text(static_cast<int16_t>(disp.x + 12), static_cast<int16_t>(dy + 1),
                      "CALC", COLOR_TEXT_DIM);

        const int ix = 24;
        const int iy = dy + 14;
        int cx = ix, cy = iy;

        std::vector<std::string> segs;
        if (split_frac(_expr, segs)) {
            frac_draw(ui, segs, static_cast<int>(segs.size()), ix, iy, COLOR_TEXT, cx, cy);
            if (_frac_out) {
                const Box b = frac_measure(segs, static_cast<int>(segs.size()));
                cx = ix + b.w + 2;
                cy = iy;
            }
        } else {
            ui->draw_text_utf8(ix, iy, _expr, COLOR_TEXT, S);
            cx = ix + text_w(_expr);
            cy = iy;
        }

        /* 光标：400ms 闪烁（强调色竖条） */
        if (_expr.size() < 44) {
            const int64_t ms = esp_timer_get_time() / 1000;
            if ((ms / 400) % 2 == 0) {
                ui->fill_rect({static_cast<int16_t>(cx + 1), static_cast<int16_t>(cy), 2,
                               static_cast<int16_t>(CELL_H)}, _accent);
            }
        }

        /* 结果：右对齐大号数字（超宽先按像素裁剪） */
        if (!_result.empty()) {
            std::string r = _result;
            for (char& ch : r) if (ch == '\n' || ch == '\r') ch = ' ';
            if (r.size() > 26) r = r.substr(0, 26);
            const std::string shown =
                window::UIRenderer::clip_text(r, static_cast<int>(disp.w - 24) / S);
            const int rw = window::UIRenderer::text_width_utf8(shown, S);
            ui->draw_text_utf8(static_cast<int16_t>(disp.x + disp.w - 12 - rw),
                               static_cast<int16_t>(disp.y + disp.h - 32),
                               shown, _accent, S);
        }

        /* 提示 / 键帽 / 底部说明 */
        if (!_hint.empty()) {
            ui->draw_text_fit(24, static_cast<int16_t>(dy + dh + 2), _hint,
                              COLOR_WARN, SCREEN_W - 48);
        }

        if (_is_lock) {
            _footer(ui, "数字键输入 | Enter 确认 | ESC 返回");
        } else {
            _footer(ui, "Enter 计算 | 空格 切光标 | ESC 清空");
        }
    }

    /* ---------------- 密码视图（锁屏全屏） ---------------- */
    void _render_pass(window::UIRenderer* ui) {
        ui->fill_round_rect({20, 14, 6, 6}, 3, _accent);
        ui->draw_text(32, 10, "MEMORIA OS", COLOR_TEXT);
        _chip_right(ui, 468, 12, "LOCKED", COLOR_WARN);

        const window::Rect box{48, 64, 384, 196};
        ui->card(box, 12, COLOR_CARD, COLOR_STROKE);
        ui->fill_round_rect({49, 82, 3, 20}, 1, _accent);

        const std::string t1 = "请输入密码";
        const int w1 = window::UIRenderer::text_width_utf8(t1, 2);
        ui->draw_text_utf8(static_cast<int16_t>((SCREEN_W - w1) / 2), 82, t1, COLOR_TEXT, 2);

        const std::string t2 = "8 位数字";
        const int w2 = window::UIRenderer::text_width_utf8(t2);
        ui->draw_text_utf8(static_cast<int16_t>((SCREEN_W - w2) / 2), 120, t2, COLOR_TEXT_DIM);

        /* 8 格密码指示：已输入 = 强调色圆角方块 + 深色圆点 */
        const int bw = 24, gap = 10;
        const int total = 8 * bw + 7 * gap;
        const int x0 = (SCREEN_W - total) / 2;
        for (int i = 0; i < 8; i++) {
            const int x = x0 + i * (bw + gap);
            const window::Rect sr{static_cast<int16_t>(x), 146, bw, bw};
            if (i < static_cast<int>(_pass.size())) {
                ui->fill_round_rect(sr, 7, _accent);
                ui->fill_round_rect({static_cast<int16_t>(x + 9), 155, 6, 6}, 3, COLOR_BG);
            } else {
                ui->fill_round_rect(sr, 7, COLOR_BG_SOFT);
                ui->draw_round_rect(sr, 7, COLOR_STROKE, 1);
            }
        }

        if (!_hint.empty()) {
            const int w = window::UIRenderer::text_width_utf8(_hint, 1);
            ui->draw_text_utf8(static_cast<int16_t>((SCREEN_W - w) / 2), 184,
                               _hint, COLOR_DANGER, 1);
        }

        ui->divider_h(64, 208, 352, COLOR_STROKE);
        const std::string help = "数字键输入 | Enter 确认 | ESC 返回";
        const int hw = window::UIRenderer::text_width_utf8(help);
        ui->draw_text_utf8(static_cast<int16_t>((SCREEN_W - hw) / 2), 220, help, COLOR_TEXT_DIM);

        _footer(ui, "锁屏密码与设置页一致");
    }

    /* ---------------- 绘图视图 ---------------- */
    void _render_plot(window::UIRenderer* ui, int top) {
        (void)top;
        ui->draw_text_fit(14, 34, "y = " + _plot, COLOR_TEXT_DIM, SCREEN_W - 28);

        const int x0 = 14, y0 = 56;
        const int w  = SCREEN_W - 28;
        const int h  = 232;

        const window::Rect canvas{6, 52, static_cast<int16_t>(SCREEN_W - 12), 240};
        ui->fill_round_rect(canvas, 8, COLOR_BG_SOFT);
        ui->draw_round_rect(canvas, 8, COLOR_STROKE, 1);

        const double xmin = -8.0, xmax = 8.0;
        const double ymin = -5.0, ymax = 5.0;

        auto px = [&](double x) { return x0 + static_cast<int>((x - xmin) / (xmax - xmin) * w); };
        auto py = [&](double y) { return y0 + h - static_cast<int>((y - ymin) / (ymax - ymin) * h); };

        /* 网格 + 坐标轴 */
        for (int gx = -8; gx <= 8; gx += 2) {
            const int X = px(gx);
            Ili9341::instance()->draw_vline((uint16_t)X, (uint16_t)y0, (uint16_t)(y0 + h),
                                            COLOR_CARD_HI);
        }
        for (int gy = -4; gy <= 4; gy += 2) {
            const int Y = py(gy);
            Ili9341::instance()->draw_hline((uint16_t)x0, (uint16_t)(x0 + w),
                                            (uint16_t)Y, COLOR_CARD_HI);
        }
        Ili9341::instance()->draw_vline((uint16_t)px(0), (uint16_t)y0, (uint16_t)(y0 + h),
                                        COLOR_STROKE_HI);
        Ili9341::instance()->draw_hline((uint16_t)x0, (uint16_t)(x0 + w), (uint16_t)py(0),
                                        COLOR_STROKE_HI);

        /* 曲线：逐像素采样连折线（强调色） */
        const uint16_t curve = _accent;
        bool has_prev = false;
        int prev_x = 0, prev_y = 0;
        for (int i = 0; i <= w; i++) {
            const double xv = xmin + (static_cast<double>(i) / w) * (xmax - xmin);
            bool err = false;
            const double yv = Eval(_plot, xv).run(err);
            if (err || std::isnan(yv) || std::isinf(yv)) { has_prev = false; continue; }
            const int Y = py(yv);
            if (Y < y0 - 40 || Y > y0 + h + 40) { has_prev = false; continue; }
            const int X = x0 + i;
            if (has_prev) {
                Ili9341::instance()->draw_line((uint16_t)prev_x, (uint16_t)prev_y,
                                               (uint16_t)X, (uint16_t)Y, curve);
            } else {
                Ili9341::instance()->draw_pixel((uint16_t)X, (uint16_t)Y, curve);
            }
            prev_x = X; prev_y = Y; has_prev = true;
        }

        _footer(ui, "ESC 返回算式");
    }

    /* ---------------- 逻辑 ---------------- */
    void _enter_pass() {
        _view = View::Pass;
        _pass.clear();
        _hint.clear();
        _expr.clear();
        _result.clear();
        _plot.clear();
    }

    void _check_pass() {
        /* 密码存在 NVS（设置页可改），没存过时是默认 20120201 */
        if (_pass == ModeManager::instance()->login_password()) {
            ModeManager::instance()->unlock();
            return;
        }
        _pass.clear();
        _hint = "密码错误";
    }

    void _evaluate() {
        if (_expr.empty()) return;
        const std::string e = _expr;

        if (e[0] == '#') {
            if (e == "#login" && _is_lock) { _enter_pass(); return; }
            _result.clear();
            _hint = "未知命令 " + e;
            _expr.clear();
            return;
        }

        const bool has_eq  = e.find('=') != std::string::npos;
        const bool has_var = e.find('x') != std::string::npos ||
                             e.find('y') != std::string::npos ||
                             e.find('z') != std::string::npos;

        if (has_eq) {
            const auto r = math::solve_equations(e);
            if (r.ok) {
                _result = r.detail;
                _hint   = r.kind;
                _plot.clear();
            } else {
                _result = "无法求解";
                _hint   = r.detail;
                _plot.clear();
            }
            _expr.clear();
            _frac_out = false;
            return;
        }

        if (has_var) {
            _result = "函数 y = " + e;
            _hint   = "DRAW 画图 | ESC 清空";
            _plot   = e;
            _expr.clear();
            _frac_out = false;
            return;
        }

        bool err = false;
        const double v = Eval(e, 0.0).run(err);
        if (err) {
            _result = "表达式错误";
            _hint.clear();
        } else {
            _result = fmt_num(v);
            _hint.clear();
        }
        _plot.clear();
        _expr.clear();
        _frac_out = false;
    }

    View        _view = View::Expr;
    bool        _is_lock = false;
    bool        _frac_out = false;   /* TAB 后：光标在分数块右侧 */
    std::string _expr;
    std::string _result;
    std::string _hint;
    std::string _pass;
    std::string _plot;
};

/* ============================================================
 *  注册 / 工厂
 * ============================================================ */

std::shared_ptr<window::Window> create_lock_calc() {
    return std::make_shared<CalcWindow>(true);
}

static std::shared_ptr<window::Window> calc_create() {
    return std::make_shared<CalcWindow>(false);
}

void calc_mode_register() {
    ModeDesc d{};
    d.id = "calc"; d.name = "CALC"; d.cn = "计算器";
    d.color = rgb565(52, 211, 153);
    d.create = calc_create;
    ModeManager::instance()->register_mode(d);
}

} // namespace modes
} // namespace memoria

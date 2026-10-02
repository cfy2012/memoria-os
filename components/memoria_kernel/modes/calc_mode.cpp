/**
 * @file calc_mode.cpp
 * @brief CALC 计算器模式
 *
 * 按键网格（4×5）+ 自写四则表达式解析器（无 eval，安全）。
 * 摇杆移动按键光标，Enter 按下；支持 7 8 9 ÷ 4 5 6 × 1 2 3 − 0 . % + 与 = C ⌫。
 */

#include "mode_manager.hpp"

#include <cstdio>
#include <cstring>

#include <string>
#include <vector>
#include <cmath>
#include <cctype>
#include <cstdlib>

namespace memoria {
using namespace drivers;
namespace modes {

class CalcMode : public ModeWindow {
public:
    CalcMode() {}

    void mode_render(window::UIRenderer* ui, bool focused) override {
        draw_title(ui, "CALC 计算器", _hist.empty() ? "0 条" : std::to_string(_hist.size()).c_str());

        /* 显示区 */
        char expr[40];
        snprintf(expr, sizeof(expr), "%s", _expr.c_str());
        ui->fill_rect({4, 34, (int16_t)(SCREEN_W - 8), 22}, COLOR_DEEP_BLUE);
        ui->draw_text(6, 36, expr, COLOR_WHITE);
        ui->draw_text(6, 48, _result.c_str(), COLOR_YELLOW);

        /* 按键网格 4 列 × 5 行：光标 _cur 0..19 */
        static const char* keys[20] = {
            "7", "8", "9", "÷",
            "4", "5", "6", "×",
            "1", "2", "3", "−",
            "0", ".", "%", "+",
            "C", "⌫", "=", " ",
        };
        const int cols = 4;
        const int bw = (SCREEN_W - 12) / cols;   /* 77 */
        const int bh = 22;
        for (int i = 0; i < 20; i++) {
            int x = 4 + (i % cols) * bw;
            int y = 62 + (i / cols) * (bh + 2);
            bool op = (i % 4 == 3);
            ui->fill_rect({(int16_t)x, (int16_t)y, (int16_t)(bw - 2), (int16_t)bh}, op ? COLOR_DEEP_BLUE : COLOR_DARK_GRAY);
            if (i == _cur && focused) ui->draw_cursor({(int16_t)x, (int16_t)y, (int16_t)(bw - 2), (int16_t)bh}, COLOR_WHITE, 2);
            ui->draw_text(x + (bw - 2 - 8) / 2, y + (bh - 12) / 2, keys[i], COLOR_WHITE);
        }
    }

    bool mode_nav(const window::NavInput& ni) override {
        switch (ni.dir) {
            case window::NavEvent::NavUp:    _cur = _cur - 4 >= 0 ? _cur - 4 : _cur; break;
            case window::NavEvent::NavDown:  _cur = _cur + 4 < 20 ? _cur + 4 : _cur; break;
            case window::NavEvent::NavLeft:  _cur = (_cur - 1 + 20) % 20; break;
            case window::NavEvent::NavRight: _cur = (_cur + 1) % 20; break;
            case window::NavEvent::NavEnter: _press(_cur); break;
            default: return false;
        }
        return true;
    }

    const char* const* fn_labels() override {
        static const char* f[6] = {"清空", "退格", "科学", "进制", "历史", "菜单"};
        return f;
    }

    void on_fn(int idx) override {
        switch (idx) {
            case 0: _expr.clear(); _result = "0"; break;
            case 1: if (!_expr.empty()) _expr.pop_back(); break;
            case 2: _result = "√/^ 预留（真机科学页）"; break;
            case 3: _result = "DEC (进制切换预留)"; break;
            case 4: _result = _hist.empty() ? "（历史为空）" : _hist.back(); break;
            case 5: _result = "四则 + 括号 + 百分号"; break;
        }
    }

private:
    void _press(int i) {
        static const char* keys[20] = {
            "7", "8", "9", "÷", "4", "5", "6", "×",
            "1", "2", "3", "−", "0", ".", "%", "+", "C", "⌫", "=", " ",
        };
        const char* k = keys[i];
        if (!k || !*k || *k == ' ') return;
        if (strcmp(k, "C") == 0) { _expr.clear(); _result = "0"; return; }
        if (strcmp(k, "⌫") == 0) { if (!_expr.empty()) _expr.pop_back(); return; }
        if (strcmp(k, "=") == 0) { _evaluate(); return; }
        if (strcmp(k, "÷") == 0) _expr += "/";
        else if (strcmp(k, "×") == 0) _expr += "*";
        else if (strcmp(k, "−") == 0) _expr += "-";
        else { if (*k) _expr += *k; }
    }

    void _evaluate() {
        if (_expr.empty()) return;
        double v = _parse();
        if (_parse_err) { _result = "Error"; _expr.clear(); return; }
        char buf[24];
        snprintf(buf, sizeof(buf), "%.10g", v);
        _result = buf;
        _hist.push_back(_expr + " = " + _result);
        if (_hist.size() > 10) _hist.erase(_hist.begin());
        _expr.clear();
    }

    /* 递归下降四则解析（无异常版本，错误置 _parse_err）：
     * expr := term ((+|-) term)* ; term := factor ((*|/) factor)* */
    double _parse() {
        _parse_err = false;
        size_t p = 0;
        double v = _expr_term(p);
        _skip(p);
        if (p < _expr.size()) _parse_err = true;
        return v;
    }
    double _expr_term(size_t& p) {
        double v = _expr_factor(p);
        for (;;) {
            _skip(p);
            if (p < _expr.size() && (_expr[p] == '+' || _expr[p] == '-')) {
                char op = _expr[p++];
                double r = _expr_factor(p);
                v = op == '+' ? v + r : v - r;
            } else break;
        }
        return v;
    }
    double _expr_factor(size_t& p) {
        double v = _expr_primary(p);
        for (;;) {
            _skip(p);
            if (p < _expr.size() && (_expr[p] == '*' || _expr[p] == '/')) {
                char op = _expr[p++];
                double r = _expr_primary(p);
                if (op == '*') v = v * r;
                else if (r == 0.0) { _parse_err = true; return 0.0; }
                else v = v / r;
            } else break;
        }
        return v;
    }
    double _expr_primary(size_t& p) {
        _skip(p);
        if (p < _expr.size() && _expr[p] == '(') {
            p++;
            double v = _expr_term(p);
            _skip(p);
            if (p < _expr.size() && _expr[p] == ')') p++;
            else { _parse_err = true; }
            return v;
        }
        if (p < _expr.size() && _expr[p] == '-') { p++; return -_expr_primary(p); }
        double v = _expr_num(p);
        _skip(p);
        while (p < _expr.size() && _expr[p] == '%') { p++; v /= 100.0; _skip(p); }
        return v;
    }
    double _expr_num(size_t& p) {
        size_t st = p;
        while (p < _expr.size() && (isdigit(_expr[p]) || _expr[p] == '.')) p++;
        if (st == p) { _parse_err = true; return 0.0; }
        return atof(_expr.substr(st, p - st).c_str());
    }
    void _skip(size_t& p) const { while (p < _expr.size() && _expr[p] == ' ') p++; }

    std::string _expr;
    std::string _result = "0";
    std::vector<std::string> _hist;
    bool _parse_err = false;
};

static std::shared_ptr<window::Window> calc_create() {
    return std::make_shared<CalcMode>();
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
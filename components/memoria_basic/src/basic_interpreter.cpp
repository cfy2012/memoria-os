/**
 * @file basic_interpreter.cpp
 * @brief Memoria BASIC 解释器实现
 *
 * 移植自项目根目录 BASIC.cpp（MiniBasic + Native Bridge），
 * 变更：
 *   - iostream / system() 移除：输出走回调，SYSTEM 走命令表
 *   - PRINT 增强：支持分号分隔的混合输出（PRINT "A=";X）
 *   - 新增扩展语句 / 扩展函数注册表（屏幕 / WiFi / GPIO 等由宿主注入）
 *   - 步数上限强制终止（死循环保护）
 *   - ':' 多语句同行；IF..THEN..ELSE；条件可为数值（非 0 即真）
 *   - FOR..NEXT / WHILE..WEND 循环；INCLUDE "file.bas" 合并程序行
 *   - GOTO 半禁用：可用，但每轮运行提示一次注意事项
 *   - 修复：GOTO / GOSUB / IF-THEN 数字跳转原先会跳过目标行
 *     （跳转后仍自动前进一行，目标行从未执行）；现跳转后不再前进
 */

#include "basic_interpreter.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <algorithm>

namespace memoria {
namespace basic {

/* ============================================================
 *  工具
 * ============================================================ */
static std::string trim_str(const std::string& s) {
    if (s.empty()) return s;
    size_t st = 0, en = s.size();
    while (st < en && (s[st] == ' ' || s[st] == '\t' || s[st] == '\r')) st++;
    while (en > st && (s[en-1] == ' ' || s[en-1] == '\t' || s[en-1] == '\r')) en--;
    return s.substr(st, en - st);
}

static std::string upper_str(const std::string& s) {
    std::string r = s;
    for (size_t i = 0; i < r.size(); i++) r[i] = (char)std::toupper((unsigned char)r[i]);
    return r;
}

static std::string lower_str(const std::string& s) {
    std::string r = s;
    for (size_t i = 0; i < r.size(); i++) r[i] = (char)std::tolower((unsigned char)r[i]);
    return r;
}

static bool is_numeric_str(const std::string& s) {
    std::string t = trim_str(s);
    if (t.empty()) return false;
    size_t st = 0;
    if (t[0] == '-' || t[0] == '+') st = 1;
    if (st >= t.size()) return false;
    bool dot = false;
    for (size_t i = st; i < t.size(); i++) {
        if (t[i] == '.') { if (dot) return false; dot = true; }
        else if (!std::isdigit((unsigned char)t[i])) return false;
    }
    return true;
}

static bool brackets_balanced(const std::string& e) {
    int cnt = 0;
    for (size_t i = 0; i < e.size(); i++) {
        if (e[i] == '(') cnt++;
        else if (e[i] == ')') cnt--;
        if (cnt < 0) return false;
    }
    return cnt == 0;
}

/* 引号外找独立单词（ELSE / TO / STEP 用）：前后不能是字母或数字 */
static size_t find_word_outside_strings(const std::string& s, const char* kw) {
    std::string us = upper_str(s);
    size_t klen = std::strlen(kw);
    bool in_str = false;
    for (size_t i = 0; i + klen <= us.size(); i++) {
        if (us[i] == '"') in_str = !in_str;
        if (in_str) continue;
        if (us.compare(i, klen, kw) == 0) {
            bool l = (i == 0) || !std::isalnum((unsigned char)us[i - 1]);
            bool r = (i + klen == us.size()) || !std::isalnum((unsigned char)us[i + klen]);
            if (l && r) return i;
        }
    }
    return std::string::npos;
}

/* 按 ':' 切多语句（引号内的 ':' 不切） */
static std::vector<std::string> split_statements(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    bool in_str = false;
    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        if (c == '"') in_str = !in_str;
        if (c == ':' && !in_str) {
            std::string t = trim_str(cur);
            if (!t.empty()) out.push_back(t);
            cur.clear();
        } else {
            cur += c;
        }
    }
    std::string t = trim_str(cur);
    if (!t.empty()) out.push_back(t);
    return out;
}

/* 逗号分隔参数解析：引号内逗号不分割；字符串字面量保留原样，数值走 eval */
std::vector<BasicArg> parse_args(const std::string& args_text,
                                 const std::function<double(const std::string&)>& eval) {
    std::vector<BasicArg> out;
    std::string cur;
    bool in_str = false;
    auto flush = [&]() {
        std::string t = trim_str(cur);
        cur.clear();
        if (t.empty()) return;
        BasicArg a;
        if (t.size() >= 2 && t[0] == '"' && t.back() == '"') {
            a.is_string = true;
            a.str = t.substr(1, t.size() - 2);
        } else if (t.size() >= 2 && t[0] == '"' && t.find('"', 1) != std::string::npos) {
            size_t e2 = t.find('"', 1);
            if (trim_str(t.substr(e2 + 1)).empty()) {
                a.is_string = true;
                a.str = t.substr(1, e2 - 1);
            } else {
                a.num = eval(t);   /* 带引号但后面还有内容：整体求值作为回退 */
            }
        } else {
            a.num = eval(t);
        }
        out.push_back(a);
    };
    for (size_t i = 0; i < args_text.size(); i++) {
        char c = args_text[i];
        if (c == '"') in_str = !in_str;
        if (c == ',' && !in_str) { flush(); }
        else cur += c;
    }
    flush();
    return out;
}

/* ============================================================
 *  数学桥接
 * ============================================================ */
double BasicInterpreter::_math_sin(double a)   { return std::sin(a); }
double BasicInterpreter::_math_cos(double a)   { return std::cos(a); }
double BasicInterpreter::_math_tan(double a)   { return std::tan(a); }
double BasicInterpreter::_math_abs(double a)   { return std::fabs(a); }
double BasicInterpreter::_math_sqr(double a)   { return std::sqrt(a); }
double BasicInterpreter::_math_int(double a)   { return std::floor(a); }
double BasicInterpreter::_math_log(double a)   { return std::log(a); }
double BasicInterpreter::_math_log10(double a) { return std::log10(a); }
double BasicInterpreter::_math_exp(double a)   { return std::exp(a); }

BasicInterpreter::BasicInterpreter() {
    /* BASIC RND 与 .ms 共用 rand，播种一次（全局） */
    static std::once_flag s_basic_rand_seed;
    std::call_once(s_basic_rand_seed, [] { std::srand(static_cast<unsigned>(std::time(nullptr))); });

    /* 内置 RND()：0~1 */
    _funcs["RND"] = [](const std::vector<BasicArg>&) -> double {
        return std::rand() / double(RAND_MAX);
    };
}

/* ============================================================
 *  IO / 扩展注册
 * ============================================================ */
void BasicInterpreter::set_io(BasicOutFn out, BasicInputFn input) {
    _out = std::move(out); _has_out = static_cast<bool>(_out);
    _input = std::move(input); _has_input = static_cast<bool>(_input);
}

void BasicInterpreter::add_cmd(const std::string& name, BasicCmdFn fn) {
    _cmds[upper_str(trim_str(name))] = std::move(fn);
}

void BasicInterpreter::add_func(const std::string& name, BasicFuncFn fn) {
    _funcs[upper_str(trim_str(name))] = std::move(fn);
}

/* ============================================================
 *  程序管理
 * ============================================================ */
void BasicInterpreter::add_line(int line_num, const std::string& code) {
    if (trim_str(code).empty()) _program.erase(line_num);
    else _program[line_num] = code;
}

void BasicInterpreter::clear_program() {
    _program.clear();
    _vars.clear();
    _gosub_stack.clear();
    _for_stack.clear();
    _while_stack.clear();
}

bool BasicInterpreter::load_file(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return false;
    /* 逐字符读行：无 128B 缓冲上限，超长行不再被截断成多段（旧 fgets 会破坏程序） */
    std::string line;
    line.reserve(256);
    auto commit = [&]() -> bool {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string t = trim_str(line);
        if (t.empty()) return true;
        int ln = 0;
        size_t i = 0;
        while (i < t.size() && std::isdigit((unsigned char)t[i])) { ln = ln * 10 + (t[i] - '0'); i++; }
        if (i > 0) _program[ln] = trim_str(t.substr(i));
        return true;
    };
    for (;;) {
        int c = std::fgetc(f);
        if (c == EOF) break;
        if (c == '\n') { commit(); line.clear(); }
        else line += (char)c;
    }
    if (!line.empty()) commit();   /* 文件末尾无换行的最后一行 */
    std::fclose(f);
    return true;
}

bool BasicInterpreter::has_program() const { return !_program.empty(); }
int  BasicInterpreter::line_count() const  { return static_cast<int>(_program.size()); }

void BasicInterpreter::list_program(std::string& out) {
    out.clear();
    if (_program.empty()) { out = "(空程序)\n"; return; }
    char buf[24];
    for (auto& p : _program) {
        std::snprintf(buf, sizeof(buf), "%d ", p.first);
        out += buf;
        out += p.second;
        out += "\n";
    }
}

/* ============================================================
 *  表达式求值（递归下降）
 * ============================================================ */
double BasicInterpreter::evaluate_expression(const std::string& expr) {
    std::string e = trim_str(expr);
    if (e.empty()) return 0;
    if (!brackets_balanced(e)) { if (_has_out) _out("错误：括号不配对"); return 0; }

    if (e[0] == '-') return -evaluate_expression(e.substr(1));          /* 一元负号 */
    if (is_numeric_str(e)) return std::atof(e.c_str());                 /* 数字常量 */

    /* 函数调用 FUNC(...)：先扩展表，后数学表 */
    size_t op = e.find('('), cp = e.rfind(')');
    if (op != std::string::npos && cp != std::string::npos && cp > op) {
        std::string fname = upper_str(trim_str(e.substr(0, op)));
        std::string arg_text = trim_str(e.substr(op + 1, cp - op - 1));
        if (trim_str(e.substr(cp + 1)).empty()) {   /* 必须是纯函数调用 */
            bool ok = false;
            double v = call_func(fname, arg_text, ok);
            if (ok) return v;
        }
    }

    /* 变量查找（小写，大小写不敏感） */
    std::string lower_e = lower_str(e);
    auto vit = _vars.find(lower_e);
    if (vit != _vars.end()) return vit->second;

    /* 加减（从右往左找最外层） */
    for (int i = (int)e.length() - 1; i >= 0; i--) {
        if ((e[i] == '+' || e[i] == '-') && i > 0 && e[i-1] != '(') {
            return evaluate_expression(e.substr(0, i)) +
                   (e[i] == '+' ? evaluate_expression(e.substr(i + 1))
                                : -evaluate_expression(e.substr(i + 1)));
        }
    }
    /* 乘除 */
    for (int i = (int)e.length() - 1; i >= 0; i--) {
        if (e[i] == '*' || e[i] == '/') {
            double l = evaluate_expression(e.substr(0, i));
            double r = evaluate_expression(e.substr(i + 1));
            if (e[i] == '*') return l * r;
            if (std::fabs(r) < 1e-12) { if (_has_out) _out("错误：除以 0"); return 0; }
            return l / r;
        }
    }

    /* 剥外层括号 */
    if (e[0] == '(' && e[e.size() - 1] == ')')
        return evaluate_expression(e.substr(1, e.size() - 2));

    if (_has_out) _out("错误：无法识别表达式 " + e);
    return 0;
}

/* 扩展函数调用：扩展表 → 数学表 → 失败 */
double BasicInterpreter::call_func(const std::string& name, const std::string& arg_text, bool& ok) {
    auto eval_l = [this](const std::string& s) { return evaluate_expression(s); };

    auto fit = _funcs.find(name);
    if (fit != _funcs.end()) {
        ok = true;
        return fit->second(parse_args(arg_text, eval_l));
    }

    struct MathFn { const char* n; double (*f)(double); };
    static const MathFn kMath[] = {
        {"SIN", &_math_sin}, {"COS", &_math_cos}, {"TAN", &_math_tan},
        {"ABS", &_math_abs}, {"SQR", &_math_sqr}, {"INT", &_math_int},
        {"LOG", &_math_log}, {"LOG10", &_math_log10}, {"EXP", &_math_exp},
    };
    for (const auto& m : kMath) {
        if (name == m.n) {
            std::string a = trim_str(arg_text);
            if (name == "SQR") { double v = evaluate_expression(a); if (v < 0) { if (_has_out) _out("错误：SQR 参数为负"); ok = true; return 0; } ok = true; return m.f(v); }
            if (name == "LOG" || name == "LOG10") { double v = evaluate_expression(a); if (v <= 0) { if (_has_out) _out("错误：LOG 参数 <= 0"); ok = true; return 0; } ok = true; return m.f(v); }
            ok = true;
            return m.f(evaluate_expression(a));
        }
    }
    if (name == "POW") {
        auto args = parse_args(arg_text, eval_l);
        if (args.size() >= 2 && !args[0].is_string && !args[1].is_string) {
            ok = true;
            return std::pow(args[0].num, args[1].num);
        }
        if (_has_out) _out("错误：POW 需要两个参数 POW(a,b)");
        ok = true;
        return 0;
    }

    ok = false;
    return 0;
}

/* ============================================================
 *  条件求值
 * ============================================================ */
bool BasicInterpreter::evaluate_condition(const std::string& cond) {
    std::string c = trim_str(cond);
    std::string op;
    size_t pos;
    if ((pos = c.find(">=")) != std::string::npos) op = ">=";
    else if ((pos = c.find("<=")) != std::string::npos) op = "<=";
    else if ((pos = c.find("<>")) != std::string::npos) op = "<>";
    else if ((pos = c.find('>')) != std::string::npos) op = ">";
    else if ((pos = c.find('<')) != std::string::npos) op = "<";
    else if ((pos = c.find('=')) != std::string::npos) op = "=";
    else {
        /* 无数值比较符：按“非 0 即真”，WHILE 1 / IF X THEN 可用 */
        return evaluate_expression(c) != 0;
    }

    double l = evaluate_expression(trim_str(c.substr(0, pos)));
    double r = evaluate_expression(trim_str(c.substr(pos + op.size())));
    if (op == "=")  return l == r;
    if (op == ">")  return l > r;
    if (op == "<")  return l < r;
    if (op == ">=") return l >= r;
    if (op == "<=") return l <= r;
    if (op == "<>") return l != r;
    return false;
}

/* ============================================================
 *  语句
 * ============================================================ */
void BasicInterpreter::do_print(const std::string& args) {
    /* 支持分号 / 逗号混合：PRINT "SUM=";S  →  SUM=12.5 */
    std::string a = args;
    std::string line_out;
    std::string cur;
    auto flush = [&]() {
        std::string t = trim_str(cur);
        cur.clear();
        if (t.empty()) return;
        if (t.size() >= 2 && t[0] == '"' && t.back() == '"') {
            line_out += t.substr(1, t.size() - 2);
        } else if (t.size() >= 2 && t[0] == '"' && t.find('"', 1) != std::string::npos &&
                   trim_str(t.substr(t.find('"', 1) + 1)).empty()) {
            size_t e2 = t.find('"', 1);
            line_out += t.substr(1, e2 - 1);
        } else {
            double v = evaluate_expression(t);
            char buf[32];
            if (v == static_cast<int>(v)) std::snprintf(buf, sizeof(buf), "%d", (int)v);
            else std::snprintf(buf, sizeof(buf), "%.4g", v);
            line_out += buf;
        }
    };
    for (size_t i = 0; i < a.size(); i++) {
        char c = a[i];
        if (c == ';' || c == ',') flush();
        else cur += c;
    }
    flush();
    if (_has_out) _out(line_out);
}

void BasicInterpreter::do_let(const std::string& args) {
    size_t eq = args.find('=');
    if (eq == std::string::npos) { if (_has_out) _out("语法错误 LET"); return; }
    std::string var = lower_str(trim_str(args.substr(0, eq)));
    double val = evaluate_expression(trim_str(args.substr(eq + 1)));
    _vars[var] = val;
}

void BasicInterpreter::do_input(const std::string& args) {
    std::string var = lower_str(trim_str(args));
    double v = 0;
    if (_has_input) v = _input("? ");
    else { if (_has_out) _out("INPUT 不可用"); return; }
    _vars[var] = v;
}

void BasicInterpreter::do_goto(const std::string& args) {
    /* 半禁用：仍可用，但提示一次注意事项 */
    if (!_goto_warned) {
        _goto_warned = true;
        if (_has_out) _out("[注意] GOTO 半禁用：仍可用，但容易绕晕/死循环，能用 FOR/WHILE 就别用 GOTO");
    }
    _current_line = (int)evaluate_expression(args);
    _jumped = true;
}

void BasicInterpreter::do_gosub(const std::string& args) {
    _gosub_stack.push_back(next_line_after(_current_line));   /* 返回地址 = GOSUB 的下一行 */
    _current_line = (int)evaluate_expression(args);
    _jumped = true;
}

void BasicInterpreter::do_return() {
    if (_gosub_stack.empty()) {
        if (_has_out) _out("错误：RETURN 没有 GOSUB");
        _running = false;
        return;
    }
    _current_line = _gosub_stack.back();
    _gosub_stack.pop_back();
    if (_current_line < 0) { _running = false; return; }   /* GOSUB 在最后一行：返回即结束 */
    _jumped = true;
}

void BasicInterpreter::do_if(const std::string& full_line) {
    std::string u = upper_str(full_line);
    size_t then_pos = u.find("THEN");
    if (then_pos == std::string::npos) { if (_has_out) _out("语法错误 IF 缺 THEN"); return; }
    /* 条件 = 跳过开头的 IF 关键字（修复：原来把 "IF" 一起算进条件导致求值错误） */
    std::string cond_str = trim_str(full_line.substr(2, then_pos - 2));
    std::string rest = trim_str(full_line.substr(then_pos + 4));

    /* ELSE 可选：引号外找独立 ELSE */
    size_t else_pos = find_word_outside_strings(rest, "ELSE");
    std::string then_body = trim_str(else_pos == std::string::npos ? rest : rest.substr(0, else_pos));
    std::string else_body = else_pos == std::string::npos ? "" : trim_str(rest.substr(else_pos + 4));

    if (evaluate_condition(cond_str)) exec_if_body(then_body);
    else exec_if_body(else_body);
}

/* THEN/ELSE 体：纯数字 = GOTO，否则按 ':' 多语句执行 */
void BasicInterpreter::exec_if_body(const std::string& body) {
    std::string t = trim_str(body);
    if (t.empty()) return;
    if (is_numeric_str(t)) do_goto(t);
    else exec_statements(t);
}

void BasicInterpreter::do_for(const std::string& args) {
    /* FOR I = 1 TO 10 STEP 2 */
    std::string a = trim_str(args);
    size_t eq = a.find('=');
    if (eq == std::string::npos) {
        if (_has_out) _out("语法错误 FOR（写法：FOR I=1 TO 10 [STEP 2]）");
        return;
    }
    std::string var = lower_str(trim_str(a.substr(0, eq)));
    if (var.empty()) { if (_has_out) _out("语法错误 FOR：缺少变量"); return; }
    std::string rest = trim_str(a.substr(eq + 1));

    size_t to_pos = find_word_outside_strings(rest, "TO");
    if (to_pos == std::string::npos) {
        if (_has_out) _out("语法错误 FOR：缺 TO（写法：FOR I=1 TO 10）");
        return;
    }
    std::string start_expr = trim_str(rest.substr(0, to_pos));
    std::string end_part = trim_str(rest.substr(to_pos + 2));

    double step = 1.0;
    std::string end_expr;
    size_t step_pos = find_word_outside_strings(end_part, "STEP");
    if (step_pos != std::string::npos) {
        end_expr = trim_str(end_part.substr(0, step_pos));
        step = evaluate_expression(trim_str(end_part.substr(step_pos + 4)));
    } else {
        end_expr = end_part;
    }

    double start = evaluate_expression(start_expr);
    double end = evaluate_expression(end_expr);

    /* 循环方向不成立（start>end 且 step>0，或 start<end 且 step<0）
       不执行循环体，直接跳到匹配 NEXT 之后 */
    bool ok_dir = (step >= 0) ? (start <= end) : (start >= end);
    if (!ok_dir) {
        int nx = find_matching_next(next_line_after(_current_line));
        if (nx < 0) { if (_has_out) _out("错误：FOR 缺 NEXT"); return; }
        int nl = next_line_after(nx);
        if (nl < 0) { _running = false; return; }
        _current_line = nl;
        _jumped = true;
        return;
    }

    _vars[var] = start;
    ForState fs;
    fs.var = var;
    fs.end = end;
    fs.step = step;
    fs.body_line = next_line_after(_current_line);
    _for_stack.push_back(fs);
    /* 不跳转：继续执行下一行（循环体） */
}

void BasicInterpreter::do_next(const std::string& args) {
    std::string var = lower_str(trim_str(args));
    if (_for_stack.empty()) { if (_has_out) _out("错误：NEXT 没有匹配的 FOR"); return; }

    int idx = (int)_for_stack.size() - 1;
    if (!var.empty()) {
        int found = -1;
        for (int i = (int)_for_stack.size() - 1; i >= 0; i--) {
            if (_for_stack[i].var == var) { found = i; break; }
        }
        if (found < 0) { if (_has_out) _out("错误：NEXT " + var + " 没有匹配的 FOR"); return; }
        idx = found;
    }

    ForState fs = _for_stack[idx];
    _for_stack.erase(_for_stack.begin() + idx);
    _vars[fs.var] += fs.step;

    bool cont = (fs.step >= 0) ? (_vars[fs.var] <= fs.end) : (_vars[fs.var] >= fs.end);
    if (cont && fs.body_line >= 0) {
        _for_stack.push_back(fs);
        _current_line = fs.body_line;
        _jumped = true;
    }
    /* 不 cont（或无循环体）：继续执行 NEXT 之后的行 */
}

void BasicInterpreter::do_while(const std::string& args) {
    std::string cond = trim_str(args);
    if (evaluate_condition(cond)) {
        _while_stack.push_back({_current_line});
        return;   /* 继续执行下一行（循环体） */
    }
    /* 条件为假：跳到匹配的 WEND 之后 */
    int wend = find_matching_wend(_current_line);
    if (wend < 0) {
        if (_has_out) _out("错误：WHILE 缺 WEND");
        _running = false;
        return;
    }
    int nl = next_line_after(wend);
    if (nl < 0) { _running = false; return; }
    _current_line = nl;
    _jumped = true;
}

void BasicInterpreter::do_wend() {
    if (_while_stack.empty()) {
        if (_has_out) _out("错误：WEND 没有 WHILE");
        return;
    }
    int wl = _while_stack.back().while_line;
    _while_stack.pop_back();
    _current_line = wl;
    _jumped = true;
}

void BasicInterpreter::do_system(const std::string& args) {
    /* ESP32 无 shell：SYSTEM 走扩展命令表（由宿主注入 REBOOT 等内部命令） */
    auto it = _cmds.find("SYSTEM");
    if (it != _cmds.end()) {
        auto eval_l = [this](const std::string& s) { return evaluate_expression(s); };
        it->second(parse_args(args, eval_l));
        return;
    }
    if (_has_out) _out("SYSTEM: 无内部命令");
}

void BasicInterpreter::do_end() { _running = false; }

/* ============================================================
 *  执行一条语句（不处理 ':'，由 exec_statements 负责切分）
 * ============================================================ */
void BasicInterpreter::exec_statement(const std::string& stmt) {
    std::string t = trim_str(stmt);
    if (t.empty()) return;
    std::string ul = upper_str(t);

    /* 关键字边界。
       防前缀误判（PRINTX 不是 PRINT）；放行无空格紧凑写法。
       分档：
       - alpha_ok=true（GOTO/GOSUB）：非字母即分隔符 → GOTO100 数字可跟
       - 默认 !isalnum：PRINT"HI" 引号可跟；PRINTX/LETX 字母拒
       - IF/FOR/NEXT/WHILE/WEND：保留字无条件收（IFX>2 / FORI=1 / NEXTI / WHILEI<3 紧凑写法） */
    auto kw_ok = [&ul](size_t n, const char* k, bool alpha_ok = false) {
        if (!(ul.size() >= n && ul.compare(0, n, k) == 0)) return false;
        if (ul.size() == n) return true;
        unsigned char c = (unsigned char)ul[n];
        if (alpha_ok) return !std::isalpha(c);
        return !std::isalnum(c);
    };
    if (kw_ok(5, "PRINT"))  { do_print(t.substr(5)); return; }
    if (kw_ok(3, "LET"))    { do_let(t.substr(3)); return; }
    if (kw_ok(5, "INPUT"))  { do_input(t.substr(5)); return; }
    if (kw_ok(4, "GOTO", true))   { do_goto(t.substr(4)); return; }
    if (kw_ok(5, "GOSUB", true))  { do_gosub(t.substr(5)); return; }
    if (kw_ok(6, "RETURN")) { do_return(); return; }
    if (kw_ok(6, "SYSTEM")) { do_system(t.substr(6)); return; }
    if (kw_ok(3, "END"))    { do_end(); return; }
    if (ul.size() >= 3 && ul.compare(0, 3, "FOR") == 0)   { do_for(t.substr(3)); return; }
    if (ul.size() >= 4 && ul.compare(0, 4, "NEXT") == 0)  { do_next(t.substr(4)); return; }
    if (ul.size() >= 5 && ul.compare(0, 5, "WHILE") == 0) { do_while(t.substr(5)); return; }
    if (ul.size() >= 4 && ul.compare(0, 4, "WEND") == 0)  { do_wend(); return; }
    if (ul.size() >= 2 && ul.compare(0, 2, "IF") == 0)    { do_if(t); return; }
    if (kw_ok(7, "INCLUDE")) {
        std::string p = trim_str(t.substr(7));
        if (p.size() >= 2 && p.front() == '"' && p.back() == '"') p = p.substr(1, p.size() - 2);
        if (!load_file(p)) { if (_has_out) _out("INCLUDE 打开失败: " + p); }
        return;
    }

    /* 扩展语句 */
    if (dispatch_cmd(t)) return;

    if (_has_out) _out("未知命令: " + t);
}

/* 多语句执行：IF 独占整行（THEN 体内的 ':' 归 IF 管），其余按 ':' 切分 */
void BasicInterpreter::exec_statements(const std::string& text) {
    std::string t = trim_str(text);
    if (t.empty()) return;
    std::string ul = upper_str(t);
    /* IF 无条件认（条件常以变量开头，IFX=1THEN 紧凑写法）；变量不会以 IF 命名 */
    if (ul.size() >= 2 && ul.compare(0, 2, "IF") == 0) {
        do_if(t);
        return;
    }
    auto stmts = split_statements(t);
    for (size_t i = 0; i < stmts.size(); i++) {
        exec_statement(stmts[i]);
        if (_jumped) break;   /* GOTO/GOSUB 等跳转后，同行剩余语句不再执行 */
    }
}

/* 一行程序的入口 */
void BasicInterpreter::execute_line(const std::string& line) {
    exec_statements(line);
}

bool BasicInterpreter::dispatch_cmd(const std::string& line) {
    std::string t = trim_str(line);
    size_t sp = t.find_first_of(" \t");
    std::string head = upper_str(sp == std::string::npos ? t : t.substr(0, sp));
    auto it = _cmds.find(head);
    if (it == _cmds.end()) return false;

    std::string rest = (sp == std::string::npos) ? "" : t.substr(sp);
    auto eval_l = [this](const std::string& s) { return evaluate_expression(s); };
    it->second(parse_args(rest, eval_l));
    return true;
}

/* ============================================================
 *  运行循环（带死循环保护 + 跳转支持）
 * ============================================================ */
int BasicInterpreter::run(uint32_t max_steps) {
    if (_program.empty()) { if (_has_out) _out("没有程序"); return 2; }
    _vars.clear();
    _gosub_stack.clear();
    _for_stack.clear();
    _while_stack.clear();
    _goto_warned = false;
    _steps = 0;
    _running = true;
    _current_line = _program.begin()->first;

    while (_running) {
        if (++_steps > max_steps) {
            if (_has_out) _out("[保护] 运行超过 " + std::to_string(max_steps) + " 步，已强制终止（检查死循环）");
            _running = false;
            return 1;
        }
        auto it = _program.find(_current_line);
        if (it == _program.end()) {
            /* 目标行不存在（GOTO/GOSUB 悬空）：报错停止，不再静默就近跳转 */
            if (_has_out) _out("错误：行号 " + std::to_string(_current_line) + " 不存在");
            _running = false;
            continue;
        }
        _jumped = false;
        execute_line(it->second);
        if (_running && !_jumped) go_to_next_line();
    }
    return 0;
}

void BasicInterpreter::go_to_next_line() {
    auto it = _program.upper_bound(_current_line);
    if (it == _program.end()) _running = false;
    else _current_line = it->first;
}

int BasicInterpreter::next_line_after(int line) const {
    auto it = _program.upper_bound(line);
    return (it == _program.end()) ? -1 : it->first;
}

int BasicInterpreter::find_matching_wend(int from_line) const {
    int depth = 0;
    for (auto it = _program.upper_bound(from_line); it != _program.end(); ++it) {
        std::string t = trim_str(it->second);
        std::string ul = upper_str(t);
        /* 行首关键字无条件认（FORI=1 / NEXTI / WHILEI<3 紧凑写法也计深度） */
        if (ul.size() >= 5 && ul.compare(0, 5, "WHILE") == 0) { depth++; continue; }
        if (ul.size() >= 4 && ul.compare(0, 4, "WEND") == 0) {
            if (depth == 0) return it->first;
            depth--;
        }
    }
    return -1;
}

/* 找匹配的 NEXT（FOR 嵌套深度为 0 时） */
int BasicInterpreter::find_matching_next(int from_line) const {
    int depth = 0;
    for (auto it = _program.upper_bound(from_line); it != _program.end(); ++it) {
        std::string t = trim_str(it->second);
        std::string ul = upper_str(t);
        if (ul.size() >= 3 && ul.compare(0, 3, "FOR") == 0) { depth++; continue; }
        if (ul.size() >= 4 && ul.compare(0, 4, "NEXT") == 0) {
            if (depth == 0) return it->first;
            depth--;
        }
    }
    return -1;
}

} // namespace basic
} // namespace memoria
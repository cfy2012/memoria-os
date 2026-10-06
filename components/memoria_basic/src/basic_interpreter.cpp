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
#include "esp_random.h"

#if __has_include(<esp_heap_caps.h>)
#include <esp_heap_caps.h>
#endif

namespace memoria {
namespace basic {

/* 大缓冲（文件流等 1MB 级）：有 PSRAM 用 PSRAM，否则退普通堆 */
static void* big_malloc(size_t n) {
#if __has_include(<esp_heap_caps.h>)
    void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) return p;
#endif
    return std::malloc(n);
}

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

/* ============================================================
 *  UTF-8 工具（中文内嵌字符串，BASIC-LANGUAGE.md v1.4 §4.4）
 *  纯 ASCII 时字符=字节，行为与旧版完全一致
 * ============================================================ */
static size_t utf8_charlen(const std::string& s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size(); i++)
        if (((uint8_t)s[i] & 0xC0) != 0x80) n++;   /* 只数首字节 */
    return n;
}

/* 按字符边界切取 [start, start+count)，绝不切断汉字 */
static std::string utf8_char_slice(const std::string& s, size_t start, size_t count) {
    std::string out;
    size_t ci = 0;
    for (size_t i = 0; i < s.size() && ci < start + count;) {
        const uint8_t b = (uint8_t)s[i];
        size_t l = 1;
        if      (b >= 0xF0) l = 4;
        else if (b >= 0xE0) l = 3;
        else if (b >= 0xC0) l = 2;
        if (ci >= start) out.append(s, i, l);
        i += l;
        ci++;
    }
    return out;
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

/* 逗号分隔参数解析：引号内逗号不分割；字符串字面量保留原样，数值走 eval
 *  str_eval 非空：串变量（$ 结尾）/ 含引号串表达式 → is_string 参数 */
std::vector<BasicArg> parse_args(const std::string& args_text,
                                 const std::function<double(const std::string&)>& eval,
                                 const std::function<std::string(const std::string&)>& str_eval) {
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
            /* 串变量（$ 结尾）/ 含引号串表达式：走字符串求值 */
            if (str_eval && !t.empty() && (t.back() == '$' || t.find('"') != std::string::npos)) {
                a.is_string = true;
                a.str = str_eval(t);
            } else {
                a.num = eval(t);
            }
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

/* 数值 → 文本（PRINT 与 STR$ 共用：整数不带小数点，其余 %.4g） */
static std::string num_to_string(double v) {
    char buf[32];
    if (v == static_cast<double>(static_cast<int>(v))) std::snprintf(buf, sizeof(buf), "%d", (int)v);
    else std::snprintf(buf, sizeof(buf), "%.4g", v);
    return std::string(buf);
}

/* 串表达式判定：字面量 / $ 结尾变量 / $ 结尾函数（STR$ LEFT$ 等） */
static bool is_string_expr(const std::string& t) {
    std::string s = trim_str(t);
    if (s.empty()) return false;
    if (s[0] == '"') return true;
    if (s.back() == '$') return true;
    size_t op = s.find('(');
    if (op != std::string::npos && op > 0 && s[op - 1] == '$') return true;
    return false;
}

/* 引号外找比较符（两字符 >= <= <> 优先）；找不到返回 false */
static bool find_cmp_op(const std::string& c, size_t& pos, std::string& op) {
    bool in_str = false;
    for (size_t i = 0; i < c.size(); i++) {
        char ch = c[i];
        if (ch == '"') { in_str = !in_str; continue; }
        if (in_str) continue;
        if (ch == '>' || ch == '<' || ch == '=') {
            if (i + 1 < c.size() && c[i + 1] == '=' && ch != '=') { op = std::string(1, ch) + "="; pos = i; return true; }
            if (ch == '<' && i + 1 < c.size() && c[i + 1] == '>') { op = "<>"; pos = i; return true; }
            op = std::string(1, ch); pos = i; return true;
        }
    }
    return false;
}

/* 引号外按逗号切参数（$ 函数参数拆分用） */
static std::vector<std::string> split_top_commas(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    bool in_str = false;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '"') in_str = !in_str;
        if (c == ',' && !in_str) { out.push_back(trim_str(cur)); cur.clear(); }
        else cur += c;
    }
    std::string t = trim_str(cur);
    if (!t.empty() || !out.empty()) out.push_back(t);
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
    /* BASIC RND 内核：ESP32 硬件真随机（esp_random，芯片热噪声物理熵源）。
       无种子、每次开机序列不同、不可预测。范围逻辑：无参=0~1；RND(n)=1..n；RND(lo,hi)=lo..hi */

    /* 内置 RND()：无参=0~1；RND(n)=1..n 整数；RND(lo,hi)=lo..hi 整数 */
    _funcs["RND"] = [](const std::vector<BasicArg>& a) -> double {
        if (a.empty() || a[0].is_string) return esp_random() / 4294967296.0;
        if (a.size() >= 2 && !a[1].is_string) {
            double lo = a[0].num < a[1].num ? a[0].num : a[1].num;
            double hi = a[0].num < a[1].num ? a[1].num : a[0].num;
            if (hi - lo < 1.0) return lo;
            return lo + std::floor(esp_random() / 4294967296.0 * (hi - lo + 1.0));
        }
        double n1 = a[0].num;
        if (n1 >= 1.0) return 1.0 + std::floor(esp_random() / 4294967296.0 * n1);
        return esp_random() / 4294967296.0;
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

void BasicInterpreter::add_raw_cmd(const std::string& name, BasicRawCmdFn fn) {
    _raw_cmds[upper_str(trim_str(name))] = std::move(fn);
}

void BasicInterpreter::set_input_str(BasicInputStrFn fn) {
    _input_str = std::move(fn);
    _has_input_str = static_cast<bool>(_input_str);
}

/* ---- 宿主回调辅助 ---- */
double BasicInterpreter::eval_num_expr(const std::string& expr) {
    return evaluate_expression(expr);
}

std::string BasicInterpreter::eval_str_expr(const std::string& expr) {
    return evaluate_str_expr(expr);
}

bool BasicInterpreter::set_svar(const std::string& name, const std::string& value) {
    std::string k = lower_str(trim_str(name));
    if (k.size() < 2 || k.back() != '$') return false;
    for (size_t i = 0; i + 1 < k.size(); i++) {
        char c = k[i];
        if (!(std::isalnum((unsigned char)c) || c == '_')) return false;
    }
    _svars[k] = value;
    return true;
}

/* 数值变量预注入：变量名小写归一，禁 $ 结尾；供 v1.3 环境变量（ime_cn 等） */
bool BasicInterpreter::set_nvar(const std::string& name, double value) {
    std::string k = lower_str(trim_str(name));
    if (k.empty() || k.back() == '$') return false;
    for (char c : k) {
        if (!(std::isalnum((unsigned char)c) || c == '_')) return false;
    }
    _vars[k] = value;
    return true;
}

std::string BasicInterpreter::get_svar(const std::string& name) {
    auto it = _svars.find(lower_str(trim_str(name)));
    return it == _svars.end() ? std::string() : it->second;
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
    _svars.clear();
    _arrays.clear();
    _fns.clear();
    _data_queue.clear();
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

    /* NOT：一元前缀（行首独立词） */
    if (find_word_outside_strings(e, "NOT") == 0)
        return (evaluate_expression(trim_str(e.substr(3))) == 0) ? 1.0 : 0.0;

    /* OR：短路（字运算符，引号外） */
    {
        size_t w = find_word_outside_strings(e, "OR");
        if (w != std::string::npos) {
            if (evaluate_expression(trim_str(e.substr(0, w))) != 0) return 1.0;
            return (evaluate_expression(trim_str(e.substr(w + 2))) != 0) ? 1.0 : 0.0;
        }
    }
    /* AND：短路 */
    {
        size_t w = find_word_outside_strings(e, "AND");
        if (w != std::string::npos) {
            if (evaluate_expression(trim_str(e.substr(0, w))) == 0) return 0.0;
            return (evaluate_expression(trim_str(e.substr(w + 3))) != 0) ? 1.0 : 0.0;
        }
    }
    /* MOD：与乘除同级（fmod，符号随被除数） */
    {
        size_t w = find_word_outside_strings(e, "MOD");
        if (w != std::string::npos) {
            double l = evaluate_expression(trim_str(e.substr(0, w)));
            double r = evaluate_expression(trim_str(e.substr(w + 3)));
            if (std::fabs(r) < 1e-12) { if (_has_out) _out("错误：MOD 除以 0"); return 0; }
            return std::fmod(l, r);
        }
    }

    if (e[0] == '-') return -evaluate_expression(e.substr(1));          /* 一元负号 */
    if (is_numeric_str(e)) return std::atof(e.c_str());                 /* 数字常量 */

    /* 函数调用 FUNC(...)：扩展表 → 数学表 → DEF FN → DIM 数组元素 */
    size_t op = e.find('('), cp = e.rfind(')');
    if (op != std::string::npos && cp != std::string::npos && cp > op) {
        std::string fname = upper_str(trim_str(e.substr(0, op)));
        std::string arg_text = trim_str(e.substr(op + 1, cp - op - 1));
        if (trim_str(e.substr(cp + 1)).empty()) {   /* 必须是纯函数调用 */
            bool ok = false;
            double v = call_func(fname, arg_text, ok);
            if (!ok) v = call_fn(lower_str(fname), arg_text, ok);   /* DEF FN */
            if (!ok) {
                std::string an = lower_str(fname);
                if (!an.empty() && an.back() != '$') {
                    if (array_get(an, arg_text, v)) ok = true;
                } else if (!an.empty() && _arrays.count(an)) {
                    /* 数值上下文读字符串数组：类型不匹配 */
                    if (_has_out) _out("错误：" + an + " 是字符串数组，不能当数值用");
                    _running = false;
                    ok = true;
                    v = 0;
                }
            }
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

/* ============================================================
 *  字符串表达式求值：字面量 / 串变量 / + 拼接 / STR$ CHR$ LEFT$ RIGHT$ MID$
 * ============================================================ */
std::string BasicInterpreter::evaluate_str_expr(const std::string& expr) {
    std::string e = trim_str(expr);
    if (e.empty()) return "";

    /* 纯字面量（单对引号） */
    if (e.size() >= 2 && e[0] == '"' && e.back() == '"' && e.find('"', 1) == e.size() - 1)
        return e.substr(1, e.size() - 2);

    /* $ 结尾函数调用 */
    size_t op = e.find('('), cp = e.rfind(')');
    if (op != std::string::npos && cp != std::string::npos && cp > op &&
        trim_str(e.substr(cp + 1)).empty()) {
        std::string fname = upper_str(trim_str(e.substr(0, op)));
        std::string arg = trim_str(e.substr(op + 1, cp - op - 1));
        if (fname == "STR$") return num_to_string(evaluate_expression(arg));
        if (fname == "CHR$") {
            int c = (int)evaluate_expression(arg);
            if (c < 0 || c > 255) { if (_has_out) _out("错误：CHR$ 参数 0~255"); return ""; }
            return std::string(1, (char)c);
        }
        if (fname == "LEFT$" || fname == "RIGHT$" || fname == "MID$") {
            std::vector<std::string> ps = split_top_commas(arg);
            if (ps.empty()) { if (_has_out) _out("错误：" + fname + " 缺参数"); return ""; }
            std::string s = evaluate_str_expr(ps[0]);
            long n1 = ps.size() > 1 ? (long)evaluate_expression(ps[1]) : 0;
            if (fname == "LEFT$") {
                if (n1 < 0) n1 = 0;
                size_t clen = utf8_charlen(s);
                if (n1 > (long)clen) n1 = (long)clen;
                return utf8_char_slice(s, 0, (size_t)n1);
            }
            if (fname == "RIGHT$") {
                if (n1 <= 0) return "";
                size_t clen = utf8_charlen(s);
                if (n1 > (long)clen) n1 = (long)clen;
                return utf8_char_slice(s, clen - (size_t)n1, (size_t)n1);
            }
            /* MID$(s, start, len)：start 从 1 计，按字符边界 */
            long len = ps.size() > 2 ? (long)evaluate_expression(ps[2]) : (long)s.size();
            size_t clen = utf8_charlen(s);
            if (n1 < 1 || n1 > (long)clen) return "";
            if (len < 0) len = 0;
            if (len > (long)(clen - (size_t)(n1 - 1))) len = (long)(clen - (size_t)(n1 - 1));
            return utf8_char_slice(s, (size_t)(n1 - 1), (size_t)len);
        }
        /* 字符串数组元素 NAME$(i[,j])（内置 $ 函数优先） */
        std::string fan = lower_str(fname);
        auto ait = _arrays.find(fan);
        if (ait != _arrays.end()) {
            if (!ait->second.is_str) {
                if (_has_out) _out("错误：" + fan + " 是数值数组，不能当字符串用");
                _running = false;
                return "";
            }
            std::string sv;
            array_get_str(fan, arg, sv);
            return sv;
        }
    }

    /* 串变量（未定义读出空串） */
    if (e.back() == '$') {
        auto it = _svars.find(lower_str(e));
        return it == _svars.end() ? std::string() : it->second;
    }

    /* + 拼接（引号外，从右往左） */
    bool in_str = false;
    for (int i = (int)e.size() - 1; i >= 0; i--) {
        char c = e[i];
        if (c == '"') in_str = !in_str;
        else if (c == '+' && !in_str && i > 0)
            return evaluate_str_expr(e.substr(0, i)) + evaluate_str_expr(e.substr(i + 1));
    }

    if (_has_out) _out("错误：无法识别字符串表达式 " + e + "（数字转文本用 STR$(x)）");
    return "";
}

/* 扩展函数调用：扩展表 → 数学表 → 失败 */
double BasicInterpreter::call_func(const std::string& name, const std::string& arg_text, bool& ok) {
    auto eval_l = [this](const std::string& s) { return evaluate_expression(s); };
    auto str_l  = [this](const std::string& s) { return evaluate_str_expr(s); };

    auto fit = _funcs.find(name);
    if (fit != _funcs.end()) {
        ok = true;
        return fit->second(parse_args(arg_text, eval_l, str_l));
    }

    /* 内置串函数（返回数值）：LEN(s$) / VAL(s$) */
    if (name == "LEN" || name == "VAL") {
        auto args = parse_args(arg_text, eval_l, str_l);
        ok = true;
        if (args.empty() || !args[0].is_string) return 0;
        if (name == "LEN") return (double)utf8_charlen(args[0].str);
        return is_numeric_str(args[0].str) ? std::atof(args[0].str.c_str()) : 0.0;
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
        auto args = parse_args(arg_text, eval_l, str_l);
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
    size_t pos;
    std::string op;

    /* AND / OR / NOT：字逻辑运算符（引号外），短路求值；OR 优先级最低先切 */
    size_t orp = find_word_outside_strings(c, "OR");
    if (orp != std::string::npos) {
        if (evaluate_condition(trim_str(c.substr(0, orp)))) return true;
        return evaluate_condition(trim_str(c.substr(orp + 2)));
    }
    size_t andp = find_word_outside_strings(c, "AND");
    if (andp != std::string::npos) {
        if (!evaluate_condition(trim_str(c.substr(0, andp)))) return false;
        return evaluate_condition(trim_str(c.substr(andp + 3)));
    }
    if (find_word_outside_strings(c, "NOT") == 0)
        return !evaluate_condition(trim_str(c.substr(3)));

    if (!find_cmp_op(c, pos, op)) {
        /* 无数值比较符：按“非 0 即真”，WHILE 1 / IF X THEN 可用 */
        return evaluate_expression(c) != 0;
    }
    std::string lt = trim_str(c.substr(0, pos));
    std::string rt = trim_str(c.substr(pos + op.size()));

    /* 字符串比较：任一侧为串表达式（"字面量"/串变量/STR$ 等） */
    if (is_string_expr(lt) || is_string_expr(rt)) {
        std::string l = evaluate_str_expr(lt);
        std::string r = evaluate_str_expr(rt);
        if (op == "=")  return l == r;
        if (op == "<>") return l != r;
        if (op == ">")  return l > r;
        if (op == "<")  return l < r;
        if (op == ">=") return l >= r;
        if (op == "<=") return l <= r;
        return false;
    }

    double l = evaluate_expression(lt);
    double r = evaluate_expression(rt);
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
        } else if (is_string_expr(t)) {
            /* 串变量 / 串表达式（A$、"a"+B$、STR$(X)） */
            line_out += evaluate_str_expr(t);
        } else {
            line_out += num_to_string(evaluate_expression(t));
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
    /* 数组元素赋值 A(i) = 数值表达式 / A$(i) = 串表达式（均支持二维 A(i,j)） */
    if (!var.empty() && var.back() == ')') {
        size_t pop = var.find('(');
        if (pop != std::string::npos) {
            std::string an = trim_str(var.substr(0, pop));
            std::string ix = trim_str(var.substr(pop + 1, var.size() - pop - 2));
            if (!an.empty() && an.back() == '$')
                array_set_str(an, ix, evaluate_str_expr(trim_str(args.substr(eq + 1))));
            else
                array_set(an, ix, evaluate_expression(trim_str(args.substr(eq + 1))));
            return;
        }
    }
    /* 字符串变量：LET A$ = 串表达式 */
    if (!var.empty() && var.back() == '$') {
        _svars[var] = evaluate_str_expr(trim_str(args.substr(eq + 1)));
        return;
    }
    double val = evaluate_expression(trim_str(args.substr(eq + 1)));
    _vars[var] = val;
}

void BasicInterpreter::do_input(const std::string& args) {
    std::string var = lower_str(trim_str(args));
    /* INPUT A$：字符串录入（宿主提供键盘回调） */
    if (!var.empty() && var.back() == '$') {
        if (!_has_input_str) { if (_has_out) _out("错误：宿主未提供字符串输入"); return; }
        _svars[var] = _input_str("? ");
        return;
    }
    double v = 0;
    if (_has_input) v = _input("? ");
    else { if (_has_out) _out("INPUT 不可用"); return; }
    _vars[var] = v;
}

void BasicInterpreter::do_goto(const std::string& args) {
    if (_fn_depth > 0) { if (_has_out) _out("错误：函数体内禁用 GOTO（用 FNRET 返回）"); _running = false; return; }
    /* 半禁用：仍可用，但提示一次注意事项 */
    if (!_goto_warned) {
        _goto_warned = true;
        if (_has_out) _out("[注意] GOTO 半禁用：仍可用，但容易绕晕/死循环，能用 FOR/WHILE 就别用 GOTO");
    }
    _current_line = (int)evaluate_expression(args);
    _jumped = true;
}

void BasicInterpreter::do_gosub(const std::string& args) {
    if (_fn_depth > 0) { if (_has_out) _out("错误：函数体内禁用 GOSUB"); _running = false; return; }
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

    /* 块 IF 头：THEN 后无体（体从下一行起，至 ELSE/ELSE IF/END IF） */
    if (then_body.empty()) { run_block_if(cond_str, _current_line); return; }

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

/* ---------- READ / DATA ---------- */
void BasicInterpreter::do_read(const std::string& args) {
    auto names = split_top_commas(args);
    for (auto& nq : names) {
        std::string var = lower_str(trim_str(nq));
        if (var.empty()) continue;
        if (_data_queue.empty()) { if (_has_out) _out("错误：READ 缺 DATA"); _running = false; return; }
        std::string raw = _data_queue.front();
        _data_queue.erase(_data_queue.begin());
        if (var.back() == '$') {
            if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"')
                raw = raw.substr(1, raw.size() - 2);   /* 带引号去引号 */
            _svars[var] = raw;
        } else {
            _vars[var] = is_numeric_str(raw) ? std::atof(raw.c_str()) : 0.0;
        }
    }
}

/* ---------- DIM 数组：一/二维，数值/字符串（A$）；AS 类型声明记录实例；下标 0..n ---------- */
void BasicInterpreter::do_dim(const std::string& args) {
    for (auto& decl : split_top_commas(args)) {
        std::string d = trim_str(decl);
        size_t op = d.find('(');
        /* AS 类型：DIM p AS Point / DIM ps(10) AS Point（实例字段以扁平 key 落入变量表） */
        std::string du = upper_str(d);
        size_t asp = du.find(" AS ");
        if (asp != std::string::npos) {
            std::string dpart = trim_str(d.substr(0, asp));
            std::string tname = lower_str(trim_str(d.substr(asp + 4)));
            auto tit = _types.find(tname);
            if (tit == _types.end()) { if (_has_out) _out("错误：TYPE 类型未定义 " + tname); _running = false; return; }
            long d1 = 1, d2 = 0;
            bool is_arr = false;
            std::string iname;
            if (dpart.find('(') == std::string::npos) {
                iname = lower_str(dpart);
            } else {
                is_arr = true;
                size_t op2 = dpart.find('(');
                if (dpart.empty() || dpart.back() != ')') {
                    if (_has_out) _out("语法错误 DIM（实例数组：DIM ps(10) AS " + tname + "）");
                    _running = false;
                    return;
                }
                iname = lower_str(trim_str(dpart.substr(0, op2)));
                std::vector<std::string> dims = split_top_commas(trim_str(dpart.substr(op2 + 1, dpart.size() - op2 - 2)));
                if (dims.empty() || dims.size() > 2) { if (_has_out) _out("语法错误 DIM：实例数组维度 1~2 个"); _running = false; return; }
                d1 = (long)evaluate_expression(trim_str(dims[0]));
                d2 = dims.size() == 2 ? (long)evaluate_expression(trim_str(dims[1])) : 0;
            }
            bool id = !iname.empty() && (std::isalpha((unsigned char)iname[0]) || iname[0] == '_');
            for (size_t i = 1; id && i < iname.size(); i++)
                if (!std::isalnum((unsigned char)iname[i]) && iname[i] != '_') id = false;
            if (!id) { if (_has_out) _out("语法错误 DIM：实例名不合法"); _running = false; return; }
            if (d1 < 0 || d1 > 10000 || d2 < 0 || d2 > 10000 ||
                d1 * (d2 > 0 ? d2 : 1) * (long)tit->second.size() > 100000) {
                if (_has_out) _out("错误：DIM 实例规模不合法（总字段数 ≤100000）");
                _running = false;
                return;
            }
            long n2 = d2 > 0 ? d2 : 1;
            for (long i = 0; i < (is_arr ? d1 : 1); i++) {
                for (long j = 0; j < n2; j++) {
                    std::string inst = iname;
                    if (is_arr) {
                        inst += "(" + std::to_string(i);
                        if (d2 > 0) inst += "," + std::to_string(j);
                        inst += ")";
                    }
                    for (auto& f : tit->second) {
                        std::string key = inst + "." + f;
                        if (!f.empty() && f.back() == '$') _svars[key] = std::string();
                        else                               _vars[key] = 0.0;
                    }
                }
            }
            continue;
        }
        if (op == std::string::npos || d.empty() || d.back() != ')') {
            if (_has_out) _out("语法错误 DIM（写法：DIM A(10) / DIM A(3,4) / DIM A$(10)）");
            _running = false;
            return;
        }
        std::string name = lower_str(trim_str(d.substr(0, op)));
        bool id = !name.empty() &&
                  (std::isalpha((unsigned char)name[0]) || name[0] == '_');
        for (size_t i = 1; id && i < name.size(); i++)
            if (!std::isalnum((unsigned char)name[i]) && name[i] != '_') id = false;
        if (!id) { if (_has_out) _out("语法错误 DIM：数组名不合法"); _running = false; return; }
        if (_arrays.count(name)) { if (_has_out) _out("错误：数组 " + name + " 重复声明"); _running = false; return; }
        bool is_str = name.back() == '$';
        /* 维度：括号内 1~2 个表达式（顶层逗号切分） */
        std::vector<std::string> dims = split_top_commas(trim_str(d.substr(op + 1, d.size() - op - 2)));
        if (dims.empty() || dims.size() > 2) {
            if (_has_out) _out("语法错误 DIM：维度 1~2 个（一维 / 二维）");
            _running = false;
            return;
        }
        long d1 = (long)evaluate_expression(trim_str(dims[0]));
        long d2 = dims.size() == 2 ? (long)evaluate_expression(trim_str(dims[1])) : 0;
        if (d1 < 0 || d1 > 10000 || d2 < 0 || d2 > 10000 ||
            d1 * (d2 > 0 ? d2 : 1) > 100000) {
            if (_has_out) _out("错误：DIM 上界不合法（每维 ≤10000，总元素 ≤100000）");
            _running = false;
            return;
        }
        BasicArray a;
        a.is_str = is_str;
        a.d1 = (int)d1;
        a.d2 = (int)d2;
        size_t n = (size_t)d1 * (size_t)(d2 > 0 ? d2 : 1);
        if (is_str) a.str.assign(n, std::string());
        else        a.num.assign(n, 0.0);
        _arrays[name] = std::move(a);
    }
}

/* 下标解析 + 范围校验：true=ok；false=已报错停机（个数不符/越界/语法错） */
bool BasicInterpreter::array_idx(const BasicArray& a, const std::string& idx_expr, size_t& out) {
    std::vector<std::string> ps = split_top_commas(idx_expr);
    if (ps.empty() || ps.size() > 2 || (ps.size() == 1 && a.d2 > 0)) {
        if (_has_out) _out(a.d2 > 0 ? "错误：二维数组下标须为 i,j" : "错误：数组下标须为 i");
        _running = false;
        return false;
    }
    long i = (long)evaluate_expression(trim_str(ps[0]));
    long j = 0;
    if (ps.size() == 2) j = (long)evaluate_expression(trim_str(ps[1]));
    if (i < 0 || i >= a.d1 || (a.d2 > 0 && (j < 0 || j >= a.d2))) {
        if (_has_out) _out("错误：数组下标越界");
        _running = false;
        return false;
    }
    out = a.flat(i, j);
    return true;
}

bool BasicInterpreter::array_get(const std::string& name, const std::string& idx_expr, double& out) {
    out = 0;
    auto it = _arrays.find(lower_str(trim_str(name)));
    if (it == _arrays.end()) {
        if (_has_out) _out("错误：数组 " + name + " 未 DIM 声明");
        _running = false;
        return true;   /* 错误已报：表达式按 0 处理并停机 */
    }
    if (it->second.is_str) { if (_has_out) _out("错误：" + name + " 是字符串数组，不能当数值用"); _running = false; return true; }
    size_t k;
    if (!array_idx(it->second, idx_expr, k)) return true;
    out = it->second.num[k];
    return true;
}

bool BasicInterpreter::array_get_str(const std::string& name, const std::string& idx_expr, std::string& out) {
    out.clear();
    auto it = _arrays.find(lower_str(trim_str(name)));
    if (it == _arrays.end()) {
        if (_has_out) _out("错误：数组 " + name + " 未 DIM 声明");
        _running = false;
        return true;
    }
    if (!it->second.is_str) { if (_has_out) _out("错误：" + name + " 是数值数组，不能当字符串用"); _running = false; return true; }
    size_t k;
    if (!array_idx(it->second, idx_expr, k)) return true;
    out = it->second.str[k];
    return true;
}

bool BasicInterpreter::array_set(const std::string& name, const std::string& idx_expr, double v) {
    auto it = _arrays.find(lower_str(trim_str(name)));
    if (it == _arrays.end()) {
        if (_has_out) _out("错误：数组 " + name + " 未 DIM 声明");
        _running = false;
        return false;
    }
    if (it->second.is_str) { if (_has_out) _out("错误：" + name + " 是字符串数组，不能存数值"); _running = false; return false; }
    size_t k;
    if (!array_idx(it->second, idx_expr, k)) return false;
    it->second.num[k] = v;
    return true;
}

bool BasicInterpreter::array_set_str(const std::string& name, const std::string& idx_expr, const std::string& v) {
    auto it = _arrays.find(lower_str(trim_str(name)));
    if (it == _arrays.end()) {
        if (_has_out) _out("错误：数组 " + name + " 未 DIM 声明");
        _running = false;
        return false;
    }
    if (!it->second.is_str) { if (_has_out) _out("错误：" + name + " 是数值数组，不能存字符串"); _running = false; return false; }
    size_t k;
    if (!array_idx(it->second, idx_expr, k)) return false;
    it->second.str[k] = v;
    return true;
}

/* ---------- DEF FN：单行（= 表达式）或多行（DEF 行..END DEF），参数按值遮蔽 ---------- */
void BasicInterpreter::do_def(const std::string& args) {
    if (_collecting_fn) { if (_has_out) _out("错误：函数体内不可再 DEF（不支持嵌套定义）"); _running = false; return; }
    std::string a = trim_str(args);
    if (a.empty()) { if (_has_out) _out("语法错误 DEF FN"); return; }
    size_t op = a.find('(');
    if (op == std::string::npos) { if (_has_out) _out("语法错误 DEF（写法：DEF FN名(参数) = 表达式 或 DEF FN名(参数) 换行..END DEF）"); return; }
    std::string name = lower_str(trim_str(a.substr(0, op)));
    size_t cp = a.rfind(')');
    if (name.empty() || cp == std::string::npos || cp < op) {
        if (_has_out) _out("语法错误 DEF：函数名/括号不合法");
        return;
    }
    std::vector<std::string> ps;
    for (auto& p : split_top_commas(a.substr(op + 1, cp - op - 1))) {
        std::string pn = lower_str(trim_str(p));
        if (!pn.empty()) ps.push_back(pn);
    }
    std::string body = trim_str(a.substr(cp + 1));
    if (!body.empty() && body[0] == '=') body = trim_str(body.substr(1));
    if (body.empty()) {
        /* 多行模式：后续行收进函数体，直到 END DEF（run 循环拦截） */
        _collecting_fn = true;
        _collect_name = name;
        _collect_params = ps;
        _collect_body.clear();
        return;
    }
    _fns[name] = {ps, body, {}, false};
}

/* ---------- TYPE：记录类型定义启动，体行收字段名（$ 尾=串字段），直到 END TYPE ---------- */
void BasicInterpreter::do_type(const std::string& args) {
    if (_collecting_type) { if (_has_out) _out("错误：TYPE 体内不可嵌套 TYPE"); _running = false; return; }
    std::string tn = lower_str(trim_str(args));
    bool id = !tn.empty() && (std::isalpha((unsigned char)tn[0]) || tn[0] == '_');
    for (size_t i = 1; id && i < tn.size(); i++)
        if (!std::isalnum((unsigned char)tn[i]) && tn[i] != '_') id = false;
    if (!id) { if (_has_out) _out("语法错误 TYPE（写法：TYPE 名 换行 字段..END TYPE）"); return; }
    if (_types.count(tn)) { if (_has_out) _out("错误：类型 " + tn + " 重复定义"); return; }
    _collecting_type = true;
    _type_name = tn;
    _type_fields.clear();
}

double BasicInterpreter::call_fn(const std::string& name, const std::string& arg_text, bool& ok) {
    ok = false;
    auto it = _fns.find(name);
    if (it == _fns.end()) return 0;
    ok = true;
    const FnDef& f = it->second;   /* 函数体执行期间 _fns 不变（体内 DEF 已禁） */
    auto eval_l = [this](const std::string& s) { return evaluate_expression(s); };
    auto str_l  = [this](const std::string& s) { return evaluate_str_expr(s); };
    auto ps = parse_args(arg_text, eval_l, str_l);
    const auto& pnames = f.params;
    if (ps.size() != pnames.size()) {
        if (_has_out) _out("错误：FN " + name + " 参数个数不符");
        return 0;
    }
    /* 参数遮蔽：保存旧值 → 写入 → 求值 → 恢复（栈式，递归安全；未定义过的擦除） */
    std::vector<std::pair<std::string, bool>> had;
    std::vector<double> oldv;
    for (size_t i = 0; i < ps.size(); i++) {
        if (ps[i].is_string) { if (_has_out) _out("错误：FN 参数须为数值"); return 0; }
        auto vit = _vars.find(pnames[i]);
        had.push_back({pnames[i], vit != _vars.end()});
        if (vit != _vars.end()) oldv.push_back(vit->second);
        _vars[pnames[i]] = ps[i].num;
    }
    double v = 0;
    if (f.multi) {
        if (++_fn_depth > 32) {
            if (_has_out) _out("错误：函数嵌套过深（递归无出口？上限 32）");
            _running = false;
            --_fn_depth;
            for (size_t i = 0; i < had.size(); i++) {
                if (had[i].second) _vars[had[i].first] = oldv[i];
                else               _vars.erase(had[i].first);
            }
            return 0;
        }
        v = exec_fn_body(f);
        --_fn_depth;
    } else {
        v = evaluate_expression(f.expr);
    }
    for (size_t i = 0; i < had.size(); i++) {
        if (had[i].second) _vars[had[i].first] = oldv[i];
        else               _vars.erase(had[i].first);
    }
    return v;
}

/* 多行函数体执行：FNRET（任意深度）返回；体尾隐式返回 0；GOTO/GOSUB 已在 do_goto/do_gosub 禁用 */
double BasicInterpreter::exec_fn_body(const FnDef& f) {
    double ret = 0;
    size_t fs0 = _for_stack.size(), ws0 = _while_stack.size();
    bool ret_ok = false;
    for (const auto& line : f.body) {
        if (_abort || !_running) break;   /* 宿主强停 / 内部报错停机 */
        if (++_steps > _max_steps) {
            if (_has_out) _out("[保护] 运行超过 " + std::to_string(_max_steps) + " 步，已强制终止（检查死循环）");
            _running = false;
            break;
        }
        if (_step_hook && (_steps & 0xFFF) == 0) _step_hook();
        std::string t = trim_str(line);
        if (t.empty()) continue;
        exec_statements(t);
        if (_fn_returning) { ret_ok = true; break; }   /* FNRET（含 IF THEN 内） */
    }
    /* 消费返回标志并取值（嵌套调用时内层消费自己的，不影响外层） */
    if (ret_ok) ret = _fn_ret_val;
    _fn_returning = false;
    /* 中途 FNRET 跳出：丢弃体内未闭合的 FOR/WHILE 帧，避免污染主程序循环栈 */
    if (_for_stack.size() > fs0)   _for_stack.resize(fs0);
    if (_while_stack.size() > ws0) _while_stack.resize(ws0);
    return ret;
}

/* ---------- 文件流：FREAD / FWRITE / FAPPEND（esp_vfs，读入上限 1MB，PSRAM） ---------- */
static const size_t kFreadMax = 1u * 1024u * 1024u;

void BasicInterpreter::do_fread(const std::string& args) {
    auto ps = split_top_commas(args);
    if (ps.size() != 2) { if (_has_out) _out("语法错误 FREAD（写法：FREAD 路径, T$）"); return; }
    std::string path = evaluate_str_expr(ps[0]);
    std::string var = lower_str(trim_str(ps[1]));
    if (var.empty() || var.back() != '$') { if (_has_out) _out("错误：FREAD 目标须为串变量"); return; }
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { if (_has_out) _out("fread: 打不开 " + path); return; }
    char* buf = (char*)big_malloc(kFreadMax + 1);
    if (!buf) { std::fclose(f); if (_has_out) _out("fread: 内存不足"); return; }
    size_t n = std::fread(buf, 1, kFreadMax, f);
    std::fclose(f);
    buf[n] = 0;
    _svars[var] = std::string(buf, n);
    std::free(buf);
}

void BasicInterpreter::do_fwrite(const std::string& args, bool append) {
    auto ps = split_top_commas(args);
    if (ps.size() != 2) { if (_has_out) _out("语法错误（写法：FWRITE 路径, 内容串）"); return; }
    std::string path = evaluate_str_expr(ps[0]);
    std::string val  = evaluate_str_expr(ps[1]);
    FILE* f = std::fopen(path.c_str(), append ? "ab" : "wb");
    if (!f) { if (_has_out) _out(std::string(append ? "fappend: 打不开 " : "fwrite: 打不开 ") + path); return; }
    std::fwrite(val.data(), 1, val.size(), f);
    std::fclose(f);
}

/* ---------- 块 IF：IF..THEN(换行)..ELSE / ELSE IF / END IF ---------- */
void BasicInterpreter::run_block_if(const std::string& cond, int if_line) {
    int depth = 0;
    int branch_line = -1;   /* 深度 0 的第一个 ELSE / ELSE IF 行 */
    int endif_line = -1;
    for (auto it = _program.upper_bound(if_line); it != _program.end(); ++it) {
        std::string t = trim_str(it->second);
        std::string ul = upper_str(t);
        /* 块 IF 头：标准 IF 开头且 THEN 后无体（或无 THEN）→ 深度 +1；单行 IF 不计 */
        if (ul.compare(0, 2, "IF") == 0 && (ul.size() == 2 || !std::isalnum((unsigned char)ul[2]))) {
            size_t tp = find_word_outside_strings(t, "THEN");
            if (tp == std::string::npos || trim_str(t.substr(tp + 4)).empty()) { depth++; continue; }
            continue;
        }
        if (ul.compare(0, 5, "ENDIF") == 0 ||
            (ul.compare(0, 3, "END") == 0 && trim_str(t.substr(3)) == "IF")) {
            if (depth == 0) { endif_line = it->first; break; }
            depth--;
            continue;
        }
        if (depth == 0 && ul.compare(0, 4, "ELSE") == 0 &&
            (ul.size() == 4 || ul[4] == ' ' || ul.compare(0, 6, "ELSEIF") == 0)) {
            if (branch_line < 0) branch_line = it->first;
        }
    }
    if (endif_line < 0) { if (_has_out) _out("错误：块 IF 缺 END IF"); _running = false; return; }

    if (evaluate_condition(cond)) {
        exec_block_range(if_line, branch_line > 0 ? branch_line : endif_line, endif_line);
        return;
    }
    if (branch_line < 0) { _current_line = endif_line; _jumped = true; return; }
    std::string et = trim_str(_program[branch_line]);
    std::string eul = upper_str(et);
    if (eul.compare(0, 6, "ELSEIF") == 0 || eul.compare(0, 7, "ELSE IF") == 0) {
        std::string sub = trim_str(et.substr(eul.compare(0, 6, "ELSEIF") == 0 ? 6 : 7));
        size_t tp = find_word_outside_strings(sub, "THEN");
        if (tp != std::string::npos) sub = trim_str(sub.substr(0, tp));
        run_block_if(sub, branch_line);   /* ELSE IF 递归判定（共用同一 END IF） */
        return;
    }
    exec_block_range(branch_line, endif_line, endif_line);   /* ELSE 体 */
}

/* 执行 (from, to) 行区间；正常走完停 endif（空语句后前进），GOTO 出块则交主循环 */
void BasicInterpreter::exec_block_range(int from, int to, int endif_line) {
    _current_line = next_line_after(from);
    if (_current_line < 0) { _running = false; return; }
    for (;;) {
        if (!_running) return;
        if (_current_line >= to) {
            if (_jumped) return;                              /* GOTO 出块：主循环执行目标行 */
            _current_line = endif_line; _jumped = true;       /* 顺序走完：停 END IF */
            return;
        }
        if (_current_line <= from) { _jumped = true; return; } /* 跳回块前：交主循环重判 */
        auto it = _program.find(_current_line);
        if (it == _program.end()) {
            if (_has_out) _out("错误：行号 " + std::to_string(_current_line) + " 不存在");
            _running = false;
            return;
        }
        _jumped = false;
        execute_line(it->second);
        if (_jumped) continue;    /* 块内导航（循环回跳/内层块收尾/GOTO 块内）→ 边界检查分流 */
        go_to_next_line();
    }
}

void BasicInterpreter::do_system(const std::string& args) {
    /* ESP32 无 shell：SYSTEM 走扩展命令表（由宿主注入 REBOOT 等内部命令） */
    auto it = _cmds.find("SYSTEM");
    if (it != _cmds.end()) {
        auto eval_l = [this](const std::string& s) { return evaluate_expression(s); };
        auto str_l  = [this](const std::string& s) { return evaluate_str_expr(s); };
        it->second(parse_args(args, eval_l, str_l));
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
    if (kw_ok(3, "REM"))   { return; }                       /* 注释兜底（行首已由 exec_statements 拦截） */
    if (kw_ok(4, "DATA"))  { return; }                       /* 数据行：run 时已收集，执行跳过 */
    if (kw_ok(4, "READ"))  { do_read(t.substr(4)); return; }
    if (kw_ok(3, "DIM"))   { do_dim(t.substr(3)); return; }
    if (kw_ok(3, "DEF"))   { do_def(t.substr(3)); return; }
    if (kw_ok(4, "TYPE"))  { do_type(t.substr(4)); return; }
    if (kw_ok(5, "FNRET")) {
        /* FNRET [expr]：函数体内任意深度返回（IF THEN 内也走这里） */
        if (_fn_depth <= 0) { if (_has_out) _out("错误：FNRET 只能在函数体内使用"); return; }
        std::string rest = trim_str(t.substr(5));
        _fn_ret_val = rest.empty() ? 0.0 : evaluate_expression(rest);
        _fn_returning = true;
        return;
    }
    if (kw_ok(5, "FREAD"))   { do_fread(t.substr(5)); return; }
    if (kw_ok(6, "FWRITE"))  { do_fwrite(t.substr(6), false); return; }
    if (kw_ok(7, "FAPPEND")) { do_fwrite(t.substr(7), true); return; }
    /* 块 IF 结构行（空语句）：END IF / ENDIF / ELSE / ELSE IF；END DEF / END TYPE 同为收集兜底 */
    if (ul.compare(0, 3, "END") == 0 && trim_str(t.substr(3)) == "IF") return;
    if (kw_ok(5, "ENDIF")) { return; }
    if (kw_ok(4, "ELSE"))  { return; }
    if (kw_ok(3, "END"))   {
        std::string tail = trim_str(t.substr(3));
        if (tail == "DEF" || tail == "TYPE") return;   /* 收集模式外泄漏的结束行：空语句 */
        do_end(); return;
    }
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

    /* 裸赋值（LET 可省）：VAR = 表达式；VAR 名 = 字母/下划线开头，可 $ 结尾；数组元素 A(i) = 表达式；实例字段 p.x / ps(0).name$ */
    {
        size_t eq = t.find('=');
        if (eq != std::string::npos) {
            std::string lhs = trim_str(t.substr(0, eq));
            if (!lhs.empty() && lhs.back() == ')') {   /* 数组元素目标 A(i) */
                size_t pop = lhs.find('(');
                if (pop != std::string::npos) { do_let(t); return; }
            }
            bool id = !lhs.empty() && (std::isalpha((unsigned char)lhs[0]) || lhs[0] == '_');
            for (size_t i = 1; id && i < lhs.size(); i++) {
                char c = lhs[i];
                if (!(std::isalnum((unsigned char)c) || c == '_' || c == '.' ||
                      c == '(' || c == ')' ||
                      (c == '$' && i == lhs.size() - 1))) id = false;
            }
            if (id) { do_let(t); return; }
        }
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
        std::string us = upper_str(stmts[i]);
        if (us.compare(0, 3, "REM") == 0 &&
            (us.size() == 3 || !std::isalnum((unsigned char)us[3]))) break;   /* REM 至行尾：其后不再执行 */
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
    auto eval_l = [this](const std::string& s) { return evaluate_expression(s); };
    auto str_l  = [this](const std::string& s) { return evaluate_str_expr(s); };
    std::string rest = (sp == std::string::npos) ? "" : t.substr(sp);

    auto it = _cmds.find(head);
    if (it != _cmds.end()) {
        it->second(parse_args(rest, eval_l, str_l));
        return true;
    }
    /* 原样参数语句（HTTPGET 等）：参数原文交宿主解析 */
    auto rit = _raw_cmds.find(head);
    if (rit != _raw_cmds.end()) {
        rit->second(rest);
        return true;
    }
    return false;
}

/* ============================================================
 *  运行循环（带死循环保护 + 跳转支持）
 * ============================================================ */
int BasicInterpreter::run(uint32_t max_steps) {
    if (_program.empty()) { if (_has_out) _out("没有程序"); return 2; }
    _vars.clear();
    _svars.clear();
    _gosub_stack.clear();
    _for_stack.clear();
    _while_stack.clear();
    _arrays.clear();
    _fns.clear();
    _types.clear();
    _collecting_fn = false;
    _collecting_type = false;
    _fn_depth = 0;
    _max_steps = max_steps;
    _data_queue.clear();
    _goto_warned = false;
    _abort = false;
    _steps = 0;
    _running = true;
    _current_line = _program.begin()->first;

    /* 收集 DATA 项（行号序） */
    for (auto& p : _program) {
        std::string t = trim_str(p.second);
        std::string ul = upper_str(t);
        if (ul.compare(0, 4, "DATA") == 0 && (ul.size() == 4 || !std::isalnum((unsigned char)ul[4])))
            for (auto& v : split_top_commas(trim_str(t.substr(4))))
                _data_queue.push_back(trim_str(v));
    }

    while (_running) {
        if (_abort) { _running = false; return 3; }   /* 宿主强停（APP 返回键） */
        if (++_steps > max_steps) {
            if (_has_out) _out("[保护] 运行超过 " + std::to_string(max_steps) + " 步，已强制终止（检查死循环）");
            _running = false;
            return 1;
        }
        if (_step_hook && (_steps & 0xFFF) == 0) _step_hook();   /* 每 4096 步让出 CPU */
        auto it = _program.find(_current_line);
        if (it == _program.end()) {
            /* 目标行不存在（GOTO/GOSUB 悬空）：报错停止，不再静默就近跳转 */
            if (_has_out) _out("错误：行号 " + std::to_string(_current_line) + " 不存在");
            _running = false;
            continue;
        }
        /* 多行 DEF FN 函数体收集：不执行，收行直到 END DEF */
        if (_collecting_fn) {
            std::string t = trim_str(it->second);
            std::string flat;
            for (char c : t) if (c != ' ' && c != '\t') flat += (char)std::toupper((unsigned char)c);
            if (flat == "ENDDEF") {
                _fns[_collect_name] = {_collect_params, "", _collect_body, true};
                _collecting_fn = false;
            } else if (!t.empty()) {
                _collect_body.push_back(t);
            }
            go_to_next_line();
            continue;
        }
        /* TYPE 体收集：每行一个字段名，直到 END TYPE */
        if (_collecting_type) {
            std::string t = trim_str(it->second);
            std::string flat;
            for (char c : t) if (c != ' ' && c != '\t') flat += (char)std::toupper((unsigned char)c);
            if (flat == "ENDTYPE") {
                if (!_type_fields.empty()) _types[_type_name] = _type_fields;
                _collecting_type = false;
            } else if (!t.empty()) {
                /* 字段名：字母/下划线开头 + 字母数字下划线，$ 可作末位 */
                bool id = (std::isalpha((unsigned char)t[0]) || t[0] == '_');
                for (size_t i = 1; id && i < t.size(); i++) {
                    char c = t[i];
                    if (!(std::isalnum((unsigned char)c) || c == '_' ||
                          (c == '$' && i == t.size() - 1))) id = false;
                }
                if (!id) { if (_has_out) _out("语法错误 TYPE：字段名不合法 " + t); _running = false; continue; }
                _type_fields.push_back(lower_str(t));
            }
            go_to_next_line();
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
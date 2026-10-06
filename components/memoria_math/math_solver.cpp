/**
 * @file math_solver.cpp
 * @brief 解方程求解器实现：词法解析 + 系数提取 + 判别求解（SOLVER-SPEC）
 *
 * 范围（初中数学，够用为止）：
 *  - 一元一次：bx + c = 0
 *  - 一元二次：ax²+bx+c = 0，判别式分支（无实根/单根/双根）
 *  - 二元一次方程组：克拉默法则，Δ=0 报无唯一解
 *  - 三元一次方程组：高斯消元（部分主元）
 * 解析特性：缺项补 0、系数省略（x、-x）、隐式乘（2x、x2）、幂次 x^2、
 * 连乘 x*x、系数除法 x/2；忽略空格；括号与多元高次项明确报不支持。
 */

#include "math_solver.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <utility>
#include <vector>

namespace memoria {
namespace math {

namespace {

/* ---------- 数据结构 ---------- */

/* 单个方程（已移项：左边 - 右边 = 0） */
struct Poly {
    double k = 0.0;          /* 常数项 */
    double c[3][3] = {{0}};  /* c[u][d]：未知数 u(x=0,y=1,z=2) 的 d(1,2) 次系数 */
};

/* ---------- 小工具 ---------- */

int var_idx(char ch) { return ch == 'x' ? 0 : (ch == 'y' ? 1 : 2); }

const char* var_name(int u) {
    static const char* n[3] = {"x", "y", "z"};
    return n[u];
}

std::string fmt_num(double v) {
    if (std::fabs(v) < 1e-12) v = 0.0;   /* 消 -0 与尾噪 */
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.10g", v);
    return buf;
}

/* ---------- 单侧式子解析：系数累积进 poly ---------- */

bool parse_side(const std::string& s, Poly& out, std::string& err) {
    size_t p = 0;
    while (p < s.size()) {
        /* 项间 +/- 决定符号 */
        double sign = 1.0;
        if (s[p] == '+') { sign = 1.0; p++; }
        else if (s[p] == '-') { sign = -1.0; p++; }

        /* 一项：因子链（*、/ 连接，隐式乘允许） */
        double num = 1.0;
        int    deg[3] = {0, 0, 0};   /* 各未知数在本项中的次数 */
        int    vars_in_term = 0;
        bool   saw_factor = false;

        for (;;) {
            if (p >= s.size()) break;
            char ch = s[p];
            if (ch == '+' || ch == '-') break;          /* 下一项开始 */
            if (ch == '*') {
                if (!saw_factor) { err = "乘号位置不对"; return false; }
                p++;
                continue;
            }
            if (ch == '/') {
                /* 仅支持系数除法：/ 后必须是数字 */
                p++;
                if (p >= s.size() || !isdigit((unsigned char)s[p])) {
                    err = "除号后只支持数字（不支持多项式除法）";
                    return false;
                }
                size_t st = p;
                while (p < s.size() && (isdigit((unsigned char)s[p]) || s[p] == '.')) p++;
                double d = std::atof(s.substr(st, p - st).c_str());
                if (d == 0.0) { err = "除数为 0"; return false; }
                num /= d;
                saw_factor = true;
                continue;
            }
            if (ch == '(' || ch == ')') { err = "括号暂不支持"; return false; }
            if (isdigit((unsigned char)ch) || ch == '.') {
                size_t st = p;
                while (p < s.size() && (isdigit((unsigned char)s[p]) || s[p] == '.')) p++;
                num *= std::atof(s.substr(st, p - st).c_str());   /* 隐式乘：2x、x2 */
                saw_factor = true;
                continue;
            }
            if (ch == 'x' || ch == 'y' || ch == 'z') {
                int u = var_idx(ch);
                p++;
                int d = 1;
                if (p < s.size() && s[p] == '^') {
                    p++;
                    if (p >= s.size() || !isdigit((unsigned char)s[p])) {
                        err = "^ 后要写次数（如 x^2）";
                        return false;
                    }
                    d = s[p] - '0';
                    p++;
                    if (d > 2 || (p < s.size() && isdigit((unsigned char)s[p]))) {
                        err = "暂不支持三次及以上";
                        return false;
                    }
                }
                if (deg[u] != 0) {
                    err = "同一项里未知数重复（幂次用 ^ 写）";
                    return false;
                }
                deg[u] = d;
                vars_in_term++;
                saw_factor = true;
                continue;
            }
            err = "看不懂的字符";
            return false;
        }

        /* 项归位 */
        if (vars_in_term >= 2) { err = "暂不支持多元高次项（如 xy）"; return false; }
        if (vars_in_term == 1) {
            int u = deg[0] ? 0 : (deg[1] ? 1 : 2);
            out.c[u][deg[u]] += sign * num;
        } else {
            if (!saw_factor) { err = "式子格式不对"; return false; }
            out.k += sign * num;
        }
    }
    return true;
}

/* ---------- 方程组装：等号切分，移项到左 ---------- */

bool build_equation(const std::string& eq, Poly& out, std::string& err) {
    size_t eqpos = eq.find('=');
    if (eqpos == std::string::npos) { err = "式子里没有等号"; return false; }
    if (eq.find('=', eqpos + 1) != std::string::npos) { err = "等号只能有一个"; return false; }
    Poly lhs, rhs;
    if (!parse_side(eq.substr(0, eqpos), lhs, err)) return false;
    if (!parse_side(eq.substr(eqpos + 1), rhs, err)) return false;
    out.k = lhs.k - rhs.k;
    for (int u = 0; u < 3; u++)
        for (int d = 1; d <= 2; d++)
            out.c[u][d] = lhs.c[u][d] - rhs.c[u][d];
    return true;
}

/* ---------- 判别辅助 ---------- */

int used_mask(const std::vector<Poly>& eqs) {
    int mask = 0;
    for (const auto& e : eqs)
        for (int u = 0; u < 3; u++)
            if (std::fabs(e.c[u][1]) > 1e-12 || std::fabs(e.c[u][2]) > 1e-12)
                mask |= (1 << u);
    return mask;
}

bool any_quadratic(const std::vector<Poly>& eqs) {
    for (const auto& e : eqs)
        for (int u = 0; u < 3; u++)
            if (std::fabs(e.c[u][2]) > 1e-12) return true;
    return false;
}

/* ---------- 判别与求解 ---------- */

SolveResult solve_one(const std::vector<Poly>& eqs) {
    SolveResult r;
    int  mask = used_mask(eqs);
    int  n = __builtin_popcount(mask);
    size_t m = eqs.size();

    int vars[3], nv = 0;
    for (int u = 0; u < 3; u++) if (mask & (1 << u)) vars[nv++] = u;

    if (any_quadratic(eqs) && n != 1) {
        r.detail = "方程组暂不支持二次项（二次只限一元）";
        return r;
    }

    if (n == 0) {                       /* 纯常数式：0=0 / 0=5 */
        for (const auto& e : eqs) {
            if (std::fabs(e.k) > 1e-12) {
                r.kind = "矛盾式";
                r.detail = "无解（等式两边不相等）";
                return r;
            }
        }
        r.ok = true;
        r.kind = "恒等式";
        r.detail = "无数解（两边恒等）";
        return r;
    }

    if (n == 1) {
        if (m != 1) { r.detail = "一个未知数只需要一个方程"; return r; }
        int u = vars[0];
        double a2 = eqs[0].c[u][2], a1 = eqs[0].c[u][1], a0 = eqs[0].k;
        if (std::fabs(a2) > 1e-12) {    /* 一元二次 */
            double dlt = a1 * a1 - 4.0 * a2 * a0;
            r.ok = true;
            if (dlt < -1e-12) {
                r.kind = "一元二次·无实根";
                r.detail = "判别式 < 0";
                return r;
            }
            if (std::fabs(dlt) <= 1e-12) {
                r.kind = "一元二次·单根";
                r.detail = std::string(var_name(u)) + " = " + fmt_num(-a1 / (2.0 * a2));
            } else {
                r.kind = "一元二次·两个实根";
                double sq = std::sqrt(dlt);
                r.detail = std::string(var_name(u)) + "1 = " + fmt_num((-a1 + sq) / (2.0 * a2)) +
                           ", " + var_name(u) + "2 = " + fmt_num((-a1 - sq) / (2.0 * a2));
            }
            return r;
        }
        if (std::fabs(a1) <= 1e-12) {   /* 未知数被消掉 */
            if (std::fabs(a0) <= 1e-12) {
                r.ok = true;
                r.kind = "恒等式";
                r.detail = "无数解（两边恒等）";
            } else {
                r.kind = "矛盾式";
                r.detail = "无解（未知数被消掉）";
            }
            return r;
        }
        r.ok = true;                    /* 一元一次 */
        r.kind = "一元一次";
        r.detail = std::string(var_name(u)) + " = " + fmt_num(-a0 / a1);
        return r;
    }

    if (n == 2) {
        if (m < 2) { r.detail = "两个未知数需要两个方程（逗号隔开）"; return r; }
        if (m > 2) { r.detail = "两个未知数给两个方程就够了"; return r; }
        int u = vars[0], v = vars[1];
        double A1 = eqs[0].c[u][1], B1 = eqs[0].c[v][1], C1 = -eqs[0].k;
        double A2 = eqs[1].c[u][1], B2 = eqs[1].c[v][1], C2 = -eqs[1].k;
        double dlt = A1 * B2 - A2 * B1;
        r.ok = true;
        r.kind = "二元一次方程组";
        if (std::fabs(dlt) <= 1e-12) {
            r.detail = "无唯一解（两方程平行或重合）";
            return r;
        }
        r.detail = std::string(var_name(u)) + " = " + fmt_num((C1 * B2 - C2 * B1) / dlt) +
                   ", " + var_name(v) + " = " + fmt_num((A1 * C2 - A2 * C1) / dlt);
        return r;
    }

    /* n == 3：三元一次，高斯消元（部分主元） */
    if (m < 3) { r.detail = "三个未知数需要三个方程（逗号隔开）"; return r; }
    if (m > 3) { r.detail = "三个未知数给三个方程就够了"; return r; }
    double M[3][4];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) M[i][j] = eqs[i].c[vars[j]][1];
        M[i][3] = -eqs[i].k;
    }
    for (int col = 0; col < 3; col++) {
        int piv = col;
        for (int i = col + 1; i < 3; i++)
            if (std::fabs(M[i][col]) > std::fabs(M[piv][col])) piv = i;
        if (std::fabs(M[piv][col]) <= 1e-12) {
            r.kind = "三元一次方程组";
            r.detail = "无唯一解（系数矩阵奇异）";
            return r;
        }
        if (piv != col)
            for (int j = 0; j < 4; j++) std::swap(M[col][j], M[piv][j]);
        for (int i = 0; i < 3; i++) {
            if (i == col) continue;
            double f = M[i][col] / M[col][col];
            for (int j = col; j < 4; j++) M[i][j] -= f * M[col][j];
        }
    }
    r.ok = true;
    r.kind = "三元一次方程组";
    for (int j = 0; j < 3; j++) {
        if (j) r.detail += ", ";
        r.detail += std::string(var_name(vars[j])) + " = " + fmt_num(M[j][3] / M[j][j]);
    }
    return r;
}

} // namespace

std::string SolveResult::text() const {
    if (!ok) return "无法求解: " + detail;
    return kind + ": " + detail;
}

SolveResult solve_equations(const std::string& input) {
    SolveResult r;
    /* 归一化：去空白、统一小写 */
    std::string s;
    s.reserve(input.size());
    for (char ch : input) {
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
        s += (char)std::tolower((unsigned char)ch);
    }
    if (s.empty()) { r.detail = "空式子"; return r; }

    /* 逗号拆方程 */
    std::vector<Poly> eqs;
    std::string err;
    size_t st = 0;
    for (size_t i = 0; i <= s.size(); i++) {
        if (i == s.size() || s[i] == ',') {
            std::string part = s.substr(st, i - st);
            if (part.empty()) { r.detail = "有空的方程（逗号多写了）"; return r; }
            Poly e;
            if (!build_equation(part, e, err)) { r.detail = err; return r; }
            eqs.push_back(e);
            st = i + 1;
        }
    }
    return solve_one(eqs);
}

} // namespace math
} // namespace memoria

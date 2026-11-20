/**
 * @file math_solver.cpp
 * @brief 解方程：递归下降解析 → 多元多项式 / 有理式 → 一元求根、线性方程组
 *
 * 支持范围：
 *  - 任意括号嵌套、隐式乘（2x、x(x+1)、3(x-1)^2）
 *  - 分式方程：先通分成 P(x)/Q(x)=0，解 P=0，再剔除使 Q=0 的增根
 *  - 一元一次到八次：一/二次闭式，三次以上用导数切分区间 + 二分求实根
 *  - 二元/三元一次方程组：括号展开后仍按克拉默、高斯消元解
 *  - 分母含未知数的方程组、多元二次以上：明确报不支持
 */

#include "math_solver.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace memoria {
namespace math {

namespace {

constexpr int MAX_DEG = 8;    /* 单变量最高次数 */
constexpr int MAX_SUM = 12;   /* 单项总次数上限 */
constexpr int MAX_TERMS = 256;

/* ============================================================
 *  稀疏多元多项式：每项 = 系数 * x^ex * y^ey * z^ez
 * ============================================================ */
struct Term { int ex, ey, ez; double c; };

struct MP {
    std::vector<Term> t;

    bool zero() const { return t.empty(); }

    void norm() {
        std::vector<Term> o;
        o.reserve(t.size());
        for (const auto& a : t) {
            bool hit = false;
            for (auto& b : o) {
                if (b.ex == a.ex && b.ey == a.ey && b.ez == a.ez) {
                    b.c += a.c;
                    hit = true;
                    break;
                }
            }
            if (!hit) o.push_back(a);
        }
        t.clear();
        for (const auto& a : o)
            if (std::fabs(a.c) > 1e-12) t.push_back(a);
    }
};

MP mp_c(double c) {
    MP m;
    if (std::fabs(c) > 1e-12) m.t.push_back({0, 0, 0, c});
    return m;
}

MP mp_var(int u) {
    MP m;
    int e[3] = {0, 0, 0};
    e[u] = 1;
    m.t.push_back({e[0], e[1], e[2], 1.0});
    return m;
}

MP mp_add(const MP& a, const MP& b, bool sub, bool& over) {
    MP r = a;
    for (const auto& x : b.t)
        r.t.push_back({x.ex, x.ey, x.ez, sub ? -x.c : x.c});
    if (r.t.size() > MAX_TERMS) { over = true; return MP{}; }
    r.norm();
    return r;
}

MP mp_scale(const MP& a, double k) {
    MP r;
    r.t.reserve(a.t.size());
    for (const auto& x : a.t)
        if (std::fabs(x.c * k) > 1e-12) r.t.push_back({x.ex, x.ey, x.ez, x.c * k});
    return r;
}

MP mp_mul(const MP& a, const MP& b, bool& over) {
    MP r;
    if (a.t.empty() || b.t.empty()) return r;
    if (a.t.size() * b.t.size() > MAX_TERMS) { over = true; return r; }
    r.t.reserve(a.t.size() * b.t.size());
    for (const auto& x : a.t) {
        for (const auto& y : b.t) {
            const int ex = x.ex + y.ex, ey = x.ey + y.ey, ez = x.ez + y.ez;
            if (ex > MAX_DEG || ey > MAX_DEG || ez > MAX_DEG || ex + ey + ez > MAX_SUM) {
                over = true;
                return MP{};
            }
            r.t.push_back({ex, ey, ez, x.c * y.c});
        }
    }
    r.norm();
    return r;
}

MP mp_pow(const MP& a, int e, bool& over) {
    MP r = mp_c(1.0);
    for (int i = 0; i < e; i++) {
        r = mp_mul(r, a, over);
        if (over) return MP{};
    }
    return r;
}

/* 把多项式 p 中的变量 vidx 替换成 (k * uidx + b)，其余项不变 */
MP mp_subst(const MP& p, int vidx, int uidx, double k, double b, bool& over) {
    MP out;
    for (const auto& t : p.t) {
        const int e = (vidx == 0) ? t.ex : (vidx == 1 ? t.ey : t.ez);
        if (e == 0) { out.t.push_back(t); continue; }

        MP base = mp_add(mp_c(b), mp_scale(mp_var(uidx), k), false, over);
        if (over) return out;
        MP bp = mp_pow(base, e, over);
        if (over) return out;

        MP rest = mp_c(t.c);
        if (t.ex - (vidx == 0 ? e : 0) > 0) {
            MP q = mp_pow(mp_var(0), t.ex - (vidx == 0 ? e : 0), over);
            if (over) return out;
            rest = mp_mul(rest, q, over);
        }
        if (t.ey - (vidx == 1 ? e : 0) > 0) {
            MP q = mp_pow(mp_var(1), t.ey - (vidx == 1 ? e : 0), over);
            if (over) return out;
            rest = mp_mul(rest, q, over);
        }
        if (t.ez - (vidx == 2 ? e : 0) > 0) {
            MP q = mp_pow(mp_var(2), t.ez - (vidx == 2 ? e : 0), over);
            if (over) return out;
            rest = mp_mul(rest, q, over);
        }
        rest = mp_mul(rest, bp, over);
        if (over) return out;
        for (const auto& x : rest.t) out.t.push_back(x);
        if (out.t.size() > MAX_TERMS) { over = true; return out; }
    }
    out.norm();
    return out;
}

/* ============================================================
 *  有理式 num/den
 * ============================================================ */
struct Q { MP n, d; };

Q q_c(double c)  { return Q{mp_c(c), mp_c(1.0)}; }
Q q_v(int u)     { return Q{mp_var(u), mp_c(1.0)}; }

Q q_add(const Q& a, const Q& b, bool sub, bool& over) {
    Q r;
    r.d = mp_mul(a.d, b.d, over);
    if (over) return r;
    MP t1 = mp_mul(a.n, b.d, over);
    if (over) return r;
    MP t2 = mp_mul(b.n, a.d, over);
    if (over) return r;
    r.n = mp_add(t1, t2, sub, over);
    return r;
}

Q q_mul(const Q& a, const Q& b, bool& over) {
    Q r;
    r.n = mp_mul(a.n, b.n, over);
    if (!over) r.d = mp_mul(a.d, b.d, over);
    return r;
}

Q q_div(const Q& a, const Q& b, bool& over, bool& divzero) {
    Q r;
    if (b.n.zero()) { divzero = true; return r; }
    r.n = mp_mul(a.n, b.d, over);
    if (!over) r.d = mp_mul(a.d, b.n, over);
    return r;
}

Q q_pow(const Q& a, int e, bool& over, bool& divzero) {
    Q r = q_c(1.0);
    if (e == 0) return r;
    if (e < 0) {
        if (a.n.zero()) { divzero = true; return r; }
        Q base{a.d, a.n};
        e = -e;
        for (int i = 0; i < e; i++) {
            r = q_mul(r, base, over);
            if (over) return r;
        }
        return r;
    }
    for (int i = 0; i < e; i++) {
        r = q_mul(r, a, over);
        if (over) return r;
    }
    return r;
}

/* ============================================================
 *  递归下降解析
 * ============================================================ */
class Parser {
public:
    explicit Parser(const std::string& s) : _s(s) {}

    bool parse(Q& out, std::string& err) {
        Q v = expr();
        skip();
        if (_err) { err = _msg; return false; }
        if (_p < _s.size()) {
            err = "看不懂的字符 '" + std::string(1, _s[_p]) + "'";
            return false;
        }
        out = v;
        return true;
    }

private:
    void skip() { while (_p < _s.size() && (_s[_p] == ' ' || _s[_p] == '\t')) _p++; }

    void fail(const std::string& m) {
        if (!_err) { _err = true; _msg = m; }
    }

    Q expr() {
        Q v = term();
        for (;;) {
            skip();
            if (_p < _s.size() && (_s[_p] == '+' || _s[_p] == '-')) {
                const bool sub = (_s[_p] == '-');
                _p++;
                Q r = term();
                v = q_add(v, r, sub, _over);
                if (_over) fail("次数太高（单变量最多 8 次）");
                if (_err) return q_c(0);
            } else break;
        }
        return v;
    }

    Q term() {
        Q v = unary();
        for (;;) {
            skip();
            if (_p >= _s.size()) break;
            const char ch = _s[_p];
            if (ch == '*' || ch == '/') {
                const bool div = (ch == '/');
                _p++;
                Q r = unary();
                if (div) {
                    bool dz = false;
                    v = q_div(v, r, _over, dz);
                    if (dz) { fail("除数为 0"); return q_c(0); }
                } else {
                    v = q_mul(v, r, _over);
                }
                if (_over) fail("次数太高（单变量最多 8 次）");
                if (_err) return q_c(0);
                continue;
            }
            /* 隐式乘：2x、x(x+1)、3(x-1)、x2 */
            if (std::isdigit((unsigned char)ch) || ch == '(' ||
                ch == 'x' || ch == 'y' || ch == 'z') {
                Q r = unary();
                v = q_mul(v, r, _over);
                if (_over) fail("次数太高（单变量最多 8 次）");
                if (_err) return q_c(0);
                continue;
            }
            break;
        }
        return v;
    }

    Q unary() {
        skip();
        if (_p < _s.size() && (_s[_p] == '-' || _s[_p] == '+')) {
            const bool neg = (_s[_p] == '-');
            _p++;
            Q v = unary();
            if (neg) v.n = mp_scale(v.n, -1.0);
            return v;
        }
        return power();
    }

    Q power() {
        Q v = primary();
        if (_err) return v;
        skip();
        if (_p < _s.size() && _s[_p] == '^') {
            _p++;
            const int e = exponent();
            if (_err) return q_c(0);
            bool dz = false;
            v = q_pow(v, e, _over, dz);
            if (dz) { fail("0 不能作负数次幂"); return q_c(0); }
            if (_over) fail("次数太高（单变量最多 8 次）");
        }
        return v;
    }

    int exponent() {
        skip();
        bool neg = false;
        if (_p < _s.size() && (_s[_p] == '-' || _s[_p] == '+')) {
            neg = (_s[_p] == '-');
            _p++;
            skip();
        }
        bool paren = false;
        if (_p < _s.size() && _s[_p] == '(') { paren = true; _p++; }
        skip();
        int e = 0;
        bool has = false;
        while (_p < _s.size() && std::isdigit((unsigned char)_s[_p])) {
            e = e * 10 + (_s[_p] - '0');
            _p++;
            has = true;
            if (e > 32) { fail("指数太大"); return 0; }
        }
        if (!has) { fail("指数只能是数字（比如 x^2）"); return 0; }
        if (paren) {
            skip();
            if (_p < _s.size() && _s[_p] == ')') _p++;
            else { fail("括号没闭合"); return 0; }
        }
        if (e > 8) { fail("次数太高（单变量最多 8 次）"); return 0; }
        return neg ? -e : e;
    }

    Q primary() {
        skip();
        if (_p >= _s.size()) { fail("式子不完整"); return q_c(0); }

        const char ch = _s[_p];

        if (ch == '(') {
            _p++;
            Q v = expr();
            if (_err) return v;
            skip();
            if (_p < _s.size() && _s[_p] == ')') _p++;
            else fail("括号没闭合");
            return v;
        }
        if (std::isdigit((unsigned char)ch) || ch == '.') {
            const size_t st = _p;
            while (_p < _s.size() && (std::isdigit((unsigned char)_s[_p]) || _s[_p] == '.')) _p++;
            return q_c(std::atof(_s.substr(st, _p - st).c_str()));
        }
        if (ch == 'x' || ch == 'y' || ch == 'z') {
            _p++;
            return q_v(ch == 'x' ? 0 : (ch == 'y' ? 1 : 2));
        }
        if (std::isalpha((unsigned char)ch)) {
            fail(std::string("只认 x、y、z 三个未知数，不认 '") + std::string(1, ch) + "'");
            return q_c(0);
        }
        fail("看不懂的字符 '" + std::string(1, ch) + "'");
        return q_c(0);
    }

    const std::string& _s;
    size_t _p = 0;
    bool _err = false;
    bool _over = false;
    std::string _msg;
};

/* ============================================================
 *  一元多项式求根
 * ============================================================ */
void trim_lead(std::vector<double>& a) {
    while (a.size() > 1 && std::fabs(a.back()) < 1e-12) a.pop_back();
}

double horner(const std::vector<double>& a, double x) {
    double v = 0.0;
    for (int i = static_cast<int>(a.size()) - 1; i >= 0; i--) v = v * x + a[i];
    return v;
}

std::vector<double> roots_of(std::vector<double> a, int depth) {
    std::vector<double> r;
    trim_lead(a);
    const int n = static_cast<int>(a.size()) - 1;
    if (n <= 0 || depth > 10) return r;

    if (n == 1) {
        r.push_back(-a[0] / a[1]);
        return r;
    }
    if (n == 2) {
        const double A = a[2], B = a[1], C = a[0];
        const double d = B * B - 4.0 * A * C;
        if (d < 0) return r;
        const double sq = std::sqrt(d);
        if (sq < 1e-9) {
            r.push_back(-B / (2.0 * A));
        } else {
            r.push_back((-B + sq) / (2.0 * A));
            r.push_back((-B - sq) / (2.0 * A));
        }
        return r;
    }

    /* n>=3：先求导数根，把实轴切成单调段 */
    std::vector<double> da;
    da.reserve(a.size() - 1);
    for (int i = 1; i < static_cast<int>(a.size()); i++) da.push_back(a[i] * i);
    const std::vector<double> crit = roots_of(da, depth + 1);

    /* Cauchy 上界：1 + max|a_i / a_n| */
    double R = 1.0;
    for (int i = 0; i < n; i++) {
        const double q = std::fabs(a[i] / a[n]);
        if (q > R) R = q;
    }
    R = R + 1.0;
    if (R < 2.0) R = 2.0;

    std::vector<double> pts;
    pts.push_back(-R);
    for (double c : crit)
        if (c > -R && c < R) pts.push_back(c);
    pts.push_back(R);
    std::sort(pts.begin(), pts.end());

    /* 规模，用于重根判据 */
    double scale = 1.0;
    for (int i = 0; i <= n; i++) scale += std::fabs(a[i]) * std::pow(R, i);

    for (double c : pts) {
        if (std::fabs(horner(a, c)) < 1e-9 * scale) r.push_back(c);
    }

    for (size_t i = 0; i + 1 < pts.size(); i++) {
        double lo = pts[i], hi = pts[i + 1];
        if (hi - lo < 1e-12) continue;
        double flo = horner(a, lo), fhi = horner(a, hi);
        if (flo * fhi < 0) {
            for (int k = 0; k < 90; k++) {
                const double mid = (lo + hi) * 0.5;
                const double fm = horner(a, mid);
                if (fm == 0.0) { lo = hi = mid; break; }
                if (flo * fm < 0) { hi = mid; fhi = fm; }
                else { lo = mid; flo = fm; }
            }
            r.push_back((lo + hi) * 0.5);
        }
    }
    return r;
}

/* 去重 + 就近整数化 */
std::vector<double> roots_clean(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    std::vector<double> o;
    for (double x : v) {
        if (!o.empty()) {
            const double d = std::fabs(x - o.back());
            if (d < 1e-9 * (1.0 + std::fabs(x))) continue;
        }
        o.push_back(x);
    }
    for (double& x : o) {
        const double nr = std::round(x);
        if (std::fabs(x - nr) < 1e-9 * (1.0 + std::fabs(x))) x = nr;
    }
    return o;
}

/* ============================================================
 *  小工具
 * ============================================================ */
const char* var_name(int u) {
    static const char* n[3] = {"x", "y", "z"};
    return n[u];
}

std::string fmt_num(double v) {
    if (std::fabs(v) < 1e-12) v = 0.0;
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.10g", v);
    return buf;
}

int var_mask(const MP& p) {
    int m = 0;
    for (const auto& t : p.t) {
        if (t.ex) m |= 1;
        if (t.ey) m |= 2;
        if (t.ez) m |= 4;
    }
    return m;
}

int total_deg(const MP& p) {
    int d = 0;
    for (const auto& t : p.t) {
        const int s = t.ex + t.ey + t.ez;
        if (s > d) d = s;
    }
    return d;
}

/* 一元多项式抽成系数（a[i] 是 x^i 的系数） */
std::vector<double> to_coeffs(const MP& p, int u) {
    std::vector<double> a;
    for (const auto& t : p.t) {
        int d = (u == 0) ? t.ex : (u == 1 ? t.ey : t.ez);
        if (d < 0) d = 0;
        if (d >= static_cast<int>(a.size())) a.resize(d + 1, 0.0);
        a[d] += t.c;
    }
    trim_lead(a);
    if (a.empty()) a.push_back(0.0);
    return a;
}

/* 分母是否为常数（可忽略） */
bool den_is_const(const MP& d, double& k) {
    if (d.t.empty()) { k = 0; return false; }
    if (d.t.size() != 1) return false;
    if (d.t[0].ex || d.t[0].ey || d.t[0].ez) return false;
    k = d.t[0].c;
    return true;
}

/* ============================================================
 *  求解入口
 * ============================================================ */
SolveResult solve_linear(const std::vector<MP>& eqs) {
    SolveResult r;
    int mask = 0;
    for (const auto& e : eqs) mask |= var_mask(e);
    int n = __builtin_popcount(mask);

    if (n == 0) {
        for (const auto& e : eqs) {
            if (!e.zero()) {
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

    int vars[3], nv = 0;
    for (int u = 0; u < 3; u++)
        if (mask & (1 << u)) vars[nv++] = u;

    const size_t m = eqs.size();
    if (m != static_cast<size_t>(n)) {
        r.detail = std::to_string(n) + " 个未知数需要 " + std::to_string(n) + " 个方程（逗号隔开）";
        return r;
    }

    /* 系数矩阵 n×n，右端 -常数项 */
    double M[3][4] = {{0}};
    for (size_t i = 0; i < m; i++) {
        for (const auto& t : eqs[i].t) {
            if (t.ex || t.ey || t.ez) {
                int which = -1;
                if (t.ex) which = 0;
                else if (t.ey) which = 1;
                else which = 2;
                for (int j = 0; j < n; j++)
                    if (vars[j] == which) M[i][j] += t.c;
            } else {
                M[i][3] = -t.c;
            }
        }
    }

    if (n == 2) {
        const double dlt = M[0][0] * M[1][1] - M[1][0] * M[0][1];
        r.ok = true;
        r.kind = "二元一次方程组";
        if (std::fabs(dlt) <= 1e-12) {
            r.detail = "无唯一解（两方程平行或重合）";
            return r;
        }
        r.detail = std::string(var_name(vars[0])) + " = " + fmt_num((M[0][3] * M[1][1] - M[1][3] * M[0][1]) / dlt) +
                   ", " + var_name(vars[1]) + " = " + fmt_num((M[0][0] * M[1][3] - M[1][0] * M[0][3]) / dlt);
        return r;
    }

    /* n == 3：高斯消元（部分主元） */
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
            const double f = M[i][col] / M[col][col];
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

SolveResult solve_univariate(const Q& q, int u) {
    SolveResult r;

    const std::vector<double> num = to_coeffs(q.n, u);
    double den_k = 0.0;
    const bool den_const = den_is_const(q.d, den_k);

    if (q.n.zero()) {
        r.ok = true;
        r.kind = "恒等式";
        r.detail = "无数解（两边恒等）";
        return r;
    }

    const int deg = static_cast<int>(num.size()) - 1;

    std::vector<double> roots = roots_clean(roots_of(num, 0));

    /* 分式方程：剔除使分母为 0 的点 */
    std::string dropped;
    if (!den_const) {
        const std::vector<double> droots = roots_clean(roots_of(to_coeffs(q.d, u), 0));
        std::vector<double> keep;
        for (double x : roots) {
            bool bad = false;
            for (double d : droots)
                if (std::fabs(x - d) < 1e-6 * (1.0 + std::fabs(x))) { bad = true; break; }
            if (bad) {
                if (!dropped.empty()) dropped += ", ";
                dropped += std::string(var_name(u)) + " = " + fmt_num(x);
            } else {
                keep.push_back(x);
            }
        }
        roots = keep;
        if (den_k == 0.0 && q.d.zero()) {
            r.detail = "分母恒为 0，方程无意义";
            return r;
        }
    }

    const bool frac = !den_const;
    r.ok = true;

    if (roots.empty()) {
        r.kind = frac ? "分式方程·无解" : "无实根";
        r.detail = "没有满足定义域的实根";
        if (!dropped.empty()) r.detail += "（" + dropped + " 是增根，已舍去）";
        return r;
    }

    std::string kind;
    switch (deg) {
        case 1: kind = "一元一次"; break;
        case 2: kind = "一元二次"; break;
        case 3: kind = "一元三次"; break;
        default: kind = "一元" + std::to_string(deg) + "次"; break;
    }
    if (frac) kind = "分式方程·" + kind;

    if (deg >= 2) kind += (roots.size() == 1 ? "·单根" : "·" + std::to_string(roots.size()) + " 个实根");

    r.kind = kind;
    for (size_t i = 0; i < roots.size(); i++) {
        if (i) r.detail += ", ";
        if (roots.size() > 1) r.detail += std::string(var_name(u)) + std::to_string(i + 1);
        else r.detail += std::string(var_name(u));
        r.detail += " = " + fmt_num(roots[i]);
    }
    if (!dropped.empty()) r.detail += "（已舍 " + dropped + " 增根）";
    return r;
}

/* 二元方程组：一条一次式 + 一条高次式 → 代入消元，化成一元方程 */
bool solve_by_substitution(const std::vector<MP>& polys, int u, int v, SolveResult& r) {
    int lin = -1;
    for (size_t i = 0; i < polys.size(); i++)
        if (total_deg(polys[i]) <= 1) { lin = static_cast<int>(i); break; }
    if (lin < 0) return false;
    if (total_deg(polys[1 - lin]) <= 1) return false;   /* 两条都一次，走线性 */

    double cf[3] = {0, 0, 0}, C = 0;
    for (const auto& t : polys[lin].t) {
        if (t.ex) cf[0] += t.c;
        else if (t.ey) cf[1] += t.c;
        else if (t.ez) cf[2] += t.c;
        else C += t.c;
    }

    int keep = u, elim = v;
    if (std::fabs(cf[elim]) < 1e-12) std::swap(keep, elim);
    if (std::fabs(cf[elim]) < 1e-12) return false;

    const double k = -cf[keep] / cf[elim];
    const double b = -C / cf[elim];

    bool over = false;
    const MP one = mp_subst(polys[1 - lin], elim, keep, k, b, over);
    if (over) { r.detail = "次数太高（单变量最多 8 次）"; return true; }

    const std::vector<double> roots = roots_clean(roots_of(to_coeffs(one, keep), 0));

    r.ok = true;
    r.kind = "二元二次方程组";
    if (roots.empty()) {
        r.detail = "没有实数解";
        return true;
    }
    for (size_t i = 0; i < roots.size(); i++) {
        if (i) r.detail += ", ";
        const double x0 = roots[i];
        const double y0 = k * x0 + b;
        const std::string suf = roots.size() > 1 ? std::to_string(i + 1) : "";
        r.detail += std::string(var_name(keep)) + suf + " = " + fmt_num(x0) +
                    ", " + var_name(elim) + suf + " = " + fmt_num(y0);
    }
    return true;
}

} // namespace

/* ============================================================
 *  公开接口
 * ============================================================ */
std::string SolveResult::text() const {
    if (!ok) return "无法求解: " + detail;
    return kind + ": " + detail;
}

SolveResult solve_equations(const std::string& input) {
    SolveResult r;

    /* 归一化：去空白、转小写 */
    std::string s;
    s.reserve(input.size());
    for (char ch : input) {
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
        s += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (s.empty()) { r.detail = "空式子"; return r; }

    /* 逗号拆方程（括号内的逗号不拆，当前语法里括号内不会有逗号） */
    std::vector<std::string> parts;
    {
        std::string cur;
        int depth = 0;
        for (char ch : s) {
            if (ch == '(') depth++;
            if (ch == ')') depth--;
            if (ch == ',' && depth == 0) {
                parts.push_back(cur);
                cur.clear();
            } else {
                cur += ch;
            }
        }
        parts.push_back(cur);
    }

    std::vector<Q> eqs;
    std::vector<MP> polys;      /* 每个方程：左边 - 右边，已通分后的分子 */

    for (const auto& part : parts) {
        if (part.empty()) { r.detail = "有空的方程（逗号多写了）"; return r; }

        const size_t eqpos = part.find('=');
        if (eqpos == std::string::npos) { r.detail = "式子里没有等号"; return r; }
        if (part.find('=', eqpos + 1) != std::string::npos) { r.detail = "等号只能有一个"; return r; }

        Q lhs, rhs;
        std::string err;
        Parser pl(part.substr(0, eqpos));
        if (!pl.parse(lhs, err)) { r.detail = err; return r; }
        Parser pr(part.substr(eqpos + 1));
        if (!pr.parse(rhs, err)) { r.detail = err; return r; }

        bool over = false;
        Q diff = q_add(lhs, rhs, true, over);
        if (over) { r.detail = "次数太高（单变量最多 8 次）"; return r; }
        eqs.push_back(diff);
        polys.push_back(diff.n);
    }

    /* 所有分子各自涉及的未知数取并集（不能相加，否则 +y 与 -y 会抵消） */
    int mask = 0;
    for (const auto& p : polys) mask |= var_mask(p);
    const int nv = __builtin_popcount(mask);
#ifdef SOLVER_DEBUG
    std::fprintf(stderr, "[dbg] eqs=%d mask=%d\n", (int)eqs.size(), mask);
#endif

    if (nv == 0) {
        /* 纯数字式子：0=0 或 0=5 */
        return solve_linear(polys);
    }

    if (nv == 1) {
        if (eqs.size() != 1) { r.detail = "一个未知数只需要一个方程"; return r; }
        int u = 0;
        for (int i = 0; i < 3; i++)
            if (mask & (1 << i)) u = i;
        return solve_univariate(eqs[0], u);
    }

    /* 多元：分母含未知数直接拒绝 */
    if (eqs.size() < static_cast<size_t>(nv)) {
        r.detail = std::to_string(nv) + " 个未知数需要 " + std::to_string(nv) + " 个方程（逗号隔开）";
        return r;
    }
    for (size_t i = 0; i < eqs.size(); i++) {
        double k = 0.0;
        if (!den_is_const(eqs[i].d, k) || k == 0.0) {
            r.detail = "方程组不支持分母含未知数的式子";
            return r;
        }
    }

    /* 二元：若一条是一次式、另一条是高次式，先代入消元成单变量方程 */
    if (nv == 2 && eqs.size() == 2) {
        int u = 0, v = 0, cnt = 0;
        for (int i = 0; i < 3; i++)
            if (mask & (1 << i)) { if (cnt == 0) u = i; else v = i; cnt++; }
        SolveResult sub;
        if (solve_by_substitution(polys, u, v, sub)) return sub;
    }

    for (size_t i = 0; i < eqs.size(); i++) {
        if (total_deg(polys[i]) > 1) {
            r.detail = "多元方程只支持一次（括号可以，次数不能高于 1）";
            return r;
        }
    }
    return solve_linear(polys);
}

} // namespace math
} // namespace memoria

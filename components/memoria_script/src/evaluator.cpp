/**
 * @file evaluator.cpp
 * @brief memoria::script Evaluator（AST → 值）
 *
 * 纯 C++，零 ESP-IDF 依赖。
 */

#include "evaluator.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <functional>

namespace memoria {
namespace script {

/* 循环步数上限（死循环保护） */
static const size_t LOOP_MAX = 100000;

/* 循环作用域 RAII——While/For 进入 +1、任何退出路径 -1，
   裸 break/continue（depth==0）在 Break/Continue 分支报错，不泄漏标志到外层 */
struct LoopScope {
    Env& env;
    explicit LoopScope(Env& e) : env(e) { env.loop_depth++; }
    ~LoopScope() { env.loop_depth--; }
};

/* ---------- Value 方法 ---------- */
bool   Value::truthy() const {
    switch (kind) {
        case Kind::Int:    return as_int != 0;
        case Kind::Float:  return as_float != 0.0;
        case Kind::Bool:   return as_bool;
        case Kind::String: return !as_string.empty();
        case Kind::Null:   return false;
        case Kind::Callable: return true;
    }
    return false;
}
double Value::to_num() const {
    switch (kind) {
        case Kind::Int:    return as_int;
        case Kind::Float:  return as_float;
        case Kind::Bool:   return as_bool ? 1.0 : 0.0;
        case Kind::String: { char* _e = nullptr; double _v = strtod(as_string.c_str(), &_e); return (_e && *_e == 0) ? _v : 0.0; }
        default: return 0.0;
    }
}
int Value::to_int() const { return static_cast<int>(to_num()); }

/* ---------- Env ---------- */
Env::Env() {
    /* rand 播种一次（原每次复位序列一致） */
    static std::once_flag s_rand_seed;
    std::call_once(s_rand_seed, [] { std::srand(static_cast<unsigned>(std::time(nullptr))); });

    /* 0: print */
    builtins.push_back([this](const std::vector<Value>& args) -> Value {
        std::string s;
        for (size_t i = 0; i < args.size(); i++) {
            if (i) s += " ";
            s += Env::value_to_string(args[i]);
        }
        if (output_fn) output_fn(s);
        return Value::make_null();
    });
    builtin_idx["print"] = 0;
    builtin_idx["console_log"] = 0;

    /* 1: print_line */
    builtins.push_back([this](const std::vector<Value>& args) -> Value {
        std::string s;
        for (size_t i = 0; i < args.size(); i++) {
            if (i) s += " ";
            s += Env::value_to_string(args[i]);
        }
        if (output_fn) output_fn(s + "\n");
        return Value::make_null();
    });
    builtin_idx["print_line"] = 1;
    builtin_idx["println"] = 1;

    /* 2: rand() */
    builtins.push_back([](const std::vector<Value>&) -> Value {
        return Value::make_float(std::rand() / double(RAND_MAX));
    });
    builtin_idx["rand"] = 2;

    /* 3: sleep_ms：经 sleep_ms_fn 平台桥执行（host 空实现，嵌入式侧为 vTaskDelay） */
    builtins.push_back([this](const std::vector<Value>& args) -> Value {
        int ms = args.empty() ? 0 : args[0].to_int();
        if (sleep_ms_fn) sleep_ms_fn(ms);
        return Value::make_null();
    });
    builtin_idx["sleep_ms"] = 3;

    /* 4: len(str) */
    builtins.push_back([](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_int(0);
        if (args[0].kind == Value::Kind::String)
            return Value::make_int(static_cast<int>(args[0].as_string.size()));
        return Value::make_int(0);
    });
    builtin_idx["len"] = 4;
}

Value Env::lookup(const std::string& name) const {
    auto it = vars.find(name);
    if (it != vars.end()) return it->second;
    auto bit = builtin_idx.find(name);
    if (bit != builtin_idx.end()) return Value::make_callable(bit->second);
    return Value::make_null();
}

std::string Env::value_to_string(const Value& v) {
    switch (v.kind) {
        case Value::Kind::Int:    return std::to_string(v.as_int);
        case Value::Kind::Float: {
            char buf[32]; std::snprintf(buf, sizeof(buf), "%g", v.as_float);
            return buf;
        }
        case Value::Kind::String: return v.as_string;
        case Value::Kind::Bool:   return v.as_bool ? "true" : "false";
        case Value::Kind::Null:   return "null";
        case Value::Kind::Callable: return "<builtin>";
    }
    return "?";
}

static bool value_eq(const Value& a, const Value& b) {
    if (a.kind == b.kind) {
        switch (a.kind) {
            case Value::Kind::Int:    return a.as_int == b.as_int;
            case Value::Kind::Float:  return a.as_float == b.as_float;
            case Value::Kind::String: return a.as_string == b.as_string;
            case Value::Kind::Bool:   return a.as_bool == b.as_bool;
            default: return true;
        }
    }
    /* 跨类型数值比较 */
    bool a_num = (a.kind == Value::Kind::Int || a.kind == Value::Kind::Float || a.kind == Value::Kind::Bool);
    bool b_num = (b.kind == Value::Kind::Int || b.kind == Value::Kind::Float || b.kind == Value::Kind::Bool);
    if (a_num && b_num) return a.to_num() == b.to_num();
    return false;
}

/* ---------- eval_node ---------- */
static Value eval_node(const ASTNode& n, Env& env);

static Value eval_node(const ASTNode& n, Env& env) {
    switch (n.type) {
        case ASTNode::Type::IntLit:
            { char* _e = nullptr; long _v = strtol(n.value.c_str(), &_e, 10);
              if (!_e || *_e != 0) { env.error_pending = true; env.error_msg = "invalid int literal: " + n.value; return Value::make_null(); }
              return Value::make_int((int)_v); }
        case ASTNode::Type::FloatLit:
            { char* _e = nullptr; double _v = strtod(n.value.c_str(), &_e);
              if (!_e || *_e != 0) { env.error_pending = true; env.error_msg = "invalid float literal: " + n.value; return Value::make_null(); }
              return Value::make_float(_v); }
        case ASTNode::Type::StringLit: return Value::make_string(n.value);
        case ASTNode::Type::BoolLit:   return Value::make_bool(n.value == "true");
        case ASTNode::Type::NullLit:   return Value::make_null();

        case ASTNode::Type::Ident: {
            if (n.value == "true")  return Value::make_bool(true);
            if (n.value == "false") return Value::make_bool(false);
            if (n.value == "null")  return Value::make_null();
            return env.lookup(n.value);
        }

        case ASTNode::Type::Block:
        case ASTNode::Type::Program: {
            Value last = Value::make_null();
            for (auto& c : n.children) {
                if (!c) continue;
                last = eval_node(*c, env);
                /* continue_pending 同样需要打断块循环（此前仅检查 return/break） */
                if (env.return_pending || env.break_pending || env.continue_pending) break;
                /* 错误后块内语句停止执行（此前错误状态下仍执行完整块，直至 runtime 统一报错） */
                if (env.error_pending) break;
            }
            return last;
        }

        case ASTNode::Type::VarDecl: {
            Value v = Value::make_null();
            if (n.children.size() >= 1) v = eval_node(*n.children[0], env);
            env.vars[n.value] = v;
            return v;
        }

        case ASTNode::Type::Assign: {
            if (n.children.size() < 2) return Value::make_null();
            Value rhs = eval_node(*n.children[1], env);
            auto* lhs = dynamic_cast<const ASTNode*>(n.children[0].get());
            if (lhs && lhs->type == ASTNode::Type::Ident) {
                /* 区分 = / += / -=（原 Assign 分支一律覆盖，x+=2 变为 x=2） */
                const std::string& op = n.value;
                if (op == "+=") {
                    double cur = env.lookup(lhs->value).to_num();
                    env.vars[lhs->value] = Value::make_float(cur + rhs.to_num());
                } else if (op == "-=") {
                    double cur = env.lookup(lhs->value).to_num();
                    env.vars[lhs->value] = Value::make_float(cur - rhs.to_num());
                } else {
                    env.vars[lhs->value] = rhs;
                }
            }
            return rhs;
        }

        case ASTNode::Type::Binary: {
            if (n.children.size() < 2) return Value::make_null();
            Value l = eval_node(*n.children[0], env);
            const std::string& op = n.value;
            /* && || 短路求值——左假/左真时不求值右操作数，
               x!=0 && 10/x>1 除零保护惯用法不再触发除零（此前两个 child 均先求值） */
            if (op == "&&" && !l.truthy()) return Value::make_bool(false);
            if (op == "||" && l.truthy()) return Value::make_bool(true);
            Value r = eval_node(*n.children[1], env);

            if (op == "+" || op == "-" || op == "*" || op == "/" || op == "%") {
                double a = l.to_num(), b = r.to_num();
                double rr = 0;
                if (op == "+") rr = a + b;
                else if (op == "-") rr = a - b;
                else if (op == "*") rr = a * b;
                else if (op == "/" || op == "%") {
                    /* 除零不再静默返回 0，置运行期错误 */
                    if (b == 0) {
                        env.error_pending = true;
                        env.error_msg = "divide by zero";
                        return Value::make_null();
                    }
                    rr = (op == "/") ? a / b : std::fmod(a, b);
                }
                bool both_int = (l.kind == Value::Kind::Int && r.kind == Value::Kind::Int);
                if (both_int && op != "/" && op != "%") return Value::make_int(static_cast<int>(rr));
                return Value::make_float(rr);
            }
            if (op == "==") return Value::make_bool(value_eq(l, r));
            if (op == "!=") return Value::make_bool(!value_eq(l, r));
            if (op == "<")  return Value::make_bool(l.to_num() <  r.to_num());
            if (op == ">")  return Value::make_bool(l.to_num() >  r.to_num());
            if (op == "<=") return Value::make_bool(l.to_num() <= r.to_num());
            if (op == ">=") return Value::make_bool(l.to_num() >= r.to_num());
            if (op == "&&") return Value::make_bool(r.truthy());   /* 短路未触发（左真）时 */
            if (op == "||") return Value::make_bool(r.truthy());   /* 短路未触发（左假）时 */
            /* Binary 分支的 +=/-= 是死代码（parser 不生成），随 Assign 分派一并清除 */
            return Value::make_null();
        }

        case ASTNode::Type::Unary: {
            if (n.children.empty()) return Value::make_null();
            Value v = eval_node(*n.children[0], env);
            if (n.value == "-") return Value::make_float(-v.to_num());
            if (n.value == "!") return Value::make_bool(!v.truthy());
            return v;
        }

        case ASTNode::Type::Call: {
            if (n.children.empty()) return Value::make_null();
            Value callee = eval_node(*n.children[0], env);
            std::vector<Value> args;
            for (size_t i = 1; i < n.children.size(); i++)
                args.push_back(eval_node(*n.children[i], env));
            if (callee.kind == Value::Kind::Callable
                && callee.as_callable >= 0
                && callee.as_callable < (int)env.builtins.size()) {
                return env.builtins[callee.as_callable](args);
            }
            return Value::make_null();
        }

        case ASTNode::Type::If: {
            if (n.children.size() < 2) return Value::make_null();
            Value cond = eval_node(*n.children[0], env);
            /* 条件求值出错则停止（与 While/For 一致，此前错误后仍走 else 分支） */
            if (env.error_pending) return Value::make_null();
            if (cond.truthy()) return eval_node(*n.children[1], env);
            if (n.children.size() >= 3) return eval_node(*n.children[2], env);
            return Value::make_null();
        }

        /* while/for 循环（含死循环步数保护） */
        case ASTNode::Type::While: {
            if (n.children.empty()) return Value::make_null();
            LoopScope _ls(env);   /* 循环深度计数 */
            size_t guard = 0;
            while (true) {
                if (env.return_pending) break;
                if (++guard > LOOP_MAX) {
                    env.error_pending = true; env.error_msg = "loop limit exceeded"; return Value::make_null();
                }
                Value c = eval_node(*n.children[0], env);
                if (env.error_pending) return Value::make_null();
                if (!c.truthy()) break;
                if (n.children.size() > 1 && n.children[1]) eval_node(*n.children[1], env);
                if (env.error_pending) return Value::make_null();
                if (env.break_pending) { env.break_pending = false; break; }
                if (env.continue_pending) { env.continue_pending = false; continue; }
            }
            return Value::make_null();
        }
        case ASTNode::Type::For: {
            if (n.children.size() < 3) return Value::make_null();
            LoopScope _ls(env);   /* 循环深度计数 */
            if (n.children[0]) eval_node(*n.children[0], env);   /* init */
            if (env.error_pending) return Value::make_null();
            size_t guard = 0;
            while (true) {
                if (env.return_pending) break;
                if (++guard > LOOP_MAX) {
                    env.error_pending = true; env.error_msg = "loop limit exceeded"; return Value::make_null();
                }
                if (n.children[1]) {
                    Value c = eval_node(*n.children[1], env);   /* 条件 */
                    if (env.error_pending) return Value::make_null();
                    if (!c.truthy()) break;
                }
                if (n.children.size() > 3 && n.children[3]) eval_node(*n.children[3], env);   /* body */
                if (env.error_pending) return Value::make_null();
                if (env.break_pending) { env.break_pending = false; break; }
                if (env.continue_pending) env.continue_pending = false;   /* continue → 跳步进 */
                if (n.children[2]) eval_node(*n.children[2], env);   /* step */
                if (env.error_pending) return Value::make_null();
            }
            return Value::make_null();
        }

        case ASTNode::Type::Return: {
            Value v = Value::make_null();
            if (!n.children.empty()) v = eval_node(*n.children[0], env);
            env.return_pending = true;
            env.return_value = v;
            return v;
        }
        case ASTNode::Type::Break:
            /* 裸 break（无循环）报错，不泄漏标志到外层 */
            if (env.loop_depth == 0) {
                env.error_pending = true;
                env.error_msg = "break outside loop";
            } else {
                env.break_pending = true;
            }
            return Value::make_null();
        case ASTNode::Type::Continue:
            if (env.loop_depth == 0) {
                env.error_pending = true;
                env.error_msg = "continue outside loop";
            } else {
                env.continue_pending = true;
            }
            return Value::make_null();

        default: return Value::make_null();
    }
}

/* 公开入口 */
Value eval(const ASTNode& ast, Env& env) {
    return eval_node(ast, env);
}

} }
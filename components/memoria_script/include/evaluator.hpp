/**
 * @file evaluator.hpp
 * @brief memoria::script Evaluator 公开接口
 *
 * 暴露：Value 类型 + Env 环境 + eval(AST, Env) 求值入口。
 * 纯 C++，零 ESP-IDF 依赖。
 */

#pragma once

#include "tokenizer.hpp"
#include <string>
#include <functional>
#include <unordered_map>
#include <vector>

namespace memoria {
namespace script {

struct Value {
    enum class Kind { Int, Float, String, Bool, Null, Callable };
    Kind kind = Kind::Null;
    int    as_int    = 0;
    double as_float  = 0.0;
    std::string as_string;
    bool   as_bool   = false;
    int    as_callable = -1;

    static Value make_int(int v)   { Value r; r.kind = Kind::Int;   r.as_int = v;   return r; }
    static Value make_float(double v) { Value r; r.kind = Kind::Float; r.as_float = v; return r; }
    static Value make_string(const std::string& s) {
        Value r; r.kind = Kind::String; r.as_string = s; return r;
    }
    static Value make_bool(bool b) { Value r; r.kind = Kind::Bool;  r.as_bool = b;  return r; }
    static Value make_null()       { Value r; r.kind = Kind::Null;                    return r; }
    static Value make_callable(int idx) { Value r; r.kind = Kind::Callable; r.as_callable = idx; return r; }

    bool   truthy() const;
    double to_num() const;
    int    to_int() const;
};

struct Env {
    std::unordered_map<std::string, Value> vars;
    std::function<void(const std::string&)> output_fn;
    /* sleep_ms 平台桥：host 默认空实现，嵌入式侧由 runtime 注入 vTaskDelay */
    std::function<void(int)> sleep_ms_fn;
    bool return_pending = false;
    Value return_value;
    bool break_pending = false;
    bool continue_pending = false;
    /* 循环嵌套深度（While/For 进入 +1 退出 -1），
       裸 break/continue（depth==0）直接报错，避免标志泄漏到外层 */
    int loop_depth = 0;
    /* 运行期错误：置位后 eval 停止出值，runtime 统一检查 */
    bool error_pending = false;
    std::string error_msg;
    /* 内置函数由 evaluator.cpp 填充 */
    std::vector<std::function<Value(const std::vector<Value>&)>> builtins;
    std::unordered_map<std::string, int> builtin_idx;

    Env();
    Value lookup(const std::string& name) const;
    static std::string value_to_string(const Value& v);
};

Value eval(const ASTNode& ast, Env& env);

} }
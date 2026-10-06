/**
 * @file basic_interpreter.hpp
 * @brief Memoria BASIC 解释器
 *
 * 纯 C++（零 ESP-IDF 依赖）的经典行号 BASIC：
 *   PRINT / LET / INPUT / GOTO / GOSUB / RETURN / IF..THEN..ELSE / END / SYSTEM
 *   FOR..NEXT / WHILE..WEND 循环；':' 多语句同行；条件可为数值（非 0 即真）
 *   INCLUDE "file.bas" 合并另一个程序文件的行（同号覆盖）
 *   数学函数：SIN COS TAN ABS SQR INT LOG LOG10 EXP POW RND
 *
 * 字符串变量（经典 $ 变量）：
 *   LET A$="hi"（LET 可省：裸赋值 A$="hi" / X=5）、+ 拼接、= <> 比较
 *   串函数：STR$ CHR$ LEN VAL LEFT$ RIGHT$ MID$；INPUT A$ 键盘录入
 *   字符串只与字符串运算，数字需 STR$(x) 转换；串变量无定义读出空串
 *
 * 原始参数扩展语句 add_raw_cmd：参数原文交给宿主解析
 *   （HTTPGET / HTTPPOST 等需要"输出到变量"的语句由宿主注入）
 *
 * GOTO 半禁用：仍然可用，但运行时提示一次注意事项（容易导致逻辑混乱/死循环）。
 * 跳转（GOTO/GOSUB/RETURN/循环）执行后不再自动前进一行，目标行真正被执行。
 *
 * 硬件能力（屏幕 / WiFi / GPIO / 串口 / 蓝牙 / 重启）全部通过
 *  add_cmd / add_func 注册回调注入（由 PRGM 模式提供），
 *  SYSTEM 命令同样走命令表（ESP32 上无 shell，由系统注入内部命令）。
 *
 * 防死循环保护（"杀线程"）：run(max_steps) 达到步数上限即强制终止。
 */

#pragma once

#include <string>
#include <map>
#include <vector>
#include <functional>
#include <cstdint>

namespace memoria {
namespace basic {

/* 扩展命令的一个参数：字符串字面量 或 数值表达式 */
struct BasicArg {
    bool   is_string = false;
    std::string str;    /* 字符串字面量内容（is_string=true 时有效） */
    double num = 0.0;   /* 表达式求值结果（is_string=false 时有效） */
};

/* DEF FN 定义：单行（expr）或多行（body，expr 为空时用） */
struct FnDef {
    std::vector<std::string> params;
    std::string expr;
    std::vector<std::string> body;
    bool multi = false;
};

/* DIM 数组：数值/字符串、一维/二维（下标从 0 起，二维行主序） */
struct BasicArray {
    bool is_str = false;
    int  d1 = 0, d2 = 0;                   /* d2==0 一维；d2>0 二维 */
    std::vector<double> num;
    std::vector<std::string> str;
    size_t flat(long i, long j) const { return d2 > 0 ? (size_t)i * (size_t)d2 + (size_t)j : (size_t)i; }
};

using BasicOutFn   = std::function<void(const std::string& line)>;                /* 输出一行 */
using BasicInputFn = std::function<double(const std::string& prompt)>;             /* INPUT 读数字 */
using BasicInputStrFn = std::function<std::string(const std::string& prompt)>;     /* INPUT 读字符串 */
using BasicRawCmdFn = std::function<void(const std::string& raw_args)>;            /* 原样参数扩展语句 */
using BasicCmdFn   = std::function<void(const std::vector<BasicArg>& args)>;       /* 扩展语句 */
using BasicFuncFn  = std::function<double(const std::vector<BasicArg>& args)>;     /* 扩展表达式函数 */

class BasicInterpreter {
public:
    BasicInterpreter();

    /* ---- IO 回调（PRGM 模式注入） ---- */
    void set_io(BasicOutFn out, BasicInputFn input);
    void set_input_str(BasicInputStrFn fn);                    /* INPUT A$ 字符串录入 */

    /* ---- 扩展注册 ---- */
    void add_cmd(const std::string& name, BasicCmdFn fn);      /* 语句：TEXT / RECT / WIFI ... */
    void add_func(const std::string& name, BasicFuncFn fn);    /* 表达式函数：RND / WIFISTAT ... */
    void add_raw_cmd(const std::string& name, BasicRawCmdFn fn); /* 语句（参数原文）：HTTPGET ... */

    /* ---- 宿主回调辅助（raw_cmd 内取值/写串变量） ---- */
    double      eval_num_expr(const std::string& expr);
    std::string eval_str_expr(const std::string& expr);
    bool        set_svar(const std::string& name, const std::string& value);
    std::string get_svar(const std::string& name);
    bool        set_nvar(const std::string& name, double value);   /* 数值变量预注入（v1.3 环境变量） */

    /* ---- 程序管理 ---- */
    void add_line(int line_num, const std::string& code);      /* 空 code = 删除该行 */
    void clear_program();
    bool load_file(const std::string& path);                   /* 读 .bas 文件 */
    bool has_program() const;
    int  line_count() const;
    void list_program(std::string& out);                       /* 供 LIST 命令 */

    /* ---- 运行 ----
     * 返回：0 正常结束 / 1 步数超限强制终止（死循环保护）
     *       / 2 语法错误终止 / 3 被宿主 request_abort 终止（APP 模式返回键）
     * APP 任务运行模式配套：
     *   request_abort()  宿主强停（每行检查，阻塞在宿主语句时由宿主自行让出）
     *   set_step_hook()  每 4096 步回调一次（宿主用来 vTaskDelay 让出 CPU，
     *                    防纯 while 1 饿死 idle 触发任务看门狗） */
    int run(uint32_t max_steps = 100000);
    void request_abort() { _abort = true; }
    void set_step_hook(std::function<void()> fn) { _step_hook = std::move(fn); }

    /* 统计 */
    uint32_t steps_run() const { return _steps; }

private:
    /* 核心（移植自 MiniBasic，行为保持一致） */
    double evaluate_expression(const std::string& expr);
    std::string evaluate_str_expr(const std::string& expr);
    bool   evaluate_condition(const std::string& cond);
    void   execute_line(const std::string& line);
    void   go_to_next_line();

    void do_print(const std::string& args);
    void do_let(const std::string& args);
    void do_input(const std::string& args);
    void do_goto(const std::string& args);
    void do_gosub(const std::string& args);
    void do_return();
    void do_if(const std::string& full_line);
    void do_system(const std::string& args);
    void do_end();

    /* 循环 / 多语句 */
    struct ForState {
        std::string var;
        double end = 0.0;
        double step = 1.0;
        int body_line = -1;   /* 循环体第一行 = FOR 的下一行 */
    };
    struct WhileState {
        int while_line = 0;   /* WHILE 语句行号 */
    };
    void do_for(const std::string& args);
    void do_next(const std::string& args);
    void do_while(const std::string& args);
    void do_wend();
    void do_read(const std::string& args);           /* READ：从 DATA 队列顺序取值 */
    void do_dim(const std::string& args);            /* DIM 数组/实例声明 */
    void do_def(const std::string& args);            /* DEF FN 单行/多行函数定义 */
    void do_type(const std::string& args);           /* TYPE 记录类型定义启动（体收字段名至 END TYPE） */
    void do_fread(const std::string& args);          /* FREAD 路径, r$：读文件进串 */
    void do_fwrite(const std::string& args, bool append); /* FWRITE / FAPPEND */
    void exec_statements(const std::string& text);   /* ':' 多语句（IF 独占整行） */
    void exec_statement(const std::string& stmt);
    void exec_if_body(const std::string& body);      /* THEN/ELSE 体：数字=GOTO，否则按多语句执行 */
    /* 块 IF：THEN 体为空时进入块模式，行集合执行 */
    void run_block_if(const std::string& cond, int if_line); /* 块 IF 判定 + 分派 */
    void exec_block_range(int from, int to, int endif_line); /* 执行 (from,to) 行区间，收尾停 endif */
    bool array_get(const std::string& name, const std::string& idx_expr, double& out);
    bool array_get_str(const std::string& name, const std::string& idx_expr, std::string& out);
    bool array_set(const std::string& name, const std::string& idx_expr, double v);
    bool array_set_str(const std::string& name, const std::string& idx_expr, const std::string& v);
    bool array_idx(const BasicArray& a, const std::string& idx_expr, size_t& out); /* 下标解析+范围校验 */
    double call_fn(const std::string& name, const std::string& arg_text, bool& ok); /* DEF FN 调用 */
    double exec_fn_body(const FnDef& f);  /* 多行函数体执行：FNRET/体尾返回，禁 GOTO/GOSUB */
    int  next_line_after(int line) const;
    int  find_matching_wend(int from_line) const;
    int  find_matching_next(int from_line) const;   /* FOR 方向不成立时跳到匹配 NEXT 之后 */

    /* 扩展分派 */
    bool dispatch_cmd(const std::string& line);                /* 未命中标准命令时查扩展表 */
    double call_func(const std::string& name, const std::string& arg_text, bool& ok);

    std::map<int, std::string> _program;       /* 行号 → 代码 */
    std::map<std::string, double> _vars;       /* 变量表（小写，大小写不敏感） */
    std::map<std::string, std::string> _svars; /* 字符串变量表（小写名含 $ 结尾） */
    std::map<std::string, BasicArray> _arrays;
    std::map<std::string, FnDef> _fns;         /* def fn：名（fn 前缀小写）→ 定义 */
    std::map<std::string, std::vector<std::string>> _types; /* TYPE：类型名 → 字段名表（$ 尾=串字段） */
    bool _collecting_type = false;      /* TYPE 体收集中（run 循环拦截） */
    std::string _type_name;             /* 收集中的类型名（小写） */
    std::vector<std::string> _type_fields;
    std::vector<std::string> _data_queue;      /* DATA 项队列（run 开始时收集） */
    std::vector<int> _gosub_stack;
    std::vector<ForState>   _for_stack;        /* FOR..NEXT 嵌套 */
    std::vector<WhileState> _while_stack;      /* WHILE..WEND 嵌套 */
    int  _current_line = 0;
    bool _running = false;
    bool _jumped = false;       /* 本行发生过跳转：run() 不再自动前进一行 */
    bool _goto_warned = false;  /* GOTO 半禁用提示：每轮运行只提示一次 */
    bool _collecting_fn = false;        /* 多行 DEF FN 函数体收集中（run 循环拦截） */
    std::string _collect_name;          /* 收集中的函数名（fn 前缀小写） */
    std::vector<std::string> _collect_params;
    std::vector<std::string> _collect_body;
    int      _fn_depth = 0;             /* 函数调用深度（递归保护，上限 32） */
    bool     _fn_returning = false;     /* FNRET 标志（函数体内任意深度触发，exec_fn_body 消费） */
    double   _fn_ret_val = 0;           /* FNRET 返回值 */
    uint32_t _max_steps = 0;            /* run() 步数上限（函数体内续用） */
    bool _abort = false;        /* 宿主强停标志（APP 模式返回键） */
    std::function<void()> _step_hook;  /* 每 4096 步让出回调（宿主注入） */
    uint32_t _steps = 0;

    BasicOutFn   _out;
    BasicInputFn _input;
    bool _has_out = false;
    bool _has_input = false;
    BasicInputStrFn _input_str;
    bool _has_input_str = false;

    /* 扩展表 */
    std::map<std::string, BasicCmdFn>   _cmds;
    std::map<std::string, BasicFuncFn>  _funcs;
    std::map<std::string, BasicRawCmdFn> _raw_cmds;

    /* 数学桥接 */
    static double _math_sin(double a); static double _math_cos(double a);
    static double _math_tan(double a); static double _math_abs(double a);
    static double _math_sqr(double a); static double _math_int(double a);
    static double _math_log(double a); static double _math_log10(double a);
    static double _math_exp(double a);
};

/* 工具：把逗号分隔的参数串解析为 BasicArg 列表（字符串字面量保留原样）
 *  str_eval 非空时：串变量（$ 结尾）/含引号的串表达式参数按字符串求值 */
std::vector<BasicArg> parse_args(const std::string& args_text,
                                 const std::function<double(const std::string&)>& eval,
                                 const std::function<std::string(const std::string&)>& str_eval = nullptr);

} // namespace basic
} // namespace memoria
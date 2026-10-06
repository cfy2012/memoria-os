/**
 * @file math_solver.hpp
 * @brief 解方程求解器公共接口（SOLVER-SPEC 落码）
 *
 * 输入式子字符串，自动识别未知数个数与次数：
 * 一元一次 / 一元二次（判别式分支）/ 二元一次方程组（克拉默法则）/
 * 三元一次方程组（高斯消元）。
 * 方程组用逗号分隔：`2x+y=5, x-y=1`。
 * 错误一律中文提示放在 detail 里，不抛异常不崩溃。
 */

#pragma once

#include <string>

namespace memoria {
namespace math {

struct SolveResult {
    bool        ok = false;   /* true = 求解成功，detail 为解 */
    std::string kind;         /* 判别说明，如 "一元二次·两个实根" */
    std::string detail;       /* 解（x = 5）或失败原因 */

    /* 单行可显示文本：成功 "判别: 解"，失败 "无法求解: 原因" */
    std::string text() const;
};

/* 解方程主入口：input 为一条或多条（逗号分隔）方程 */
SolveResult solve_equations(const std::string& input);

} // namespace math
} // namespace memoria

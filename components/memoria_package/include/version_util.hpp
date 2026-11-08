/**
 * @file version_util.hpp
 * @brief 版本号比较（纯 C++，不依赖 ESP-IDF，可直接 host 单测）
 */
#pragma once
#include <cstdlib>

namespace memoria {
namespace package {

/* "x.y.z" 三点比较：a>b 返回 1，相等 0，a<b 返回 -1。
 * 缺段按 0 处理（"1.6" 等价 "1.6.0"），非数字尾巴忽略（"1.6.7-beta" 读作 1.6.7）。 */
inline int version_cmp(const char* a, const char* b) {
    const char* pa = a ? a : "";
    const char* pb = b ? b : "";
    for (int i = 0; i < 3; i++) {
        char* ea = nullptr;
        char* eb = nullptr;
        long x = std::strtol(pa, &ea, 10);
        long y = std::strtol(pb, &eb, 10);
        if (x != y) return x > y ? 1 : -1;
        pa = ea;
        pb = eb;
        if (*pa == '.') pa++;
        if (*pb == '.') pb++;
    }
    return 0;
}

} }

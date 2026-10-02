/**
 * @file runtime.cpp
 * @brief memoria::script Runtime 入口
 *
 * 完整链路：读文件 → tokenize → parse → eval → 返回退出码。
 *
 * 平台桥：sleep_ms 在 ESP32 构建（ESP_PLATFORM）下注入 vTaskDelay，
 *         host 构建保持空实现，便于本机单测。
 */

#include "runtime.hpp"
#include "evaluator.hpp"
#include <fstream>
#include <sstream>
#include <cstdio>

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace memoria {
namespace script {

static bool read_file(const std::string& path, std::string& out) {
    std::ifstream fp(path, std::ios::binary);
    if (!fp.is_open()) return false;
    std::ostringstream ss; ss << fp.rdbuf();
    out = ss.str();
    return true;
}

int run_file_out(const std::string& path,
                 const std::function<void(const std::string&)>& out) {
    std::string src;
    if (!read_file(path, src)) {
        std::fprintf(stderr, "script: cannot read %s\n", path.c_str());
        return 1;
    }

    auto tokens = tokenize(src);
    /* 解析失败回填原因（全角字符等非法输入不再静默变 null） */
    std::string perr;
    auto ast = parse_program(tokens, &perr);
    if (!ast) { std::fprintf(stderr, "script: parse failed: %s\n", perr.c_str()); return 2; }

    Env env;
#ifdef ESP_PLATFORM
    env.sleep_ms_fn = [](int ms) { vTaskDelay(pdMS_TO_TICKS(ms)); };
#endif
    if (out) {
        env.output_fn = out;
    } else {
        env.output_fn = [](const std::string& s) { std::fprintf(stdout, "%s", s.c_str()); };
    }
    eval(*ast, env);
    if (env.error_pending) {
        std::fprintf(stderr, "script: runtime error: %s\n", env.error_msg.c_str());
        return 3;
    }
    return 0;
}

int run_file(const std::string& path) {
    return run_file_out(path, std::function<void(const std::string&)>());
}

} }
/**
 * @file runtime.hpp
 * @brief memoria::script Runtime 公开接口
 *
 * run_file：从文件系统读取 .ms 脚本并执行整条链路
 * （读文件 → tokenize → parse → eval），返回退出码。
 * 纯 C++ 接口，不依赖任何 ESP-IDF 类型。
 */

#pragma once

#include <string>
#include <functional>

namespace memoria {
namespace script {

/* 执行脚本文件，输出经回调逐段回传；返回退出码（0 成功） */
int run_file_out(const std::string& path,
                 const std::function<void(const std::string&)>& out);

/* 执行脚本文件，输出打到 stdout */
int run_file(const std::string& path);

} }
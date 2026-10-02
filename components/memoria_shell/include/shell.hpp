/**
 * @file shell.hpp
 * @brief memoria::shell Shell 交互层
 *
 * 功能：
 *   - get_line() 从 stdin 读一行（串口 shell）
 *   - execute() 解析并执行一条命令
 *   - menu() 进入 REPL 主循环
 *
 * 内置命令：help, info, battery, wifi, ble, sd, scripts, reboot,
 *           script <name>, pkg_update, pkg_mirror <url>,
 *           ota_url <url>, ota_firmware, rec start/stop
 */

#pragma once
#include <string>

namespace memoria {
namespace shell {

class Shell {
public:
    Shell();
    int  execute(const std::string& line);
    void menu();   /* REPL 主循环 */
};

} }
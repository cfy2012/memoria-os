/**
 * @file shell.hpp
 * @brief memoria::shell Shell 交互层
 *
 * 功能：
 *   - get_line() 从 stdin 读一行（串口 shell）
 *   - execute() 解析并执行一条命令
 *   - menu() 进入 REPL 主循环
 *
 * 内置命令：help, info, battery, wifi, ble, sd [mount|format], scripts, reboot,
 *           script <name>, pkg_update, pkg_mirror <url>,
 *           ota_url <url>, ota_firmware, rec start/stop
 *
 * SAFE 模式：TF 卡未挂载时进入（提示符 safe>），补应急命令
 *   sd mount（重试挂载）/ sd format（格式化重挂），挂载成功自动退出 SAFE。
 */

#pragma once
#include <string>

namespace memoria {
namespace shell {

class Shell {
public:
    Shell();
    int  execute(const std::string& line, bool safe = false);
    void menu();   /* REPL 主循环 */
};

} }
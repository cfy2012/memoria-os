/**
 * @file ota_updater.hpp
 */
#pragma once
#include <string>

namespace memoria {
namespace package {
std::string get_ota_url();
bool set_ota_url(const std::string& url);

/* 下载并写入另一槽。sha256 非空时边收边算，不一致直接放弃（返回 6）。
 * 返回码：1 handshake/open，2 镜像/切槽失败，3 传输中断，4 身份不符已切回，
 *        5 系统时间未同步，6 sha256 不符。成功重启，不返回。 */
int ota_firmware(const std::string& url = "", const std::string& sha256 = "");
} }

/**
 * @file firmware_update.hpp
 * @brief 固件更新检查与安装（版本清单 + OTA 触发）
 */
#pragma once
#include <string>

namespace memoria {
namespace package {

struct FirmwareInfo {
    bool checked = false;          /* 是否完成过检查 */
    bool update_available = false; /* 服务器有更高版本 */
    bool major = false;            /* 大版本（存储布局可能变化） */
    std::string latest;            /* 服务器最新版本号 */
    std::string url;               /* 固件下载地址 */
    std::string changelog;         /* 更新说明（可空） */
    std::string error;             /* 检查失败原因（空 = 正常） */
};

/* 当前固件版本（编译期宏） */
std::string firmware_current_version();

/* 拉取版本清单并对比；返回 true 表示检查过程正常（结果看 out.update_available） */
bool firmware_check(FirmwareInfo& out);

/* 下载并安装指定固件：写 OTA 分区后自动重启；成功不返回 */
bool firmware_apply(const std::string& url);

} }


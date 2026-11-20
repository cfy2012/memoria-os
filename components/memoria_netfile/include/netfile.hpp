/**
 * @file netfile.hpp
 * @brief 板子当读卡器：HTTP 上传页 + 流式写 TF 卡（/mem_fat、/sd_ext）
 *
 * 默认不自启；由 shell `net start ap|sta` 手动拉起，避免常驻占射频。
 */

#pragma once

#include <esp_err.h>
#include <cstdint>

namespace memoria {
namespace netfile {

/* AP 热点：SSID MemoriaOS，网页 http://192.168.4.1/ */
esp_err_t start_ap();
/* STA：用 NVS 已保存凭据连路由器，网页走分配的 IP */
esp_err_t start_sta();

void stop();
bool running();

} // namespace netfile
} // namespace memoria

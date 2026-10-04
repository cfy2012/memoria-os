/**
 * @file net_manager.hpp
 * @brief 网络状态查询适配层（BASIC v1.3 预注入用）
 *
 * 由豆包-1 提供，供 prgm_mode _inject_env() 弱依赖接入（__has_include）。
 * 六函数签名与 GLM 接口约定一致；实现包装 drivers 的 WifiManager / BleManager。
 * header-only：kernel 组件无需新增 .cpp。
 */

#pragma once

#include "wifi_manager.hpp"
#include "ble_manager.hpp"

namespace net {

/* STA 是否已连接 */
inline bool sta_connected() {
    return memoria::drivers::WifiManager::instance()->connected();
}

/* 当前连接 SSID（未连接返回空串） */
inline const char* sta_ssid() {
    auto* w = memoria::drivers::WifiManager::instance();
    return w->connected() ? w->current_ssid().c_str() : "";
}

/* NVS 保存的 WiFi 密码（未保存返回空串） */
inline const char* sta_pass() {
    auto* w = memoria::drivers::WifiManager::instance();
    const std::string& p = w->saved_pass();
    return p.empty() ? "" : p.c_str();
}

/* AP 热点 SSID / 密码（NVS "wifi"） */
inline const char* ap_ssid() {
    auto* w = memoria::drivers::WifiManager::instance();
    const std::string& s = w->ap_ssid();
    return s.c_str();
}

inline const char* ap_pass() {
    auto* w = memoria::drivers::WifiManager::instance();
    const std::string& s = w->ap_pass();
    return s.c_str();
}

/* BLE 设备名（NVS "net"） */
inline const char* ble_name() {
    auto* b = memoria::drivers::BleManager::instance();
    const std::string& s = b->device_name();
    return s.c_str();
}

} // namespace net

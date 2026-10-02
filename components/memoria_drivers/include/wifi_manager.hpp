/**
 * @file wifi_manager.hpp
 * @brief WiFi STA 模式管理：连接 / NVS 存密码 / 断开事件回调
 *
 * 开机自动重连上次成功连接的 SSID（NVS 持久化）。
 * WiFi STA + BLE 共存（CONFIG_ESP32_WIFI_SW_COEXIST_ENABLE=y）。
 */

#pragma once

#include <esp_err.h>
#include <esp_event.h>
#include <string>
#include <functional>

namespace memoria {
namespace drivers {

using WifiEventCb = std::function<void(bool connected, const std::string& ssid, int rssi)>;

class WifiManager {
public:
    static WifiManager* instance();
    esp_err_t init();

    /* 手动连接（会保存到 NVS） */
    esp_err_t connect(const std::string& ssid, const std::string& password);
    void      disconnect();

    /* 用 NVS 保存的凭据重连（夜间自动更新用，无凭据返回 ESP_ERR_NOT_FOUND） */
    esp_err_t reconnect();

    /* 已保存的 SSID（用于设置 App 显示） */
    std::string saved_ssid() const;

    /* 状态 */
    bool connected()    const { return _connected; }
    int  rssi()         const { return _rssi; }
    const std::string& current_ssid() const { return _current_ssid; }

    void set_event_cb(WifiEventCb cb) { _cb = std::move(cb); }

private:
    WifiManager() = default;

    static void _wifi_event_handler(void* arg, esp_event_base_t eb,
                                    int32_t ev, void* data);
    static void _ip_event_handler(void* arg, esp_event_base_t eb,
                                  int32_t ev, void* data);

    bool        _connected = false;
    int         _rssi = 0;
    std::string _current_ssid;
    WifiEventCb _cb;
};

} // namespace drivers
} // namespace memoria
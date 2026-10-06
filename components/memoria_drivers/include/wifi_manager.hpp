/**
 * @file wifi_manager.hpp
 * @brief WiFi STA 模式管理：连接 / NVS 存密码 / 断开事件回调
 *
 * 按需射频：boot 期零射频——esp_wifi_init/start 全部在 start_rf()，
 * 由网络需求侧（WIFISTAT() / wifi 语句 / 设置页）触发，NVS 凭据自动重连。
 * WiFi STA + BLE 共存（CONFIG_ESP32_WIFI_SW_COEXIST_ENABLE=y）。
 */

#pragma once

#include <esp_err.h>
#include <esp_event.h>
#include <string>
#include <array>
#include <functional>

namespace memoria {
namespace drivers {

using WifiEventCb = std::function<void(bool connected, const std::string& ssid, int rssi)>;

/* 扫描结果条目（自包含类型，hpp 不依赖 esp_wifi.h） */
struct ScanAp {
    char    ssid[33] = {};
    int8_t  rssi     = 0;
    uint8_t authmode = 0;   /* WIFI_AUTH_*：0=OPEN */
    bool    saved    = false;  /* NVS 已存该 SSID 密码 */
};

class WifiManager {
public:
    static WifiManager* instance();
    esp_err_t init();          /* 轻注册：netif/事件回调（不含射频） */

    /* 按需射频（幂等）：esp_wifi_init + start + NVS 凭据自动重连 */
    bool      rf_started() const { return _rf_started; }
    esp_err_t start_rf();

    /* 手动连接（会保存到 NVS） */
    esp_err_t connect(const std::string& ssid, const std::string& password);
    void      disconnect();

    /* 用 NVS 保存的凭据重连（夜间自动更新用，无凭据返回 ESP_ERR_NOT_FOUND） */
    esp_err_t reconnect();

    /* 已保存的 SSID / 密码（用于设置界面显示与重连） */
    std::string saved_ssid() const;
    std::string saved_pass() const;

    /* 是否 NVS 已存该 SSID 的凭据 */
    bool has_saved(const char* ssid) const;

    /* 扫描（阻塞，完成后结果经 scan_count / scan_at 查询） */
    esp_err_t scan();
    size_t    scan_count() const { return _scan_count; }
    const ScanAp* scan_at(size_t i) const { return i < _scan_count ? &_aps[i] : nullptr; }

    /* AP 热点：配置持久化 NVS（ap_ssid / ap_pass），启动/停止 */
    esp_err_t start_ap(const std::string& ssid, const std::string& password);
    void      stop_ap();
    void      set_ap_config(const std::string& ssid, const std::string& password); /* 仅保存，不切换模式 */
    bool      ap_active() const { return _ap_active; }
    std::string ap_ssid() const;
    std::string ap_pass() const;

    /* 状态 */
    bool connected()    const { return _connected; }
    int  rssi()         const { return _rssi; }
    const std::string& current_ssid() const { return _current_ssid; }
    const std::string& ip() const { return _ip; }

    void set_event_cb(WifiEventCb cb) { _cb = std::move(cb); }

private:
    WifiManager() = default;

    static void _wifi_event_handler(void* arg, esp_event_base_t eb,
                                    int32_t ev, void* data);
    static void _ip_event_handler(void* arg, esp_event_base_t eb,
                                  int32_t ev, void* data);

    bool        _connected = false;
    bool        _rf_started = false;
    int         _rssi = 0;
    bool        _ap_active = false;
    std::string _current_ssid;
    std::string _ip;
    WifiEventCb _cb;

    std::array<ScanAp, 24> _aps;
    size_t _scan_count = 0;
};

} // namespace drivers
} // namespace memoria
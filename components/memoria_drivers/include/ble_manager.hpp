/**
 * @file ble_manager.hpp
 * @brief NimBLE BLE 外设：Nordic UART Service（串口透传）
 *
 * 修复：静态函数声明全部移到 .cpp，hpp 里不引用任何 NimBLE C 类型。
 */

#pragma once

#include <esp_err.h>

struct ble_gap_event;
struct ble_gatt_access_ctxt;
#include <cstdint>
#include <string>
#include <functional>

namespace memoria {
namespace drivers {

using BleRxCb = std::function<void(const uint8_t* data, uint16_t len)>;

class BleManager {
public:
    static BleManager* instance();
    esp_err_t init();

    esp_err_t send(const uint8_t* data, uint16_t len);
    esp_err_t send(const std::string& s);

    bool connected() const { return _connected; }

    void set_rx_cb(BleRxCb cb) { _rx_cb = std::move(cb); }

private:
    BleManager() = default;

    static void _ble_task_c(void* arg);
    static void _sync_cb();
    static void _reset_cb(int reason);
    static int _on_gap_event(ble_gap_event* ev, void* arg);
    static int _on_gatt_write(uint16_t conn_handle, uint16_t attr_handle,
                              ble_gatt_access_ctxt* ctxt, void* arg);

    bool       _connected = false;
    uint16_t   _conn_handle = 0;
    BleRxCb    _rx_cb;
};

} // namespace drivers
} // namespace memoria
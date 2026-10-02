/**
 * @file ble_manager.cpp
 * @brief NimBLE BLE 外设：串口透传 Service
 *
 * NimBLE 头文件路径为 host/ble_*.h（ESP-IDF 内置 NimBLE 组件），
 * 回调函数签名与官方 NimBLE 一致。
 */

#include "ble_manager.hpp"

extern "C" {
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <nimble/ble.h>
#include <host/ble_hs.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <services/gap/ble_svc_gap.h>
#include <host/ble_uuid.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
}

#include <cstring>

namespace memoria {
namespace drivers {

static const char* TAG = "BLE";

/* Nordic UART Service UUID */
static const ble_uuid128_t NUS_SVC_UUID = {
    .u = { .type = BLE_UUID_TYPE_128 },
    .value = {0x9E,0xCA,0xDD,0x24,0x0E,0xE5,0xE0,0xA9,
              0xF3,0x93,0xB5,0xA3,0x01,0x00,0x40,0x6E}   /* 0x6E400001.. */
};
static const ble_uuid128_t NUS_TX_UUID = {
    .u = { .type = BLE_UUID_TYPE_128 },
    .value = {0x9E,0xCA,0xDD,0x24,0x0E,0xE5,0xE0,0xA9,
              0xF3,0x93,0xB5,0xA3,0x03,0x00,0x40,0x6E}   /* 0x6E400003.. */
};
static const ble_uuid128_t NUS_RX_UUID = {
    .u = { .type = BLE_UUID_TYPE_128 },
    .value = {0x9E,0xCA,0xDD,0x24,0x0E,0xE5,0xE0,0xA9,
              0xF3,0x93,0xB5,0xA3,0x02,0x00,0x40,0x6E}   /* 0x6E400002.. */
};

static uint16_t s_tx_handle = 0;

BleManager* BleManager::instance() {
    static BleManager inst;
    return &inst;
}

esp_err_t BleManager::init() {
    /* NimBLE 主机初始化 */
    nimble_port_init();

    /* 设置设备名 */
    ble_hs_cfg.reset_cb = _reset_cb;
    /* v6 NimBLE：GAP 事件回调由 ble_gap_adv_start 传入，ble_hs_cfg 不再有 gap_event_cb */

    /* 注册 GATT Service：TX(Notify) + RX(Write) */
    static const struct ble_gatt_svc_def gatt_svcs[] = {
        {
            .type = BLE_GATT_SVC_TYPE_PRIMARY,
            .uuid = &NUS_SVC_UUID.u,
            .includes = NULL,
            .characteristics = (struct ble_gatt_chr_def[]){
                {
                    .uuid = &NUS_TX_UUID.u,
                    .access_cb = nullptr,
                    .arg = nullptr,
                    .descriptors = NULL,
                    .flags = BLE_GATT_CHR_F_NOTIFY,
                    .min_key_size = 0,
                    .val_handle = &s_tx_handle,
                    .cpfd = NULL,
                },
                {
                    .uuid = &NUS_RX_UUID.u,
                    .access_cb = &_on_gatt_write,
                    .arg = nullptr,
                    .descriptors = NULL,
                    .flags = BLE_GATT_CHR_F_WRITE,
                    .min_key_size = 0,
                    .val_handle = nullptr,
                    .cpfd = NULL,
                },
                {
                    .uuid = nullptr,
                    .access_cb = nullptr,
                    .arg = nullptr,
                    .descriptors = NULL,
                    .flags = 0,
                    .min_key_size = 0,
                    .val_handle = nullptr,
                    .cpfd = NULL,
                },
            },
        },
        {
            .type = 0,
            .uuid = nullptr,
            .includes = NULL,
            .characteristics = nullptr,
        },
    };

    int rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc != 0) { ESP_LOGE(TAG, "gatts count cfg: %d", rc); return ESP_FAIL; }
    rc = ble_gatts_add_svcs(gatt_svcs);
    if (rc != 0) { ESP_LOGE(TAG, "gatts add: %d", rc); return ESP_FAIL; }

    /* 启动 NimBLE 主机线程 */
    nimble_port_freertos_init(_ble_task_c);

    ESP_LOGI(TAG, "BLE NimBLE init OK");
    return ESP_OK;
}

void BleManager::_sync_cb() {
    /* host 与控制器同步完成回调：此时广播才保证生效。
     * 原实现在 host task 里直接 adv_start——未同步时静默失败，BLE 永远不可见 */
    struct ble_gap_adv_params adv = {};
    adv.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv.disc_mode = BLE_GAP_DISC_MODE_GEN;

    struct ble_hs_adv_fields fields = {};
    const char* name = "Memoria-OS";
    fields.name = (uint8_t*)name;
    fields.name_len = std::strlen(name);
    fields.name_is_complete = 1;
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) { ESP_LOGE(TAG, "adv set fields: %d", rc); return; }
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, nullptr, BLE_HS_FOREVER, &adv,
                           _on_gap_event, nullptr);
    if (rc != 0) { ESP_LOGE(TAG, "adv start: %d", rc); return; }
    ESP_LOGI(TAG, "BLE advertising started");
}

void BleManager::_ble_task_c(void* arg) {
    (void)arg;
    /* host task 标准结构：注册同步回调 + 设备名，进入 host 事件主循环（阻塞）
     * 原实现缺少 nimble_port_run()——task 提前返回且 host 无事件循环 */
    ble_hs_cfg.sync_cb = _sync_cb;
    ble_svc_gap_device_name_set("Memoria-OS");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

int BleManager::_on_gap_event(ble_gap_event* ev, void* arg) {
    (void)arg;
    static BleManager* self = BleManager::instance();
    switch (ev->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (ev->connect.status == 0) {
                self->_connected = true;
                self->_conn_handle = ev->connect.conn_handle;
                ESP_LOGI(TAG, "BLE connected");
            } else {
                ESP_LOGW(TAG, "BLE connect fail: %d", ev->connect.status);
                /* 重新开始广播 */
                struct ble_gap_adv_params adv = {};
                adv.conn_mode = BLE_GAP_CONN_MODE_UND;
                adv.disc_mode = BLE_GAP_DISC_MODE_GEN;
                ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, nullptr, BLE_HS_FOREVER, &adv,
                                  _on_gap_event, nullptr);
            }
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            self->_connected = false;
            ESP_LOGI(TAG, "BLE disconnected");
            /* 重新广播 */
            {
                struct ble_gap_adv_params adv = {};
                adv.conn_mode = BLE_GAP_CONN_MODE_UND;
                adv.disc_mode = BLE_GAP_DISC_MODE_GEN;
                ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, nullptr, BLE_HS_FOREVER, &adv,
                                  _on_gap_event, nullptr);
            }
            break;
        default: break;
    }
    return 0;
}

int BleManager::_on_gatt_write(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt* ctxt, void* arg) {
    (void)conn_handle; (void)attr_handle; (void)arg;
    static BleManager* self = BleManager::instance();
    if (self->_rx_cb) self->_rx_cb(ctxt->om->om_data, ctxt->om->om_len);
    return 0;
}

void BleManager::_reset_cb(int reason) {
    ESP_LOGW(TAG, "NimBLE reset: reason=0x%02x", reason);
}

esp_err_t BleManager::send(const uint8_t* data, uint16_t len) {
    if (!_connected || len == 0) return ESP_ERR_INVALID_STATE;
    struct os_mbuf* om = ble_hs_mbuf_from_flat(data, len);
    if (!om) return ESP_ERR_NO_MEM;
    return ble_gatts_notify_custom(_conn_handle, s_tx_handle, om);
}

esp_err_t BleManager::send(const std::string& s) {
    return send(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

} // namespace drivers
} // namespace memoria
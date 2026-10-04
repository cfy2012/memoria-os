/**
 * @file wifi_manager.cpp
 * @brief WiFi STA：esp_wifi + esp_event + NVS 存凭据
 */

#include "wifi_manager.hpp"
#include "rtc_clock.hpp"

extern "C" {
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
}

#include <cstring>

#include <cstdlib>

#include <algorithm>

namespace memoria {
namespace drivers {

static const char* TAG = "WIFI";
static const char* NVS_NS = "wifi";
static const char* KEY_SSID = "ssid";
static const char* KEY_PASS = "pass";

WifiManager* WifiManager::instance() {
    static WifiManager inst;
    return &inst;
}

esp_err_t WifiManager::init() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                    &_wifi_event_handler, this, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                    &_ip_event_handler, this, nullptr));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    /* 尝试读取 NVS 保存的凭据并自动连接 */
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        char ssid[33] = {}, pass[65] = {};
        size_t l1 = sizeof(ssid), l2 = sizeof(pass);
        if (nvs_get_str(h, KEY_SSID, ssid, &l1) == ESP_OK && ssid[0] != 0) {
            nvs_get_str(h, KEY_PASS, pass, &l2);
            ESP_LOGI(TAG, "auto-connect saved SSID: %s", ssid);
            /* 不能直接在 init() 里调用 connect()（会循环初始化），改用启动后 xTaskCreate 延迟 */
            static char s_ssid[33] = {}, s_pass[65] = {};
            std::strncpy(s_ssid, ssid, 32);
            std::strncpy(s_pass, pass, 64);
            BaseType_t ok = xTaskCreate([](void*) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                WifiManager::instance()->connect(s_ssid, s_pass);
            }, "wifi_auto", 4096, nullptr, 3, nullptr);
            (void)ok;
        }
        nvs_close(h);
    }

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi STA init OK");
    return ESP_OK;
}

esp_err_t WifiManager::connect(const std::string& ssid, const std::string& password) {
    wifi_config_t cfg = {};
    std::strncpy((char*)cfg.sta.ssid, ssid.c_str(), 32);
    std::strncpy((char*)cfg.sta.password, password.c_str(), 64);
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK) return ret;

    /* 保存到 NVS */
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, KEY_SSID, ssid.c_str());
        nvs_set_str(h, KEY_PASS, password.c_str());
        nvs_commit(h);
        nvs_close(h);
    }
    _current_ssid = ssid;
    return ESP_OK;
}

void WifiManager::disconnect() {
    esp_wifi_disconnect();
    _connected = false;
    if (_cb) _cb(false, _current_ssid, _rssi);
}

esp_err_t WifiManager::reconnect() {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        char ssid[33] = {}, pass[65] = {};
        size_t l1 = sizeof(ssid), l2 = sizeof(pass);
        if (nvs_get_str(h, KEY_SSID, ssid, &l1) == ESP_OK && ssid[0] != 0) {
            nvs_get_str(h, KEY_PASS, pass, &l2);
            nvs_close(h);
            ESP_LOGI(TAG, "reconnect saved SSID: %s", ssid);
            return connect(ssid, pass);   /* connect 会回写 NVS，幂等无害 */
        }
        nvs_close(h);
    }
    return ESP_ERR_NOT_FOUND;
}

std::string WifiManager::saved_ssid() const {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        char ssid[33] = {}; size_t l = sizeof(ssid);
        if (nvs_get_str(h, KEY_SSID, ssid, &l) == ESP_OK) { nvs_close(h); return ssid; }
        nvs_close(h);
    }
    return {};
}

std::string WifiManager::saved_pass() const {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        char pass[65] = {}; size_t l = sizeof(pass);
        if (nvs_get_str(h, KEY_PASS, pass, &l) == ESP_OK) { nvs_close(h); return pass; }
        nvs_close(h);
    }
    return {};
}

bool WifiManager::has_saved(const char* ssid) const {
    if (!ssid || !ssid[0]) return false;
    return saved_ssid() == ssid;
}

esp_err_t WifiManager::scan() {
    wifi_scan_config_t sc = {};
    sc.show_hidden = false;
    esp_err_t ret = esp_wifi_scan_start(&sc, true);   /* 阻塞扫描 */
    if (ret != ESP_OK) return ret;

    uint16_t n = 0;
    ret = esp_wifi_scan_get_ap_records(&n, nullptr);
    if (ret != ESP_OK || n == 0) { esp_wifi_scan_get_ap_records(&n, nullptr); _scan_count = 0; return ret; }

    wifi_ap_record_t* recs = static_cast<wifi_ap_record_t*>(std::calloc(n, sizeof(wifi_ap_record_t)));
    if (!recs) { esp_wifi_scan_get_ap_records(&n, nullptr); return ESP_ERR_NO_MEM; }
    ret = esp_wifi_scan_get_ap_records(&n, recs);
    if (ret != ESP_OK) { std::free(recs); esp_wifi_scan_get_ap_records(&n, nullptr); return ret; }

    _scan_count = std::min<size_t>(n, _aps.size());
    for (size_t i = 0; i < _scan_count; i++) {
        std::strncpy(_aps[i].ssid, reinterpret_cast<char*>(recs[i].ssid), 32);
        _aps[i].ssid[32] = 0;
        _aps[i].rssi = recs[i].rssi;
        _aps[i].authmode = recs[i].authmode;
        _aps[i].saved = has_saved(_aps[i].ssid);
    }
    std::free(recs);
    esp_wifi_scan_get_ap_records(&n, nullptr);   /* 释放内核扫描缓存 */
    return ESP_OK;
}

esp_err_t WifiManager::start_ap(const std::string& ssid, const std::string& password) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "ap_ssid", ssid.c_str());
        nvs_set_str(h, "ap_pass", password.c_str());
        nvs_commit(h);
        nvs_close(h);
    }

    wifi_config_t cfg = {};
    std::strncpy(reinterpret_cast<char*>(cfg.ap.ssid), ssid.c_str(), 32);
    cfg.ap.ssid_len = static_cast<uint8_t>(std::min<size_t>(ssid.size(), 32));
    if (!password.empty())
        std::strncpy(reinterpret_cast<char*>(cfg.ap.password), password.c_str(), 64);
    cfg.ap.max_connection = 4;
    cfg.ap.authmode = password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    esp_err_t ret = esp_wifi_start();
    if (ret == ESP_OK) _ap_active = true;
    return ret;
}

void WifiManager::set_ap_config(const std::string& ssid, const std::string& password) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "ap_ssid", ssid.c_str());
        nvs_set_str(h, "ap_pass", password.c_str());
        nvs_commit(h);
        nvs_close(h);
    }
}

void WifiManager::stop_ap() {
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    _ap_active = false;
    reconnect();   /* 尝试回连已存网络 */
}

std::string WifiManager::ap_ssid() const {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        char ssid[33] = {}; size_t l = sizeof(ssid);
        if (nvs_get_str(h, "ap_ssid", ssid, &l) == ESP_OK && ssid[0]) { nvs_close(h); return ssid; }
        nvs_close(h);
    }
    return "Memoria-OS";
}

std::string WifiManager::ap_pass() const {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        char pass[65] = {}; size_t l = sizeof(pass);
        if (nvs_get_str(h, "ap_pass", pass, &l) == ESP_OK) { nvs_close(h); return pass; }
        nvs_close(h);
    }
    return {};
}

void WifiManager::_wifi_event_handler(void* arg, esp_event_base_t eb, int32_t ev, void* data) {
    auto* self = static_cast<WifiManager*>(arg);
    if (eb == WIFI_EVENT && ev == WIFI_EVENT_STA_DISCONNECTED) {
        auto* d = static_cast<wifi_event_sta_disconnected_t*>(data);
        ESP_LOGW(TAG, "disconnected: reason=%d", d ? d->reason : -1);
        self->_connected = false;
        self->_ip.clear();
        if (self->_cb) self->_cb(false, self->_current_ssid, self->_rssi);
    }
}

void WifiManager::_ip_event_handler(void* arg, esp_event_base_t eb, int32_t ev, void* data) {
    if (eb != IP_EVENT || ev != IP_EVENT_STA_GOT_IP) return;
    auto* self = static_cast<WifiManager*>(arg);
    auto* d = static_cast<ip_event_got_ip_t*>(data);
    self->_connected = true;

    /* 读 RSSI */
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) self->_rssi = ap.rssi;

    char ipbuf[16];
    esp_ip4addr_ntoa(&d->ip_info.ip, ipbuf, sizeof(ipbuf));
    self->_ip = ipbuf;
    ESP_LOGI(TAG, "connected! ip=%s", ipbuf);
    if (self->_cb) self->_cb(true, self->_current_ssid, self->_rssi);

    /* NTP 时间同步 */
    static bool ntp_started = false;
    if (!ntp_started) {
        ntp_started = true;
        xTaskCreate([](void*) {
            vTaskDelay(pdMS_TO_TICKS(500));
            drivers::RtcClock::instance()->ntp_sync();
        }, "ntp", 2048, nullptr, 2, nullptr);
    }
}

} // namespace drivers
} // namespace memoria
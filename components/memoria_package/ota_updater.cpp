/**
 * @file ota_updater.cpp
 * @brief ESP32-S3 固件 OTA
 *
 * 真实 esp_https_ota API：
 *   1. esp_https_ota_config_t cfg = {};
 *   2. esp_https_ota_begin(&cfg);
 *   3. esp_https_ota_perform_image_write();  (分块写 OTA 分区)
 *   4. esp_https_ota_finish();
 *
 * 固件 URL 存 NVS key=ota_url，默认 GitHub releases raw。
 */

#include "ota_updater.hpp"

extern "C" {
#include <esp_https_ota.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
}

#include <string>
#include <cstdio>

namespace memoria {
namespace package {

static const char* TAG = "PKG_OTA";
static const char* NVS_NS = "ota";
static const char* KEY_URL = "firmware_url";

std::string get_ota_url() {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        char buf[512] = {}; size_t l = sizeof(buf);
        if (nvs_get_str(h, KEY_URL, buf, &l) == ESP_OK && buf[0] != 0) {
            nvs_close(h); return buf;
        }
        nvs_close(h);
    }
    /* 默认：GitHub releases raw */
    return "https://github.com/memoria-os/memoria-os/releases/latest/download/memoria-os.bin";
}

bool set_ota_url(const std::string& url) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    nvs_set_str(h, KEY_URL, url.c_str()); nvs_commit(h); nvs_close(h);
    return true;
}

int ota_firmware(const std::string& url) {
    std::string u = url.empty() ? get_ota_url() : url;
    ESP_LOGI(TAG, "OTA firmware from: %s", u.c_str());

    /* http 客户端配置必须带 url（空配置 esp_https_ota_begin 会直接失败） */
    esp_http_client_config_t http_cfg = {};
    http_cfg.url = u.c_str();
    http_cfg.timeout_ms = 20000;              /* 单次读写超时 */
    http_cfg.keep_alive_enable = true;

    esp_https_ota_config_t cfg = {};
    cfg.http_config = &http_cfg;
    esp_https_ota_handle_t handle = nullptr;

    esp_err_t ret = esp_https_ota_begin(&cfg, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ota begin failed: %s", esp_err_to_name(ret));
        return 1;
    }

    ESP_LOGI(TAG, "OTA in progress...");
    while ((ret = esp_https_ota_perform(handle)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ota perform failed: %s", esp_err_to_name(ret));
        esp_https_ota_abort(handle);
        return 3;
    }

    ret = esp_https_ota_finish(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ota finish failed: %s", esp_err_to_name(ret));
        return 2;
    }

    ESP_LOGI(TAG, "OTA OK. rebooting...");
    /* 延迟 500ms 让日志刷出去再重启 */
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return 0;
}

} }
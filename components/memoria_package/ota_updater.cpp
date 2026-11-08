/**
 * @file ota_updater.cpp
 * @brief ESP32-S3 固件 OTA（HTTPS + 身份校验）
 *
 * 流程：
 *   1. esp_https_ota_begin（必须配 CA，否则 IDF 直接拒绝）
 *   2. esp_https_ota_perform 分块写另一槽
 *   3. esp_https_ota_finish（内部 esp_ota_end 验镜像 + 切启动槽）
 *   4. 读新槽 app_desc，比对项目名与版本；不符则把启动槽切回来
 *
 * 固件 URL 存 NVS key=firmware_url，默认 GitHub releases raw。
 */

#include "ota_updater.hpp"

extern "C" {
#include <esp_https_ota.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <esp_ota_ops.h>
#include <esp_app_desc.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
}

#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "system_info.h"

namespace memoria {
namespace package {

static const char* TAG = "PKG_OTA";
static const char* NVS_NS = "ota";
static const char* KEY_URL = "firmware_url";
static const char* PROJECT = "memoria_os";

/* "x.y.z" 三点比较：a>b 1，相等 0，a<b -1 */
static int ver_cmp(const char* a, const char* b) {
    const char* pa = a;
    const char* pb = b;
    for (int i = 0; i < 3; i++) {
        long x = std::strtol(pa, (char**)&pa, 10);
        long y = std::strtol(pb, (char**)&pb, 10);
        if (x != y) return x > y ? 1 : -1;
        if (*pa == '.') pa++;
        if (*pb == '.') pb++;
    }
    return 0;
}

/* 校验目标槽里的镜像是不是 Memoria OS 的固件，且版本不低于当前 */
static bool identity_ok(const esp_partition_t* part) {
    if (part == nullptr) {
        ESP_LOGE(TAG, "identity: no partition");
        return false;
    }
    esp_app_desc_t desc = {};
    if (esp_ota_get_partition_description(part, &desc) != ESP_OK) {
        ESP_LOGE(TAG, "identity: cannot read app descriptor from %s", part->label);
        return false;
    }
    ESP_LOGI(TAG, "identity: project=%s version=%s (%s)",
             desc.project_name, desc.version, part->label);

    if (std::strcmp(desc.project_name, PROJECT) != 0) {
        ESP_LOGE(TAG, "identity: project mismatch, expect \"%s\"", PROJECT);
        return false;
    }
    if (ver_cmp(desc.version, MEMORIA_OS_VERSION) < 0) {
        ESP_LOGE(TAG, "identity: version %s older than running %s",
                 desc.version, MEMORIA_OS_VERSION);
        return false;
    }
    return true;
}

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

/* TLS 证书有效期校验依赖系统时间；开机未同步 NTP 时 RTC 从 1970 起 */
static bool clock_ready() {
    time_t now = 0;
    time(&now);
    struct tm t = {};
    localtime_r(&now, &t);
    return (t.tm_year + 1900) >= 2020;
}

int ota_firmware(const std::string& url) {
    std::string u = url.empty() ? get_ota_url() : url;
    ESP_LOGI(TAG, "OTA firmware from: %s", u.c_str());

    /* 没证书校验会被 IDF 直接拒；有了证书校验又要求时间正确，两头都得管 */
    if (!clock_ready()) {
        ESP_LOGW(TAG, "system time not synced yet, waiting for SNTP...");
        for (int i = 0; i < 10 && !clock_ready(); i++) vTaskDelay(pdMS_TO_TICKS(1000));
        if (!clock_ready()) {
            ESP_LOGE(TAG, "clock still before 2020, HTTPS cert would fail");
            return 5;
        }
    }

    /* http 客户端配置：url 必填；crt_bundle_attach 必填，
     * 否则 esp_https_ota_begin 会因"无服务器校验手段"直接返回 INVALID_ARG */
    esp_http_client_config_t http_cfg = {};
    http_cfg.url = u.c_str();
    http_cfg.timeout_ms = 20000;
    http_cfg.keep_alive_enable = true;
    http_cfg.crt_bundle_attach = esp_crt_bundle_attach;

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

    /* finish 已把启动槽切到新镜像，但还没重启：在这里认一遍身份 */
    const esp_partition_t* next = esp_ota_get_boot_partition();
    const esp_partition_t* run  = esp_ota_get_running_partition();
    if (!identity_ok(next)) {
        ESP_LOGE(TAG, "OTA rejected, keep booting %s",
                 run ? run->label : "(unknown)");
        if (run) esp_ota_set_boot_partition(run);
        esp_ota_mark_app_valid_cancel_rollback();
        return 4;
    }

    ESP_LOGI(TAG, "OTA OK. rebooting...");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return 0;
}

} }

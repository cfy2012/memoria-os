/**
 * @file ota_updater.cpp
 * @brief ESP32-S3 固件 OTA（HTTPS + sha256 + 身份校验）
 *
 * 为什么不用 esp_https_ota：它把下载、写槽、end、切槽一把梭，中间拿不到字节流，
 * 算不了 sha256。这里自己开 http client 读流：
 *   1. 边读边写另一槽（esp_ota_write），同时喂 mbedtls_sha256
 *   2. 收完比对清单里的 sha256（不符 → abort，一个字节都不进校验阶段）
 *   3. esp_ota_end 验镜像结构 → set_boot_partition
 *   4. 读新槽 app_desc 比项目名/版本，不符切回原槽
 *
 * 断在半截是最常见的事故：Content-Length 对不上、sha256 对不上，都在写 otadata 之前拦下。
 */

#include "ota_updater.hpp"
#include "version_util.hpp"

extern "C" {
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <esp_ota_ops.h>
#include <esp_app_desc.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <psa/crypto.h>
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

static const size_t CHUNK = 4096;

static bool is_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static char as_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

/* 摘要与清单里的 sha256 比对：忽略大小写，允许 "aa:bb:" 或 "-" 这类分隔符 */
static bool digest_matches(const unsigned char* d, const std::string& expect) {
    if (expect.size() < 64) return false;
    int k = 0;
    for (int i = 0; i < 32; i++) {
        char hex[4];
        std::snprintf(hex, sizeof(hex), "%02x", d[i]);
        while (k < static_cast<int>(expect.size()) && !is_hex(expect[k])) k++;
        if (k + 1 >= static_cast<int>(expect.size())) return false;
        if (as_lower(expect[k]) != hex[0] || as_lower(expect[k + 1]) != hex[1]) return false;
        k += 2;
    }
    return true;
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
    if (version_cmp(desc.version, MEMORIA_OS_VERSION) < 0) {
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

int ota_firmware(const std::string& url, const std::string& sha256) {
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

    if (psa_crypto_init() != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa crypto init failed");
        return 3;
    }

    esp_http_client_config_t http_cfg = {};
    http_cfg.url = u.c_str();
    http_cfg.timeout_ms = 20000;
    http_cfg.keep_alive_enable = true;
    http_cfg.crt_bundle_attach = esp_crt_bundle_attach;
    http_cfg.buffer_size = static_cast<int>(CHUNK);

    esp_http_client_handle_t cli = esp_http_client_init(&http_cfg);
    if (cli == nullptr) {
        ESP_LOGE(TAG, "http client init failed");
        return 1;
    }
    esp_err_t ret = esp_http_client_open(cli, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "http open failed: %s", esp_err_to_name(ret));
        esp_http_client_cleanup(cli);
        return 1;
    }
    int64_t len = esp_http_client_fetch_headers(cli);
    int status = esp_http_client_get_status_code(cli);
    ESP_LOGI(TAG, "http %d, content-length %lld", status, static_cast<long long>(len));
    if (status != 200) {
        ESP_LOGE(TAG, "unexpected http status %d", status);
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        return 1;
    }

    const esp_partition_t* upd = esp_ota_get_next_update_partition(nullptr);
    if (upd == nullptr) {
        ESP_LOGE(TAG, "no update partition");
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        return 2;
    }
    esp_ota_handle_t ota = 0;
    ret = esp_ota_begin(upd, OTA_WITH_SEQUENTIAL_WRITES, &ota);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ota begin failed: %s", esp_err_to_name(ret));
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        return 2;
    }
    ESP_LOGW(TAG, "writing slot %s @0x%08x", upd->label, static_cast<unsigned>(upd->address));

    psa_hash_operation_t sha = PSA_HASH_OPERATION_INIT;
    psa_status_t ps = psa_hash_setup(&sha, PSA_ALG_SHA_256);
    if (ps != PSA_SUCCESS) {
        ESP_LOGE(TAG, "sha256 setup failed: %d", static_cast<int>(ps));
        esp_ota_abort(ota);
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        return 3;
    }

    unsigned char* buf = static_cast<unsigned char*>(std::malloc(CHUNK));
    int rc = 0;
    int64_t total = 0;
    if (buf == nullptr) {
        rc = 3;
    } else {
        while (true) {
            int r = esp_http_client_read(cli, reinterpret_cast<char*>(buf), static_cast<int>(CHUNK));
            if (r < 0) {
                ESP_LOGE(TAG, "socket read error %d", r);
                rc = 3;
                break;
            }
            if (r == 0) break;
            if (esp_ota_write(ota, buf, static_cast<size_t>(r)) != ESP_OK) {
                ESP_LOGE(TAG, "ota write failed at %lld", static_cast<long long>(total));
                rc = 3;
                break;
            }
            if (psa_hash_update(&sha, buf, static_cast<size_t>(r)) != PSA_SUCCESS) {
                ESP_LOGE(TAG, "sha256 update failed");
                rc = 3;
                break;
            }
            total += r;
            if (static_cast<size_t>(total) > upd->size) {
                ESP_LOGE(TAG, "image exceeds partition, abort");
                rc = 3;
                break;
            }
        }
    }

    unsigned char digest[32] = {};
    size_t digest_len = 0;
    if (rc == 0 &&
        psa_hash_finish(&sha, digest, sizeof(digest), &digest_len) != PSA_SUCCESS) {
        ESP_LOGE(TAG, "sha256 finish failed");
        rc = 3;
    }
    if (rc == 0 && digest_len != sizeof(digest)) {
        ESP_LOGE(TAG, "sha256 digest length %u", static_cast<unsigned>(digest_len));
        rc = 3;
    }
    if (rc != 0) psa_hash_abort(&sha);
    std::free(buf);
    esp_http_client_close(cli);
    esp_http_client_cleanup(cli);

    if (rc != 0) {
        esp_ota_abort(ota);
        return rc;
    }
    if (total == 0) {
        ESP_LOGE(TAG, "empty body");
        esp_ota_abort(ota);
        return 3;
    }
    /* Content-Length 与实际字节数对不上 = 被截断，绝不继续 */
    if (len > 0 && total != len) {
        ESP_LOGE(TAG, "size mismatch: got %lld, expect %lld",
                 static_cast<long long>(total), static_cast<long long>(len));
        esp_ota_abort(ota);
        return 3;
    }
    ESP_LOGI(TAG, "downloaded %lld bytes", static_cast<long long>(total));

    if (sha256.empty()) {
        ESP_LOGW(TAG, "manifest has no sha256, integrity relies on image check");
    } else if (!digest_matches(digest, sha256)) {
        ESP_LOGE(TAG, "sha256 mismatch, drop image before verify");
        esp_ota_abort(ota);
        return 6;
    } else {
        ESP_LOGI(TAG, "sha256 OK");
    }

    ret = esp_ota_end(ota);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "image verify failed: %s", esp_err_to_name(ret));
        return 2;
    }
    ret = esp_ota_set_boot_partition(upd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "set boot partition failed: %s", esp_err_to_name(ret));
        return 2;
    }

    /* 已把启动槽切到新镜像，但还没重启：在这里认一遍身份 */
    const esp_partition_t* run = esp_ota_get_running_partition();
    if (!identity_ok(upd)) {
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

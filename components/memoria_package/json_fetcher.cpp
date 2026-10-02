/**
 * @file json_fetcher.cpp
 * @brief HTTP/HTTPS JSON 清单拉取
 *
 * 真实 esp_http_client API：
 *   1. esp_http_client_init() → 设置 URL / headers
 *   2. esp_http_client_open() → 下载（支持断点续传 resume-header）
 *   3. esp_http_client_fetch_headers() → 拿 Content-Length
 *   4. esp_http_client_read() → 循环读 buffer
 *   5. esp_http_client_cleanup()
 *
 * 输出：std::string（完整 JSON）
 * 支持：WiFi STA 必须已连上；HTTPS 走 esp_https_ota 背后的 TLS（默认 root CA 内嵌）
 */

#include "json_fetcher.hpp"

extern "C" {
#include <esp_http_client.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
}

#include <string>
#include <vector>
#include <cstdio>

namespace memoria {
namespace package {

static const char* TAG = "PKG_JSON";

/* HTTP 事件日志（简化） */
static esp_err_t _http_event_handler(esp_http_client_event_t* evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR: ESP_LOGW(TAG, "HTTP_EVENT_ERROR"); break;
        case HTTP_EVENT_ON_CONNECTED: break;
        case HTTP_EVENT_HEADER_SENT: break;
        case HTTP_EVENT_ON_HEADER: break;
        case HTTP_EVENT_ON_DATA: break;
        case HTTP_EVENT_ON_FINISH: break;
        case HTTP_EVENT_DISCONNECTED: break;
        default: break;
    }
    return ESP_OK;
}

std::string json_fetcher::fetch(const std::string& url, uint32_t resume_from) {
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 5000;
    cfg.event_handler = _http_event_handler;
    /* 恢复主机名校验：原 skip=true 时可被中间人注入 manifest 与安装包。
       若自配 mirror 证书 SAN 不全导致连接失败，可改用域名白名单方案。 */
    cfg.skip_cert_common_name_check = false;
    cfg.buffer_size = 4096;

    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) { ESP_LOGE(TAG, "http_client_init failed"); return ""; }

    /* 断点续传 Range header */
    if (resume_from > 0) {
        char hdr[32];
        std::snprintf(hdr, sizeof(hdr), "bytes=%lu-", (unsigned long)resume_from);
        esp_http_client_set_header(h, "Range", hdr);
    }

    esp_err_t ret = esp_http_client_open(h, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "open failed: %s", esp_err_to_name(ret));
        esp_http_client_cleanup(h);
        return "";
    }

    int status = esp_http_client_get_status_code(h);
    int len = esp_http_client_fetch_headers(h);
    ESP_LOGI(TAG, "HTTP %d len=%d resume=%lu", status, len, (unsigned long)resume_from);

    if (status != 200 && status != 206) {
        ESP_LOGE(TAG, "HTTP status %d", status);
        esp_http_client_cleanup(h);
        return "";
    }

    /* 读取全部到 std::string（JSON 一般 < 10KB；上限 4MB 防恶意服务器无限数据耗尽内存） */
    const size_t FETCH_MAX = 4 * 1024 * 1024;
    std::string buf;
    buf.reserve(len > 0 ? ((size_t)len < FETCH_MAX ? (size_t)len : FETCH_MAX) : 8192);
    std::vector<char> chunk(4096);
    while (true) {
        int n = esp_http_client_read(h, chunk.data(), chunk.size());
        if (n <= 0) break;
        if (buf.size() + (size_t)n > FETCH_MAX) {
            ESP_LOGE(TAG, "response too large (>4MB), abort");
            buf.clear();
            break;
        }
        buf.append(chunk.data(), n);
    }
    esp_http_client_cleanup(h);

    ESP_LOGI(TAG, "fetched %zu bytes from %s", buf.size(), url.c_str());
    return buf;
}

} }
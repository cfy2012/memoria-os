/**
 * @file firmware_update.cpp
 * @brief 固件更新检查与安装（版本清单 + OTA 触发）
 *
 * 清单来源：软件源镜像同目录下的 firmware/version.json：
 *   { "version": "1.1.0", "url": "https://…/memoria-os.bin",
 *     "major": false, "changelog": "修复 xxx" }
 * major=true 表示大版本（可能包含文件系统/数据布局变化）：
 * OTA 仍可刷固件，但若涉及存储结构变更需整包重刷（TF 卡数据不受影响）。
 */

#include "firmware_update.hpp"
#include "json_fetcher.hpp"
#include "ota_updater.hpp"
#include "package_manager.hpp"
#include "system_info.h"

extern "C" {
#include <esp_log.h>
}

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace memoria {
namespace package {

static const char* TAG = "PKG_FW";

std::string firmware_current_version() {
    return MEMORIA_OS_VERSION;
}

/* 简易 JSON 顶层字符串/布尔取值（version.json 结构固定，够用） */
static std::string json_find(const std::string& s, const char* key) {
    std::string k = "\"" + std::string(key) + "\"";
    size_t p = s.find(k);
    if (p == std::string::npos) return "";
    p += k.size();
    while (p < s.size() && (s[p] == ':' || s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r'))
        p++;
    if (p < s.size() && s[p] == '"') {
        size_t e = s.find('"', p + 1);
        if (e != std::string::npos) return s.substr(p + 1, e - p - 1);
    }
    if (p < s.size() && (s[p] == 't' || s[p] == 'f')) {
        size_t e = s.find_first_of(",}\n\r", p);
        if (e != std::string::npos) return s.substr(p, e - p);
    }
    return "";
}

/* "x.y.z" 三点版本比较：a>b 返回 1，相等 0，a<b 返回 -1 */
static int version_cmp(const std::string& a, const std::string& b) {
    auto part = [](const std::string& v, int idx) {
        long r = 0;
        size_t start = 0;
        for (int i = 0; i < idx; i++) {
            size_t dot = v.find('.', start);
            if (dot == std::string::npos) return r;
            start = dot + 1;
        }
        size_t end = v.find('.', start);
        if (end == std::string::npos) end = v.size();
        r = std::atol(v.substr(start, end - start).c_str());
        return r;
    };
    for (int i = 0; i < 3; i++) {
        long x = part(a, i), y = part(b, i);
        if (x != y) return x > y ? 1 : -1;
    }
    return 0;
}

static std::string firmware_manifest_url() {
    std::string u = get_mirror_url();
    size_t p = u.find_last_of('/');
    if (p != std::string::npos) u = u.substr(0, p);   /* 去掉 manifest.json */
    return u + "/firmware/version.json";
}

bool firmware_check(FirmwareInfo& out) {
    out = FirmwareInfo{};
    out.checked = true;
    out.latest  = firmware_current_version();

    std::string body = json_fetcher::fetch(firmware_manifest_url());
    if (body.empty()) {
        out.error = "清单拉取失败（WiFi/源不可达）";
        return false;
    }

    std::string latest = json_find(body, "version");
    if (latest.empty()) {
        out.error = "清单缺少 version 字段";
        return false;
    }
    out.latest    = latest;
    out.url       = json_find(body, "url");
    out.changelog = json_find(body, "changelog");
    out.major     = json_find(body, "major") == "true";

    int cmp = version_cmp(latest, firmware_current_version());
    if (cmp <= 0) {
        out.update_available = false;
        return true;   /* 已是最新 */
    }
    out.update_available = true;
    if (out.url.empty()) {
        out.error = "清单缺少固件 url";
        out.update_available = false;
        return false;
    }
    ESP_LOGI(TAG, "firmware update available: %s -> %s%s",
             firmware_current_version().c_str(), latest.c_str(),
             out.major ? " (major)" : "");
    return true;
}

bool firmware_apply(const std::string& url) {
    if (url.empty()) {
        ESP_LOGE(TAG, "firmware_apply: empty url");
        return false;
    }
    set_ota_url(url);
    /* ota_firmware 内部完成下载→校验→写 OTA 分区→重启，不返回（除非失败） */
    int rc = ota_firmware(url);
    ESP_LOGE(TAG, "firmware_apply failed rc=%d", rc);
    return rc == 0;
}

} // namespace package
} // namespace memoria

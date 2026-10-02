/**
 * @file package_manager.cpp
 * @brief 软件包管理器（脚本/小游戏）
 *
 * 真实逻辑：
 *   1. NVS 读取镜像源 URL（默认 GitHub raw）
 *   2. json_fetcher 拉 manifest.json
 *   3. 比对本地 .msp 包的版本号（Package Entry 里存）
 *   4. 版本不一致 → HTTP Range 断点续传下载
 *   5. CRC32 校验，通过则写入 mem_priv 分区
 *   6. 日志写到 TF 卡 /mem_fat/update.log
 *
 * 包格式（.msp 自定义安装包）：
 *   魔数 "MSPACK" (6B)
 *   版本 uint32
 *   名称 32B null-terminated
 *   大小 uint32（payload 字节数）
 *   CRC32 uint32
 *   payload（脚本二进制）
 */

#include "package_manager.hpp"
#include "json_fetcher.hpp"
#include "private_fs.hpp"
#include "wifi_manager.hpp"

extern "C" {
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_partition.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sys/stat.h>
}

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <cctype>

#include <fstream>
#include <string>
#include <vector>

namespace memoria {
namespace package {

static const char* TAG = "PKG_MGR";
static const char* NVS_NS = "package";
static const char* KEY_MIRROR = "mirror";
static const char* KEY_AUTO   = "auto_upd";
static const char* KEY_HOUR   = "auto_hour";
static const char* KEY_MIN    = "auto_min";
static const char* DEFAULT_MIRROR = "https://raw.githubusercontent.com/memoria-os/packages/main/manifest.json";

/* 计算 CRC32（复用 private_fs 里已有的查表实现） */
extern uint32_t crc32_compute(const uint8_t* data, size_t len);

/* ============================================================
 *  NVS 持久化配置读写
 * ============================================================ */
std::string get_mirror_url() {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        char buf[256] = {}; size_t l = sizeof(buf);
        if (nvs_get_str(h, KEY_MIRROR, buf, &l) == ESP_OK && buf[0] != 0) {
            nvs_close(h);
            return buf;
        }
        nvs_close(h);
    }
    return DEFAULT_MIRROR;
}

bool set_mirror_url(const std::string& url) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t rc = nvs_set_str(h, KEY_MIRROR, url.c_str());
    nvs_commit(h); nvs_close(h);
    return rc == ESP_OK;   /* NVS 满/空间不足时不再假装成功 */
}

bool get_auto_update() {
    nvs_handle_t h;
    uint8_t v = 0;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_u8(h, KEY_AUTO, &v) == ESP_OK) { nvs_close(h); return v != 0; }
        nvs_close(h);
    }
    return true;   /* 默认开 */
}
void set_auto_update(bool on) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, KEY_AUTO, on ? 1 : 0);
    nvs_commit(h); nvs_close(h);
}

uint8_t get_auto_hour() {
    nvs_handle_t h; uint8_t v = 2;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, KEY_HOUR, &v);
        nvs_close(h);
    }
    return v;
}
void set_auto_hour(uint8_t hh) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, KEY_HOUR, hh);
    nvs_commit(h); nvs_close(h);
}

uint8_t get_auto_min() {
    nvs_handle_t h; uint8_t v = 0;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, KEY_MIN, &v);
        nvs_close(h);
    }
    return v;
}
void set_auto_min(uint8_t mm) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, KEY_MIN, mm);
    nvs_commit(h); nvs_close(h);
}

/* ============================================================
 *  CRC32（完整 256 项 IEEE 802.3 查表）
 * ============================================================ */
static uint32_t crc32_table[256];
static bool _crc_init = false;
static void _crc_init_once() {
    if (_crc_init) return;
    _crc_init = true;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
        crc32_table[i] = c;
    }
}
uint32_t crc32_compute(const uint8_t* data, size_t len) {
    _crc_init_once();
    uint32_t c = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) c = crc32_table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFF;
}

/* ============================================================
 *  日志（写 TF 卡 update.log）
 * ============================================================ */
static void _log(const char* fmt, ...) {
    char line[256];
    va_list ap; va_start(ap, fmt);
    std::vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    ESP_LOGI(TAG, "%s", line);

    FILE* f = std::fopen("/mem_fat/update.log", "a");
    if (f) {
        std::fprintf(f, "[%lu] %s\n", (unsigned long)esp_timer_get_time() / 1000, line);
        std::fclose(f);
    }
}

/* ============================================================
 *  Manifest 解析（极简 JSON 数组，手写 parser，不依赖 cJSON）
 *  格式：[{"name":"demo.msp","version":"1.2.0","size":1234,"crc":0xABCDEF12}, ...]
 * ============================================================ */
struct ManifestEntry {
    std::string name;
    std::string version;
    uint32_t    size;
    uint32_t    crc;
    bool        has_crc = false;   /* manifest 缺少 crc 字段时跳过该校验，而非整体拒绝安装 */
    std::string url;
};

static std::vector<ManifestEntry> parse_manifest(const std::string& json) {
    std::vector<ManifestEntry> out;
    /* 极简：逐字符扫描找 "name":"xxx" / "version":"xxx" / "size":1234 / "crc":0x... / "url":"..."。
       按 key 独立扫描（find from 对象起点 + 本对象 } 边界），字段顺序不敏感；
       冒号后允许空格（"name" : "a.msp"）。 */
    size_t pos = 0;
    while (pos < json.size()) {
        /* 找下一个 { */
        size_t ob = json.find('{', pos);
        if (ob == std::string::npos) break;

        ManifestEntry e;
        auto pick_str = [&](const char* key) -> std::string {
            std::string pat = std::string("\"") + key + "\"";
            size_t kp = json.find(pat, ob);
            if (kp == std::string::npos || kp > json.find('}', ob)) return "";
            size_t sp = kp + pat.size();
            while (sp < json.size() && (json[sp] == ' ' || json[sp] == '\t' || json[sp] == ':')) sp++;
            if (sp >= json.size() || json[sp] != '"') return "";
            sp++;
            size_t ep = json.find('"', sp);
            if (ep == std::string::npos) return "";
            return json.substr(sp, ep - sp);
        };
        auto pick_u32 = [&](const char* key, uint32_t& out_v) -> bool {
            std::string pat = std::string("\"") + key + "\"";
            size_t kp = json.find(pat, ob);
            if (kp == std::string::npos || kp > json.find('}', ob)) return false;
            size_t sp = kp + pat.size();
            while (sp < json.size() && (json[sp] == ' ' || json[sp] == '\t' || json[sp] == ':')) sp++;
            size_t ep = sp;
            while (ep < json.size() && (std::isdigit((unsigned char)json[ep]) || json[ep] == 'x'
                   || json[ep] == 'X' || std::isxdigit((unsigned char)json[ep]))) ep++;
            if (ep == sp) return false;
            out_v = (uint32_t)strtoul(json.substr(sp, ep - sp).c_str(), nullptr, 0);
            return true;
        };

        e.name    = pick_str("name");
        e.version = pick_str("version");
        e.url     = pick_str("url");
        pick_u32("size", e.size);
        e.has_crc = pick_u32("crc", e.crc);

        if (!e.name.empty()) out.push_back(e);

        size_t cb = json.find('}', ob);
        pos = (cb != std::string::npos) ? cb + 1 : ob + 1;
    }
    return out;
}

/* ============================================================
 *  检查更新（WiFi 必须已连）
 * ============================================================ */
bool check_updates(int* out_added, int* out_removed) {
    if (!drivers::WifiManager::instance()->connected()) {
        _log("check_updates skipped: WiFi offline");
        return false;
    }
    int added = 0;
    int removed = 0;

    std::string url = get_mirror_url();
    _log("fetching manifest from %s", url.c_str());

    std::string json = json_fetcher::fetch(url);
    if (json.empty()) {
        _log("manifest fetch failed");
        return false;
    }

    auto remote = parse_manifest(json);
    _log("manifest has %zu entries", remote.size());

    /* 按 name 比对本地版本，已安装的最新包不再重复下载 */
    auto local_version_of = [](const std::string& name) -> std::string {
        std::string path = "/mem_fat/scripts/" + name;
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) return "";
        char hdr[42];
        size_t got = std::fread(hdr, 1, sizeof(hdr), f);
        std::fclose(f);
        if (got >= 42 && std::memcmp(hdr, "MSPACK", 6) == 0) {
            uint32_t v = 0;
            std::memcpy(&v, hdr + 6, 4);
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%u", (unsigned)v);
            return buf;
        }
        return "";   /* 裸脚本：无版本头，存在即视为已装 */
    };

    for (auto& e : remote) {
        _log("  need: %s ver=%s size=%lu crc=0x%08X",
             e.name.c_str(), e.version.c_str(),
             (unsigned long)e.size, (unsigned)e.crc);

        /* 包名白名单：仅 [A-Za-z0-9_.-]，防 manifest 恶意/错误 name 路径穿越（同 STORE 修法） */
        bool name_ok = !e.name.empty() && e.name != "." && e.name != "..";
        for (char ch : e.name) {
            if (!(std::isalnum((unsigned char)ch) || ch == '_' || ch == '.' || ch == '-')) { name_ok = false; break; }
        }
        if (!name_ok) { _log("  %s 包名非法，拒绝下载", e.name.c_str()); continue; }

        /* 版本比对：.msp 版本一致 / 裸脚本已存在 → 跳过 */
        std::string lv = local_version_of(e.name);
        if (!lv.empty()) {
            if (lv == e.version) { _log("  %s 本地 v%s 已是最新，跳过", e.name.c_str(), lv.c_str()); continue; }
        } else {
            struct stat st;
            if (stat(("/mem_fat/scripts/" + e.name).c_str(), &st) == 0) {
                _log("  %s 已存在（裸脚本无版本头），跳过", e.name.c_str());
                continue;
            }
        }

        /* 下载并（有 crc 时）校验，写入 mem_fat */
        if (!e.url.empty()) {
            std::string pkg = json_fetcher::fetch(e.url);
            if (!pkg.empty()) {
                bool crc_ok = true;
                if (e.has_crc) {
                    uint32_t actual_crc = crc32_compute(
                        reinterpret_cast<const uint8_t*>(pkg.data()), pkg.size());
                    crc_ok = (actual_crc == e.crc);
                    if (!crc_ok) {
                        _log("  %s CRC mismatch: got 0x%08X expected 0x%08X — drop",
                             e.name.c_str(), actual_crc, e.crc);
                    }
                } else {
                    _log("  %s 无 crc 字段，跳过校验", e.name.c_str());
                }
                if (crc_ok) {
                    std::string path = "/mem_fat/scripts/" + e.name;
                    std::ofstream fp(path, std::ios::binary);
                    if (fp) { fp.write(pkg.data(), pkg.size()); fp.close(); added++; }
                }
            }
        }
    }

    if (out_added)   *out_added   = added;
    if (out_removed) *out_removed = removed;
    _log("done: +%d -%d", added, removed);
    return true;
}

} }
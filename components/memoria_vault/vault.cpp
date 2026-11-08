/**
 * @file vault.cpp
 * @brief 用户数据保管实现
 *
 * 镜像格式（小端，nv.bin）：
 *   "MVLT" | u16 version=1 | u16 ns_count
 *   ns_count 个块：u8 name_len | name | u16 entry_count
 *     entry_count 个条目：u8 key_len | key | u8 type | u32 data_len | data
 *
 * 只镜像我方七个命名空间；IDF 自己写的（nvs.net80211 等）不碰，
 * 免得回灌时把 WiFi 驱动的内部状态搞乱。
 */

#include "vault.hpp"

extern "C" {
#include <nvs.h>
#include <nvs_flash.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <sys/stat.h>
#include <unistd.h>
}

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace memoria {
namespace vault {

static const char* TAG = "VAULT";

static const char* kNamespaces[] = {
    "settings", "desktop", "package", "wifi", "ota", "pwr", "net",
};
static constexpr size_t kNsCount = sizeof(kNamespaces) / sizeof(kNamespaces[0]);

static const char* kMirrorName = "nvs.bin";
static const uint16_t kVersion = 1;
static const int64_t kMirrorIntervalUs = 60LL * 1000000LL;

static std::string s_root;
static bool        s_ready = false;
static int64_t     s_last_mirror = 0;

/* ============================================================
 *  小工具
 * ============================================================ */
static bool _file_exists(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static bool _ensure_dir(const std::string& p) {
    if (::mkdir(p.c_str(), 0777) == 0) return true;
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static bool _writable(const std::string& dir) {
    std::string probe = dir + "/.vault_probe";
    FILE* f = std::fopen(probe.c_str(), "wb");
    if (!f) return false;
    std::fputc('1', f);
    std::fclose(f);
    ::remove(probe.c_str());
    return true;
}

/* 我方命名空间里是否有任何一条数据 */
static bool _user_data_present() {
    for (size_t i = 0; i < kNsCount; i++) {
        nvs_handle_t h;
        if (nvs_open(kNamespaces[i], NVS_READONLY, &h) != ESP_OK) continue;
        bool has = false;
        nvs_iterator_t it = nullptr;
        if (nvs_entry_find_in_handle(h, NVS_TYPE_ANY, &it) == ESP_OK) {
            has = true;
            nvs_release_iterator(it);
        }
        nvs_close(h);
        if (has) return true;
    }
    return false;
}

/* ============================================================
 *  镜像：读一条 key，按类型取出原始字节
 * ============================================================ */
static bool _read_value(nvs_handle_t h, const char* key, nvs_type_t type,
                        std::string& out, uint8_t& tag) {
    out.clear();
    tag = static_cast<uint8_t>(type);
    switch (type) {
        case NVS_TYPE_U8: {
            uint8_t v = 0;
            if (nvs_get_u8(h, key, &v) != ESP_OK) return false;
            out.assign(reinterpret_cast<char*>(&v), 1);
            return true;
        }
        case NVS_TYPE_I8: {
            int8_t v = 0;
            if (nvs_get_i8(h, key, &v) != ESP_OK) return false;
            out.assign(reinterpret_cast<char*>(&v), 1);
            return true;
        }
        case NVS_TYPE_U16: {
            uint16_t v = 0;
            if (nvs_get_u16(h, key, &v) != ESP_OK) return false;
            out.assign(reinterpret_cast<char*>(&v), 2);
            return true;
        }
        case NVS_TYPE_I16: {
            int16_t v = 0;
            if (nvs_get_i16(h, key, &v) != ESP_OK) return false;
            out.assign(reinterpret_cast<char*>(&v), 2);
            return true;
        }
        case NVS_TYPE_U32: {
            uint32_t v = 0;
            if (nvs_get_u32(h, key, &v) != ESP_OK) return false;
            out.assign(reinterpret_cast<char*>(&v), 4);
            return true;
        }
        case NVS_TYPE_I32: {
            int32_t v = 0;
            if (nvs_get_i32(h, key, &v) != ESP_OK) return false;
            out.assign(reinterpret_cast<char*>(&v), 4);
            return true;
        }
        case NVS_TYPE_U64: {
            uint64_t v = 0;
            if (nvs_get_u64(h, key, &v) != ESP_OK) return false;
            out.assign(reinterpret_cast<char*>(&v), 8);
            return true;
        }
        case NVS_TYPE_I64: {
            int64_t v = 0;
            if (nvs_get_i64(h, key, &v) != ESP_OK) return false;
            out.assign(reinterpret_cast<char*>(&v), 8);
            return true;
        }
        case NVS_TYPE_STR: {
            size_t len = 0;
            if (nvs_get_str(h, key, nullptr, &len) != ESP_OK || len == 0) return false;
            out.resize(len);
            if (nvs_get_str(h, key, &out[0], &len) != ESP_OK) { out.clear(); return false; }
            out.resize(len);
            return true;
        }
        case NVS_TYPE_BLOB: {
            size_t len = 0;
            if (nvs_get_blob(h, key, nullptr, &len) != ESP_OK) return false;
            if (len == 0) return true;
            out.resize(len);
            if (nvs_get_blob(h, key, &out[0], &len) != ESP_OK) { out.clear(); return false; }
            out.resize(len);
            return true;
        }
        default:
            return false;   /* float/double 等少见类型不参与镜像 */
    }
}

static bool _put_u16(FILE* f, uint16_t v) { return std::fwrite(&v, 1, 2, f) == 2; }
static bool _put_u32(FILE* f, uint32_t v) { return std::fwrite(&v, 1, 4, f) == 4; }
static bool _put_u8(FILE* f, uint8_t v) { return std::fwrite(&v, 1, 1, f) == 1; }

/* 写一个命名空间块，返回写入条目数；-1 表示失败 */
static int _write_ns(FILE* f, const char* ns) {
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return 0;

    size_t name_len = std::strlen(ns);
    if (name_len > 255) { nvs_close(h); return -1; }
    if (!_put_u8(f, static_cast<uint8_t>(name_len))) { nvs_close(h); return -1; }
    if (std::fwrite(ns, 1, name_len, f) != name_len) { nvs_close(h); return -1; }

    long count_pos = std::ftell(f);
    if (!_put_u16(f, 0)) { nvs_close(h); return -1; }

    int count = 0;
    nvs_iterator_t it = nullptr;
    if (nvs_entry_find_in_handle(h, NVS_TYPE_ANY, &it) == ESP_OK) {
        do {
            nvs_entry_info_t info;
            if (nvs_entry_info(it, &info) != ESP_OK) break;
            std::string data;
            uint8_t tag = 0;
            if (!_read_value(h, info.key, info.type, data, tag)) continue;

            size_t klen = std::strlen(info.key);
            if (klen == 0 || klen > 255) continue;
            if (!_put_u8(f, static_cast<uint8_t>(klen))) break;
            if (std::fwrite(info.key, 1, klen, f) != klen) break;
            if (!_put_u8(f, tag)) break;
            if (!_put_u32(f, static_cast<uint32_t>(data.size()))) break;
            if (!data.empty() && std::fwrite(data.data(), 1, data.size(), f) != data.size()) break;
            count++;
        } while (nvs_entry_next(&it) == ESP_OK);
        nvs_release_iterator(it);
    }
    nvs_close(h);

    long end_pos = std::ftell(f);
    std::fseek(f, count_pos, SEEK_SET);
    _put_u16(f, static_cast<uint16_t>(count));
    std::fseek(f, end_pos, SEEK_SET);
    return count;
}

/* ============================================================
 *  回灌
 * ============================================================ */
static bool _restore_ns(FILE* f) {
    uint8_t nlen = 0;
    if (std::fread(&nlen, 1, 1, f) != 1 || nlen == 0 || nlen > 32) return false;
    char ns[36] = {};
    if (std::fread(ns, 1, nlen, f) != nlen) return false;
    uint16_t count = 0;
    if (std::fread(&count, 1, 2, f) != 2) return false;

    nvs_handle_t h;
    bool opened = nvs_open(ns, NVS_READWRITE, &h) == ESP_OK;

    for (uint16_t i = 0; i < count; i++) {
        uint8_t klen = 0;
        if (std::fread(&klen, 1, 1, f) != 1 || klen == 0 || klen > 32) return false;
        char key[36] = {};
        if (std::fread(key, 1, klen, f) != klen) return false;
        uint8_t tag = 0;
        if (std::fread(&tag, 1, 1, f) != 1) return false;
        uint32_t dlen = 0;
        if (std::fread(&dlen, 1, 4, f) != 4) return false;

        std::string data;
        if (dlen) {
            data.resize(dlen);
            if (std::fread(&data[0], 1, dlen, f) != dlen) return false;
        }
        if (!opened) continue;

        nvs_type_t t = static_cast<nvs_type_t>(tag);
        switch (t) {
            case NVS_TYPE_U8:  if (dlen >= 1) nvs_set_u8 (h, key, *reinterpret_cast<uint8_t*>(&data[0])); break;
            case NVS_TYPE_I8:  if (dlen >= 1) nvs_set_i8 (h, key, *reinterpret_cast<int8_t*>(&data[0])); break;
            case NVS_TYPE_U16: if (dlen >= 2) nvs_set_u16(h, key, *reinterpret_cast<uint16_t*>(&data[0])); break;
            case NVS_TYPE_I16: if (dlen >= 2) nvs_set_i16(h, key, *reinterpret_cast<int16_t*>(&data[0])); break;
            case NVS_TYPE_U32: if (dlen >= 4) nvs_set_u32(h, key, *reinterpret_cast<uint32_t*>(&data[0])); break;
            case NVS_TYPE_I32: if (dlen >= 4) nvs_set_i32(h, key, *reinterpret_cast<int32_t*>(&data[0])); break;
            case NVS_TYPE_U64: if (dlen >= 8) nvs_set_u64(h, key, *reinterpret_cast<uint64_t*>(&data[0])); break;
            case NVS_TYPE_I64: if (dlen >= 8) nvs_set_i64(h, key, *reinterpret_cast<int64_t*>(&data[0])); break;
            case NVS_TYPE_STR: nvs_set_str (h, key, data.c_str()); break;
            case NVS_TYPE_BLOB: nvs_set_blob(h, key, data.data(), dlen); break;
            default: break;
        }
    }
    if (opened) {
        nvs_commit(h);
        nvs_close(h);
    }
    return true;
}

static int _restore() {
    std::string p = path(kMirrorName);
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return -1;

    char magic[4] = {};
    uint16_t ver = 0, ns_count = 0;
    if (std::fread(magic, 1, 4, f) != 4 ||
        std::memcmp(magic, "MVLT", 4) != 0 ||
        std::fread(&ver, 1, 2, f) != 2 || ver != kVersion ||
        std::fread(&ns_count, 1, 2, f) != 2 || ns_count > 32) {
        std::fclose(f);
        ESP_LOGE(TAG, "nvs.bin header invalid");
        return -1;
    }
    int restored = 0;
    for (uint16_t i = 0; i < ns_count; i++) {
        long before = std::ftell(f);
        if (!_restore_ns(f)) {
            std::fclose(f);
            ESP_LOGE(TAG, "nvs.bin truncated at ns %u (off %ld)", (unsigned)i, before);
            return -1;
        }
        restored++;
    }
    std::fclose(f);
    return restored;
}

/* ============================================================
 *  对外接口
 * ============================================================ */
bool ready() { return s_ready; }

const std::string& root() { return s_root; }

std::string path(const char* name) {
    if (s_root.empty() || !name) return std::string();
    return s_root + "/" + name;
}

const char* state() { return s_ready ? "card" : "flash"; }

bool mirror_save() {
    if (!s_ready) return false;
    std::string tmp = path("nvs.tmp");
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) {
        ESP_LOGW(TAG, "mirror: cannot open %s", tmp.c_str());
        return false;
    }

    bool ok = true;
    char magic[4] = {'M', 'V', 'L', 'T'};
    ok = ok && std::fwrite(magic, 1, 4, f) == 4;
    ok = ok && _put_u16(f, kVersion);
    ok = ok && _put_u16(f, static_cast<uint16_t>(kNsCount));

    int total = 0;
    for (size_t i = 0; ok && i < kNsCount; i++) {
        int n = _write_ns(f, kNamespaces[i]);
        if (n < 0) { ok = false; break; }
        total += n;
    }
    std::fclose(f);

    if (!ok) {
        ::remove(tmp.c_str());
        ESP_LOGE(TAG, "mirror: write failed");
        return false;
    }

    std::string dst = path(kMirrorName);
    ::remove(dst.c_str());
    if (std::rename(tmp.c_str(), dst.c_str()) != 0) {
        ::remove(tmp.c_str());
        ESP_LOGE(TAG, "mirror: rename failed");
        return false;
    }

    s_last_mirror = esp_timer_get_time();
    ESP_LOGI(TAG, "mirror saved: %d entries -> %s", total, dst.c_str());
    return true;
}

void tick() {
    if (!s_ready) return;
    int64_t now = esp_timer_get_time();
    if (now - s_last_mirror < kMirrorIntervalUs) return;
    mirror_save();
}

esp_err_t prepare(const char* mount_point) {
    if (!mount_point || !mount_point[0]) {
        s_ready = false;
        return ESP_FAIL;
    }

    std::string m = mount_point;
    if (m.back() == '/') m.pop_back();
    s_root = m + "/sys";

    if (!_ensure_dir(s_root) || !_writable(s_root)) {
        ESP_LOGW(TAG, "card not writable at %s -> fallback to flash", s_root.c_str());
        s_ready = false;
        s_root.clear();
        return ESP_FAIL;
    }
    s_ready = true;

    bool has_flash_data = _user_data_present();
    bool has_mirror = _file_exists(path(kMirrorName));

    if (!has_flash_data && has_mirror) {
        int n = _restore();
        if (n > 0) {
            ESP_LOGI(TAG, "restored %d namespaces from card mirror", n);
        } else {
            ESP_LOGW(TAG, "card mirror unusable, starting with empty NVS");
        }
    }

    if (!has_mirror) {
        mirror_save();
    } else {
        s_last_mirror = esp_timer_get_time();
    }
    return ESP_OK;
}

} }

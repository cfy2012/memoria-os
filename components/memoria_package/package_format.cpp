/**
 * @file package_format.cpp
 * @brief .msp 自定义安装包格式
 *
 * 布局（固定大小 header，payload 追加）：
 *   offset  size  field
 *   0       6     magic "MSPACK"
 *   6       4     version (uint32)
 *   10      32    name[32] (null-terminated)
 *   42      4     payload_size (uint32)
 *   46      4     crc32 (uint32)：仅对 payload 计算
 *   50      N     payload
 *
 * 用途：脚本/小游戏安装包、OTA 固件不使用此格式（esp_https_ota 有自己的）。
 */

#include "package_format.hpp"
#include "package_manager.hpp"
#include <cstdio>
#include <esp_log.h>
#include <cstring>
#include <cctype>
#include <vector>

namespace memoria {
namespace package {

#pragma pack(push, 1)
struct MspHeader {
    char      magic[6];       /* "MSPACK" */
    uint32_t  version;
    char      name[32];
    uint32_t  payload_size;
    uint32_t  crc32;
};
#pragma pack(pop)
static_assert(sizeof(MspHeader) == 50, "MspHeader must be 50 bytes");

int create_package(const std::string& name, uint32_t version,
                   const uint8_t* payload, uint32_t payload_len,
                   const std::string& out_path) {
    /* 包名白名单：仅 [A-Za-z0-9_.-]，防路径穿越（同 STORE 修法） */
    if (name.empty() || name == "." || name == "..") return -2;
    for (char ch : name) {
        if (!(std::isalnum((unsigned char)ch) || ch == '_' || ch == '.' || ch == '-')) return -2;
    }
    if (payload_len > 0 && !payload) return -3;   /* 声明有数据却无指针：拒绝生成坏包 */
    MspHeader h{};
    std::memcpy(h.magic, "MSPACK", 6);
    h.version = version;
    std::strncpy(h.name, name.c_str(), sizeof(h.name) - 1);
    h.name[sizeof(h.name) - 1] = 0;   /* 强制终止，防尾部无 null */
    h.payload_size = payload_len;
    h.crc32 = crc32_compute(payload, payload_len);

    FILE* fp = std::fopen(out_path.c_str(), "wb");
    if (!fp) return -1;
    std::fwrite(&h, sizeof(h), 1, fp);
    if (payload && payload_len) std::fwrite(payload, 1, payload_len, fp);
    std::fclose(fp);
    return 0;
}

bool verify_package(const std::string& path, std::string* out_name,
                    uint32_t* out_version, std::vector<uint8_t>* out_payload) {
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return false;

    MspHeader h{};
    if (std::fread(&h, sizeof(h), 1, fp) != 1) { std::fclose(fp); return false; }
    if (std::memcmp(h.magic, "MSPACK", 6) != 0) { std::fclose(fp); return false; }
    h.name[sizeof(h.name) - 1] = 0;   /* 强制终止：文件里 name 无 null 时防 std::string 读越界 */

    /* payload 上限保护：损坏/恶意文件声明超大 size 时拒载，防内存耗尽 */
    if (h.payload_size > 1024 * 1024) { std::fclose(fp); return false; }

    std::vector<uint8_t> payload(h.payload_size);
    if (h.payload_size > 0) {
        if (std::fread(payload.data(), 1, h.payload_size, fp) != h.payload_size) {
            std::fclose(fp); return false;
        }
    }
    std::fclose(fp);

    uint32_t actual_crc = crc32_compute(payload.data(), payload.size());
    if (actual_crc != h.crc32) { ESP_LOGW("PKG_FMT", "CRC mismatch"); return false; }

    if (out_name)     *out_name     = h.name;
    if (out_version)  *out_version  = h.version;
    if (out_payload)  *out_payload  = std::move(payload);
    return true;
}

} }
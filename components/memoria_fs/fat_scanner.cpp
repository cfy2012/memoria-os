/**
 * @file fat_scanner.cpp
 * @brief TF 卡 FAT 目录扫描，与私有文件系统双向同步
 *
 * C++ 头文件必须放在 extern "C" 块之外，否则符号按 C linkage 修饰，
 * 与 C++ 标准库头冲突会导致链接失败。
 */

#include "fat_scanner.hpp"
#include "sdcard.hpp"   /* C++ 头文件，置于 extern "C" 之外 */

extern "C" {
#include <esp_log.h>
#include <dirent.h>
#include <sys/stat.h>
}

#include <cstdio>

#include <algorithm>
#include <cstring>

namespace memoria {
namespace fs {

static const char* TAG = "FS_SCAN";

FatScanner* FatScanner::instance() {
    static FatScanner inst;
    return &inst;
}

static FileType guess_type(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    auto end_with = [&](const char* suf) {
        size_t len = std::strlen(suf);
        return lower.size() >= len && lower.compare(lower.size() - len, len, suf) == 0;
    };
    if (end_with(".jpg") || end_with(".jpeg")) return FileType::Photo;
    if (end_with(".txt")) return FileType::Note;
    if (end_with(".wav") || end_with(".mp3") ||
        end_with(".m4a") || end_with(".aac")) return FileType::Audio;
    return FileType::Unknown;
}

esp_err_t FatScanner::_scan_one(const std::string& path, FileType expected_type, int* added) {
    DIR* d = opendir(path.c_str());
    if (!d) { ESP_LOGW(TAG, "opendir %s failed", path.c_str()); return ESP_OK; }

    int count = 0;
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        if (ent->d_name[0] == '.') continue;

        std::string full_path = path + "/" + ent->d_name;
        if (PrivateFs::instance()->find_by_name(full_path)) continue;

        FileType ft = guess_type(full_path);
        if (ft != expected_type) continue;

        struct stat st{};
        if (stat(full_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;

        esp_err_t ret = PrivateFs::instance()->create(ft, full_path, 0);
        if (ret == ESP_OK) {
            count++;
            ESP_LOGI(TAG, "  + [%c] %s (%ld B)",
                     ft == FileType::Photo ? 'P' :
                     ft == FileType::Note  ? 'N' :
                     ft == FileType::Audio ? 'A' : '?',
                     full_path.c_str(), (long)st.st_size);
        }
    }
    closedir(d);
    if (added) *added += count;
    return ESP_OK;
}

esp_err_t FatScanner::scan_dir(const std::string& subdir, int* added_count) {
    std::string path = _fat_prefix + subdir;
    mkdir(path.c_str(), 0755);

    FileType ft = FileType::Unknown;
    if (subdir.find("photo") != std::string::npos)  ft = FileType::Photo;
    else if (subdir.find("note") != std::string::npos)  ft = FileType::Note;
    else if (subdir.find("audio") != std::string::npos) ft = FileType::Audio;

    return _scan_one(path, ft, added_count);
}

esp_err_t FatScanner::scan_and_sync(int* added_count, int* removed_count) {
    ESP_LOGI(TAG, "=== FAT scan start ===");

    int added = 0;
    scan_dir("/photos", &added);
    scan_dir("/notes",  &added);
    scan_dir("/audio",  &added);

    /* 删除清理：遍历索引检查源文件是否仍存在 */
    int removed = 0;
    FileType types[] = {FileType::Photo, FileType::Note, FileType::Audio};
    for (int ti = 0; ti < 3; ti++) {
        std::vector<uint32_t> to_remove;
        PrivateFs::instance()->list_by_type(types[ti],
            [&](const Entry& e) -> bool {
                if (e.id == 0) return true;
                struct stat st{};
                if (stat(e.name, &st) != 0) to_remove.push_back(e.id);
                return true;
            });
        for (auto id : to_remove) {
            PrivateFs::instance()->remove(id);
            removed++;
        }
    }

    PrivateFs::instance()->sync();
    ESP_LOGI(TAG, "=== FAT scan done: +%d -%d ===", added, removed);
    if (added_count)   *added_count   = added;
    if (removed_count) *removed_count = removed;
    return ESP_OK;
}

} // namespace fs
} // namespace memoria
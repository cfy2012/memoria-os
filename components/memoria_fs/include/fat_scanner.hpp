/**
 * @file fat_scanner.hpp
 * @brief FAT32 分区素材扫描 + 私有 FS 索引同步
 */

#pragma once

#include "private_fs.hpp"
#include <esp_err.h>
#include <string>

namespace memoria {
namespace fs {

class FatScanner {
public:
    static FatScanner* instance();

    esp_err_t scan_and_sync(int* added_count   = nullptr,
                             int* removed_count = nullptr);

    esp_err_t scan_dir(const std::string& subdir, int* added_count = nullptr);

private:
    FatScanner() = default;
    esp_err_t _scan_one(const std::string& path, fs::FileType type, int* added);

    const std::string _fat_prefix{"/mem_fat"};
};

} // namespace fs
} // namespace memoria

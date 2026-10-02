/**
 * @file sdcard.hpp
 * @brief SPI 模式 TF 卡：复用共享 SPI 总线挂载 FAT32 (/mem_fat)
 *
 * 关键约束：SPI 总线由 SpiBus 单例独占初始化一次。
 *    SdCard 只调用 sdspi_host_init_device()，不再调用
 *    spi_bus_initialize() 或 SDSPI_HOST_DEFAULT()。
 */

#pragma once

#include <esp_err.h>
#include <string>
#include <memory>

namespace memoria {
namespace drivers {

class SdCard {
public:
    static SdCard* instance();

    esp_err_t init();
    esp_err_t deinit();
    bool      present();

    const std::string& mount_point() const { return _mount_point; }
    bool is_mounted() const { return _mounted; }

private:
    SdCard() = default;

    std::string _mount_point{"/mem_fat"};
    bool        _mounted = false;
};

} // namespace drivers
} // namespace memoria
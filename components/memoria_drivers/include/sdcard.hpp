/**
 * @file sdcard.hpp
 * @brief SPI 模式 TF 卡：复用共享 SPI 总线挂载 FAT32 (/mem_fat)
 *
 * 关键约束：SPI 总线由 SpiBus 单例独占初始化一次。
 *    SdCard 只通过 sdspi 在已有总线上挂设备；
 *    IDF 6.1 的 sdspi_host_init() 为空操作，不会重复初始化总线。
 */

#pragma once

#include <esp_err.h>
#include <string>

extern "C" {
#include "sd_protocol_types.h"   /* sdmmc_card_t 完整定义（sdmmc 组件公开头） */
}

namespace memoria {
namespace drivers {

class SdCard {
public:
    static SdCard* instance();

    /* quiet=true：失败时不打 ERROR 日志（poll 周期重试用） */
    esp_err_t init(bool quiet = false);
    esp_err_t deinit();
    /* 应急格式化：卸载 -> format_if_mount_failed=true 重挂（shell safe 模式用） */
    esp_err_t reformat();
    bool      present();
    void      poll();   /* 热插拔轮询：拔卡自动卸载，插卡自动重挂（由内核主循环周期调用） */

    const std::string& mount_point() const { return _mount_point; }
    bool is_mounted() const { return _mounted; }

private:
    SdCard() = default;

    std::string    _mount_point{"/mem_fat"};
    bool           _mounted = false;
    bool           _format_next = false;   /* 下次 init 时 format_if_mount_failed=true */
    sdmmc_card_t*  _card = nullptr;        /* mount 出参，deinit 需要它 */
};

} // namespace drivers
} // namespace memoria

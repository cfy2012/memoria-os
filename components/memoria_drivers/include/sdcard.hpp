/**
 * @file sdcard.hpp
 * @brief TF 卡：屏幕 TF 卡槽走 SPI 挂 /mem_fat（启动卡），SD 大卡座走 SDMMC 挂 /sd_ext（扩展）
 *
 * 关键约束：SPI 总线由 SpiBus 单例独占初始化一次。
 *    SdCard 只通过 sdspi 在已有总线上挂设备；
 *    IDF 6.1 的 sdspi_host_init() 为空操作，不会重复初始化总线。
 *    屏幕 CS=10 与 TF CS=17 分时片选，SDMMC 走独立外设，两卡可同时在线。
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

    /* 主卡 = 屏幕上的 TF 卡槽（SPI2 共享总线，CS=17）→ /mem_fat
     * 字库 /mem_fat/font/gb2312_16.bin、store、脚本、音频全在这张卡上。
     * quiet=true：失败时不打 ERROR 日志（poll 周期重试用） */
    esp_err_t init(bool quiet = false);

    /* 扩展卡 = 侧面 SD 大卡座（SDMMC 1-bit，CLK/CMD/D0 = 40/39/41）
     * 主卡在位时挂 /sd_ext（导照片/数据用，不碰启动卡）；
     * 主卡不在位时接管 /mem_fat 兜底，避免没字库开不了机。
     * 卡座未焊/没插卡返回错误，不阻塞启动。 */
    esp_err_t init_sdmmc(bool quiet = false);

    esp_err_t deinit();        /* 主卡卸载 */
    esp_err_t deinit_sdmmc();  /* 扩展卡卸载 */
    /* 应急格式化：卸载 -> format_if_mount_failed=true 重挂（shell safe 模式用） */
    esp_err_t reformat();
    bool      present();
    void      poll();   /* 热插拔轮询：拔卡自动卸载，插卡自动重挂（由内核主循环周期调用） */

    const std::string& mount_point()     const { return _mount_point; }
    const std::string& ext_mount_point() const { return _ext_mount; }
    bool is_mounted()    const { return _mounted; }
    bool is_sdmmc()      const { return _sdmmc; }        /* 主卡当前走的哪条通路 */
    bool ext_mounted()   const { return _ext_mounted; }

private:
    SdCard() = default;

    std::string    _mount_point{"/mem_fat"};
    std::string    _ext_mount{"/sd_ext"};
    bool           _mounted     = false;
    bool           _sdmmc       = false;   /* 主卡是否走 SDMMC（兜底接管时） */
    bool           _ext_mounted = false;
    bool           _format_next = false;   /* 下次 init 时 format_if_mount_failed=true */
    sdmmc_card_t*  _card        = nullptr; /* 主卡 mount 出参，deinit 需要它 */
    sdmmc_card_t*  _ext_card    = nullptr; /* 扩展卡 mount 出参 */
};

} // namespace drivers
} // namespace memoria

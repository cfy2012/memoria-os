/**
 * @file sdcard.cpp
 * @brief SPI TF 卡：复用 SpiBus 单例，严禁重复 spi_bus_initialize()
 */

#include "sdcard.hpp"
#include "spi_bus.hpp"

extern "C" {
#include <driver/sdspi_host.h>
#include <driver/sdmmc_host.h>
#include <driver/spi_master.h>
#include <esp_vfs_fat.h>
#include <esp_log.h>
}

#include <cstdio>

#include <cstring>

namespace memoria {
namespace drivers {

static const char* TAG = "SDCARD";

SdCard* SdCard::instance() {
    static SdCard inst;
    return &inst;
}

esp_err_t SdCard::init() {
    if (_mounted) return ESP_OK;

    auto* bus = SpiBus::instance();
    if (!bus->is_inited()) {
        ESP_LOGE(TAG, "SpiBus not ready — call SpiBus::init() first");
        return ESP_ERR_INVALID_STATE;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI_BUS_HOST;

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = static_cast<gpio_num_t>(SD_CS_GPIO);
    slot_cfg.host_id = static_cast<spi_host_device_t>(SPI_BUS_HOST);

    esp_vfs_fat_mount_config_t mount_cfg{};
    mount_cfg.format_if_mount_failed = false;
    mount_cfg.max_files = 16;
    mount_cfg.allocation_unit_size = 32 * 1024;

    esp_err_t ret = esp_vfs_fat_sdspi_mount(
        _mount_point.c_str(), &host, &slot_cfg, &mount_cfg, nullptr);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FAT mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    _mounted = true;
    ESP_LOGI(TAG, "FAT mounted at %s", _mount_point.c_str());
    return ESP_OK;
}

esp_err_t SdCard::deinit() {
    if (!_mounted) return ESP_OK;
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(_mount_point.c_str(), nullptr);
    if (ret == ESP_OK) _mounted = false;
    return ret;
}

bool SdCard::present() {
    FILE* f = fopen(_mount_point.c_str(), "r");
    if (f) { fclose(f); return true; }
    return false;
}

void SdCard::poll() {
    if (_mounted) {
        /* 已挂载：探测卡是否仍存在（拔出则卸载 VFS，避免读写悬空错误） */
        if (!present()) {
            ESP_LOGW(TAG, "card removed, unmounting");
            deinit();
        }
    } else {
        /* 未挂载：直接尝试挂载（成功即卡已插入/恢复） */
        esp_err_t ret = init();
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "card inserted, re-mounted at %s", _mount_point.c_str());
        }
    }
}

} // namespace drivers
} // namespace memoria
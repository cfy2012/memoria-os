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

esp_err_t SdCard::init(bool quiet) {
    if (_mounted) return ESP_OK;

    auto* bus = SpiBus::instance();
    if (!bus->is_inited()) {
        ESP_LOGE(TAG, "SpiBus not ready — call SpiBus::init() first");
        return ESP_ERR_INVALID_STATE;
    }

    /* #83：压制 IDF 内部日志（sdmmc/vfs/sdspi/gpio）。无卡探测 0x108 两连 +
     * GPIO CS conflict 警告均无信息量且刷屏，只保留本层人话错误。
     * mount 期间全局压（窗口最长 ~4s 无卡超时），结束即恢复。 */
    esp_log_level_t lv_sdmmc = esp_log_level_get("sdmmc_sd");
    esp_log_level_t lv_vfs   = esp_log_level_get("vfs_fat_sdmmc");
    esp_log_level_t lv_sdspi = esp_log_level_get("sdspi_host");
    esp_log_level_t lv_gpio  = esp_log_level_get("gpio");
    esp_log_level_set("sdmmc_sd", ESP_LOG_NONE);
    esp_log_level_set("vfs_fat_sdmmc", ESP_LOG_NONE);
    esp_log_level_set("sdspi_host", ESP_LOG_NONE);
    esp_log_level_set("gpio", ESP_LOG_NONE);

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI_BUS_HOST;

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = static_cast<gpio_num_t>(SD_CS_GPIO);
    slot_cfg.host_id = static_cast<spi_host_device_t>(SPI_BUS_HOST);

    esp_vfs_fat_mount_config_t mount_cfg{};
    mount_cfg.format_if_mount_failed = _format_next;
    mount_cfg.max_files = 16;
    mount_cfg.allocation_unit_size = 32 * 1024;

    /* 修复：out_card 不可为 NULL —— IDF esp_vfs_fat_sdspi_mount 参数校验
     * 对 out_card==NULL 直接返回 ESP_ERR_INVALID_ARG（未触卡即失败）。
     * 另外 deinit/unmount 需要 card 句柄，存成员。 */
    sdmmc_card_t* card = nullptr;
    esp_err_t ret = esp_vfs_fat_sdspi_mount(
        _mount_point.c_str(), &host, &slot_cfg, &mount_cfg, &card);

    esp_log_level_set("sdmmc_sd", lv_sdmmc);
    esp_log_level_set("vfs_fat_sdmmc", lv_vfs);
    esp_log_level_set("sdspi_host", lv_sdspi);
    esp_log_level_set("gpio", lv_gpio);

    if (ret != ESP_OK) {
        if (quiet) ESP_LOGD(TAG, "mount retry: %s", esp_err_to_name(ret));
        else       ESP_LOGE(TAG, "FAT mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    _card = card;
    _mounted = true;
    ESP_LOGI(TAG, "FAT mounted at %s", _mount_point.c_str());
    return ESP_OK;
}

esp_err_t SdCard::reformat() {
    deinit();
    _format_next = true;
    esp_err_t ret = init();
    _format_next = false;
    return ret;
}

esp_err_t SdCard::deinit() {
    if (!_mounted) return ESP_OK;
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(_mount_point.c_str(), _card);
    if (ret == ESP_OK) { _mounted = false; _card = nullptr; }
    return ret;
}

bool SdCard::present() {
    FILE* f = fopen(_mount_point.c_str(), "r");
    if (f) { fclose(f); return true; }
    return false;
}

void SdCard::poll() {
    /* #83：未挂载时不自动重试——每次 mount 尝试阻塞主循环 ~4s（无卡探测超时）
     * 且产生 GPIO CS conflict 噪音。挂卡后由 shell 'sd mount' 手动挂载。 */
    if (!_mounted) return;
    /* 已挂载：探测卡是否仍存在（拔出则卸载 VFS，避免读写悬空错误） */
    if (!present()) {
        ESP_LOGW(TAG, "card removed, unmounting");
        deinit();
    }
}

} // namespace drivers
} // namespace memoria

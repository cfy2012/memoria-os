/**
 * @file spi_bus.cpp
 * @brief 共享 SPI 总线：spi_bus_initialize() 只调用一次
 *
 * ILI9341(26MHz) + SD(20MHz) 共用 SPI2_HOST。
 * 所有 SPI 事务由 FreeRTOS Mutex 串行化，防止并发抢占。
 */

#include "spi_bus.hpp"

extern "C" {
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_log.h>
}

namespace memoria {
namespace drivers {

static const char* TAG = "SPI_BUS";

SpiBus* SpiBus::instance() {
    static SpiBus inst;
    return &inst;
}

esp_err_t SpiBus::init() {
    if (_inited) return ESP_OK;

    /* SPI2 主机总线：三根物理线，CS 由各 device handle 独立管理 */
    spi_bus_config_t bus_cfg{};
    bus_cfg.sclk_io_num   = SPI_SCLK_GPIO;
    bus_cfg.mosi_io_num   = SPI_MOSI_GPIO;
    bus_cfg.miso_io_num   = SPI_MISO_GPIO;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 8192;

    esp_err_t ret = spi_bus_initialize(static_cast<spi_host_device_t>(SPI_BUS_HOST), &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* ILI9341: 26MHz，正 CS */
    spi_device_interface_config_t lcd_cfg{};
    lcd_cfg.clock_speed_hz = SPI_BUS_FREQ_HZ;
    lcd_cfg.mode           = 0;
    lcd_cfg.spics_io_num   = LCD_CS_GPIO;
    lcd_cfg.queue_size     = 2;
    lcd_cfg.flags          = SPI_DEVICE_POSITIVE_CS;
    ret = spi_bus_add_device(static_cast<spi_host_device_t>(SPI_BUS_HOST), &lcd_cfg, &_handles.lcd);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "lcd device add: %s", esp_err_to_name(ret)); return ret; }

    /* TF 卡: 20MHz */
    spi_device_interface_config_t sd_cfg{};
    sd_cfg.clock_speed_hz = 20000000;
    sd_cfg.mode           = 0;
    sd_cfg.spics_io_num   = SD_CS_GPIO;
    sd_cfg.queue_size     = 1;
    sd_cfg.flags          = SPI_DEVICE_POSITIVE_CS;
    ret = spi_bus_add_device(static_cast<spi_host_device_t>(SPI_BUS_HOST), &sd_cfg, &_handles.sd);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "sd device add: %s", esp_err_to_name(ret)); return ret; }

    /* Mutex：串行化所有 SPI 事务
     * 必须用递归锁：Ili9341::flush 持锁后经 _set_window→_write_cmd 再次取锁 */
    _mutex = xSemaphoreCreateRecursiveMutex();
    if (!_mutex) return ESP_ERR_NO_MEM;

    _inited = true;
    ESP_LOGI(TAG, "SPI2 OK. LCD=26MHz SD=20MHz. Mutex ready.");
    return ESP_OK;
}

} // namespace drivers
} // namespace memoria
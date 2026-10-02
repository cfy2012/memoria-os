/**
 * @file spi_bus.hpp
 * @brief 共享 SPI 总线管理器（C++ RAII 封装）
 *
 * 单块 ILI9341 + TF 卡共用 SPI2_HOST，全部事务由 FreeRTOS Mutex 串行化。
 */

#pragma once

#include "drivers_config.hpp"

extern "C" {
#include <driver/spi_master.h>
}

#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstdint>

namespace memoria {
namespace drivers {

struct SpiHandles {
    spi_device_handle_t lcd = nullptr;
    spi_device_handle_t sd  = nullptr;
};

class SpiBus {
public:
    static SpiBus* instance();
    esp_err_t init();

    const SpiHandles&  handles()  const { return _handles; }
    SemaphoreHandle_t  mutex()    const { return _mutex; }
    bool               is_inited() const { return _inited; }

    SpiBus(const SpiBus&) = delete;
    SpiBus& operator=(const SpiBus&) = delete;

private:
    SpiBus() = default;

    SpiHandles        _handles{};
    SemaphoreHandle_t _mutex   = nullptr;
    bool              _inited  = false;
};

} // namespace drivers
} // namespace memoria

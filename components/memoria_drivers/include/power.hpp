/**
 * @file power.hpp
 * @brief 电源与背光管理：LEDC PWM 调节背光亮度 + 自动息屏
 *
 * 硬件：GPIO14 接 ILI9341 BLK 引脚，LEDC 通道 0
 * 功能：set_backlight / get_backlight / idle timeout 自动变暗 / 触摸事件重置超时
 */

#pragma once

#include "drivers_config.hpp"
#include <esp_err.h>
#include <cstdint>

namespace memoria {
namespace drivers {

class Power {
public:
    static Power* instance();

    esp_err_t init();

    /* 背光亮度 0~255，LEDC 真正写寄存器 */
    void      set_backlight(uint8_t brightness);
    uint8_t   get_backlight() const { return _backlight; }

    /* 空闲超时：秒 */
    void      set_idle_timeout_sec(uint32_t sec) { _idle_timeout_sec = sec; }
    void      touch_event();          /* 触摸/摇杆事件调用，重置计时器 */

    /* 休眠：强制 0 亮度 */
    void      deep_sleep();
    bool      is_inited() const { return _inited; }

    /* 状态查询 */
    bool      is_dimmed()   const { return _dimmed; }
    uint32_t  idle_sec()    const;

private:
    Power() = default;
    static void _power_task_c(void* arg);

    uint8_t   _backlight = 255;
    uint32_t  _idle_timeout_sec = 60;   /* 1 分钟息屏 */
    uint32_t  _last_activity_ms = 0;
    bool      _dimmed = false;
    bool      _inited = false;
};

} // namespace drivers
} // namespace memoria
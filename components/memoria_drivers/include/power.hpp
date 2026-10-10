/**
 * @file power.hpp
 * @brief 电源与背光管理：LEDC PWM 调节背光亮度 + 空闲超时自动变暗
 *        + 深睡眠电源管理（GPIO47 功放 EN / GPIO48 次级 MOS / NVS 关机标志）
 *
 * 硬件：GPIO14 接 ILI9341 BLK 引脚，LEDC 通道 0
 * 功能：set_backlight / get_backlight / idle timeout 自动变暗 / 触摸事件重置超时
 * 深睡眠：关机序列由 kernel 层编排（跨组件），本层只提供原子动作。
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

    /* ============ 深睡眠电源管理 ============ */
    /* boot 最早调用（kernel init 第 0 步）：
     * GPIO47=0 功放关、GPIO48=1 次级 MOS 开（外围 5V 上电）、
     * WS2812 DIN(46) 拉低防 boot 闪灯，然后等 50ms 让 5V 轨稳定 */
    void      boot_defaults();

    /* 功放 EN（GPIO47）：true=开（播放时），false=关（省电 <1µA） */
    void      amp_set_en(bool on);

    /* 次级 MOS（GPIO48）：true=外围 5V 轨供电，false=深睡眠断电 */
    void      subm_rail(bool on);

    /* NVS 关机标志：关机序列写入，boot 时读取并清除（记录"上次主动关机"） */
    void      set_shutdown_flag();
    void      clear_shutdown_flag();
    bool      shutdown_flag() const;

    /* 配置 GPIO2 EXT0 唤醒并进入深睡眠（不返回） */
    void      deep_sleep_now();

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
/**
 * @file battery_adc.hpp
 * @brief 电池 ADC 采样：分压 1/2 电路，读取电压 + 估算百分比
 *
 * 硬件：GPIO39 ADC1_CH3
 *   分压电阻 R1=100K (接电池+), R2=100K (接GND)
 *   VADC = VBAT * R2/(R1+R2) = VBAT/2
 *   实际电池电压 = VADC * 2
 */

#pragma once

#include "drivers_config.hpp"
#include <esp_err.h>
#include <cstdint>

namespace memoria {
namespace drivers {

class BatteryAdc {
public:
    static BatteryAdc* instance();
    esp_err_t init();

    /* 当前电池电压（伏） */
    float voltage() const;
    /* 百分比 0~100，线性映射 */
    uint8_t percent() const;

    /* 告警状态 */
    bool is_low()  const { return voltage() < BATTERY_LOW_WARN_VOL; }
    bool is_crit() const { return voltage() < BATTERY_LOW_CRIT_VOL; }

    /* TP4056 CHRG 充电检测（GPIO49 开漏，充电中拉低） */
    bool is_charging() const { return _charging; }

    /* 低电量回调（kernel 层注册，避免 drivers 依赖 kernel）：
     *   crit_cb：电压 < 临界值持续触发（kernel 发 BatteryCrit 事件给 UI）
     *   emergency_cb：电压 < 3.5V 持续 ~30s 触发（kernel 执行保命关机序列） */
    using LowBatteryCb = void (*)();
    static void set_crit_cb(LowBatteryCb cb)      { s_crit_cb = cb; }
    static void set_emergency_cb(LowBatteryCb cb) { s_emergency_cb = cb; }

    bool is_inited() const { return _inited; }

private:
    BatteryAdc() = default;
    static void _sample_task_c(void* arg);

    float   _voltage = 4.0f;
    uint8_t _percent = 80;
    bool    _charging = false;
    bool    _inited = false;

    static LowBatteryCb s_crit_cb;
    static LowBatteryCb s_emergency_cb;
};

} // namespace drivers
} // namespace memoria
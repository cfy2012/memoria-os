/**
 * @file joystick.hpp
 * @brief KY-023 模拟摇杆：X/Y 双 ADC + 按下按钮 GPIO
 *
 * 摇杆模拟值：
 *   X 轴：0~4095 (ADC1_CH0, GPIO36)，中心约 2048
 *   Y 轴：0~4095 (ADC1_CH1, GPIO37)，中心约 2048
 *   BTN ：GPIO38，按下 LOW
 *
 * 消抖：多次采样取平均 + 阈值判断。
 * 事件回调：方向枚举 + 按下/抬起。
 */

#pragma once

#include "drivers_config.hpp"
#include <esp_err.h>
#include <cstdint>
#include <functional>

namespace memoria {
namespace drivers {

enum class JoystickDir : uint8_t {
    None = 0, Up, Down, Left, Right, Center,
};

using JoystickCb = std::function<void(JoystickDir dir, bool pressed)>;

class Joystick {
public:
    static Joystick* instance();
    esp_err_t init();

    /* 阻塞读取当前状态 */
    void read(int32_t& x, int32_t& y, bool& btn) const;
    JoystickDir direction() const;
    bool button_pressed() const { return _last_btn; }

    /* 设置状态变化回调（在后台任务里触发） */
    void set_callback(JoystickCb cb) { _cb = std::move(cb); }

    bool is_inited() const { return _inited; }

private:
    Joystick() = default;
    static void _sample_task_c(void* arg);

    int32_t  _last_x = 0;
    int32_t  _last_y = 0;
    bool     _last_btn = false;
    JoystickDir _last_dir = JoystickDir::Center;

    JoystickCb _cb;
    bool     _inited = false;
};

} // namespace drivers
} // namespace memoria
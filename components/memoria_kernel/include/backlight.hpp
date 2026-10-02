#pragma once
/* ============================================================
 * @file backlight.hpp
 * @brief 键盘灯带控制（WS2812，上下两条，单线 RMT）
 *
 * 背光键（R1I）每按一次循环：关 → 低亮暖白 → 高亮暖白 → 关
 * 任意键按下触发 backlight_flash()：全亮打光 120ms 后恢复档位
 * 引脚与灯珠数唯一来源：drivers_config.hpp（KEY_LED_GPIO）
 * ============================================================ */
#include "esp_err.h"

namespace memoria::input {

/* 初始化 RMT 通道 + WS2812 编码器（首次调用使灯带全灭） */
esp_err_t backlight_init();

/* 背光键：三档循环 */
void backlight_toggle();

/* 按下打光：全亮片刻后恢复当前档位 */
void backlight_flash();

/* 当前是否点亮（供状态栏/设置显示） */
bool backlight_on();

}
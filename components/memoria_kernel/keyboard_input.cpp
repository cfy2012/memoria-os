/* ============================================================
 * @file keyboard_input.cpp
 * @brief 键盘输入解析层
 * 6×10 矩阵经 2× MCP23017 I2C 扫描（引脚见 drivers_config.hpp KBD_*）
 * ============================================================ */
#include "keyboard_input.hpp"
#include "input_queue.hpp"
#include "keyboard_driver.hpp"
#include "keyboard_keymap.hpp"
#include "backlight.hpp"
#include "esp_log.h"

namespace memoria::input {

namespace {

const char* TAG = "kbd_in";

bool s_shift = false;
bool s_caps = false;
bool s_caps_prev = false;
KeyHandler s_handler = nullptr;

void on_raw(const RawKeyEvent& evt) {
    /* 按下沿：触发键盘灯带打光 */
    if (evt.pressed) backlight_flash();

    uint16_t key = resolve_key(evt.row, evt.col, s_shift, s_caps);

    /* Shift / Caps 状态维护 */
    if (key == K_SHIFT) {
        s_shift = evt.pressed;
        return;
    }
    if (key == K_CAPSLOCK) {
        if (evt.pressed && !s_caps_prev) s_caps = !s_caps;
        s_caps_prev = evt.pressed;
        return;
    }

    /* 系统键分发（按下沿） */
    if (evt.pressed) {
        switch (key) {
            case K_BACKLIGHT:
                backlight_toggle();
                return;
            case K_ONOFF:
                ESP_LOGI(TAG, "ON/OFF key (side) pressed");
                return;   /* 电源管理后续接入 */
            case K_VOL_DN:
                ESP_LOGI(TAG, "VOL- (side)");
                return;
            case K_VOL_UP:
                ESP_LOGI(TAG, "VOL+ (side)");
                return;
            default:
                break;
        }
    }

    /* 字符键 / F1-F7 / 方向 / 回车 / 退格 → 系统输入队列 */
    input_queue_send(key, evt.pressed);

    /* 兼容钩子：如已注册 KeyHandler 也通知一份 */
    if (s_handler) s_handler(key, evt.pressed);
}

} /* namespace */

esp_err_t keyboard_input_init() {
    input_queue_init();
    keyboard_init(on_raw);
    keyboard_start();
    ESP_LOGI(TAG, "6x10 matrix keyboard ready (2x MCP23017)");
    return ESP_OK;
}

void keyboard_input_set_handler(KeyHandler h) {
    s_handler = h;
}

} /* namespace memoria::input */
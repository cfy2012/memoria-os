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
#include "event_bus.hpp"
#include "drivers_config.hpp"
#include "esp_log.h"

extern "C" {
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
}

namespace memoria::input {

namespace {

const char* TAG = "kbd_in";

/* ---------- 侧面 ON/OFF 独立键（GPIO2，不走矩阵） ----------
 * 深睡眠唤醒源（EXT0）+ 运行时长按 2s 关机。
 * 10K 上拉，一端接地：按下 = 低电平。
 * 触发一次后必须松开才能再触发（防按住连续关机）。 */
void side_key_task(void*) {
    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << drivers::WAKEUP_GPIO);
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io);

    uint32_t hold_ms = 0;
    bool triggered = false;
    for (;;) {
        const bool pressed = (gpio_get_level(static_cast<gpio_num_t>(drivers::WAKEUP_GPIO)) == 0);
        if (pressed) {
            hold_ms += 50;
            if (!triggered && hold_ms >= 2000) {
                ESP_LOGW(TAG, "side ON/OFF held 2s -> PowerOffRequest");
                kernel::EventBus::instance()->publish({kernel::EventType::PowerOffRequest});
                triggered = true;
            }
        } else {
            hold_ms = 0;
            triggered = false;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

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

    /* 侧面 ON/OFF 键长按检测任务（独立于矩阵扫描） */
    BaseType_t ok = xTaskCreatePinnedToCore(side_key_task, "side_key", 2048, nullptr, 2, nullptr, 1);
    if (ok != pdPASS) { ESP_LOGE(TAG, "side_key task create failed"); return ESP_ERR_NO_MEM; }

    ESP_LOGI(TAG, "6x10 matrix keyboard ready (2x MCP23017) + side key GPIO%d", drivers::WAKEUP_GPIO);
    return ESP_OK;
}

void keyboard_input_set_handler(KeyHandler h) {
    s_handler = h;
}

} /* namespace memoria::input */
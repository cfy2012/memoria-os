#pragma once
/* ============================================================
 * @file keyboard_input.hpp
 * @brief 键盘输入解析层
 *   驱动出 raw(row,col,pressed) → 本层维护 Shift/Caps 状态
 *   → resolve_key 出最终键码 → 特殊键分发（背光/电源等）
 *   → 其余键投递给注册的 KeyHandler（字符、F1-F7、方向、Enter…）
 * ============================================================ */
#include <cstdint>
#include "esp_err.h"

namespace memoria::input {

using KeyHandler = void (*)(uint16_t key, bool pressed);

/* 初始化 GPIO 矩阵驱动并启动扫描任务 */
esp_err_t keyboard_input_init();

/* 注册按键接收器（系统输入队列接入点） */
void keyboard_input_set_handler(KeyHandler h);

}
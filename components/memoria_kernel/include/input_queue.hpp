#pragma once
/* ============================================================
 * @file input_queue.hpp
 * @brief 系统输入队列
 *   键盘解析层产出最终键码 → 投递到这里
 *   文本类应用（BASIC/Shell/Notes/输入法）从此队列读键
 * ============================================================ */
#include <cstdint>
#include "esp_err.h"

namespace memoria {
namespace input {

struct InputEvent {
    uint16_t key;      /* 最终键码（含 Shift/Caps 处理后的字符 / K_* 特殊码） */
    bool     pressed;  /* true=按下  false=松开 */
};

/* 创建队列（32 深度），kernel 启动阶段调用一次 */
esp_err_t input_queue_init();

/* 投递按键（可带超时，默认 10ms） */
bool input_queue_send(uint16_t key, bool pressed, uint32_t timeout_ms = 10);

/* 消费者读键（阻塞式，默认永久等待；timeout_ms 可指定） */
bool input_queue_receive(InputEvent* ev, uint32_t timeout_ms = 0xFFFFFFFFUL);

} // namespace input
} // namespace memoria
/**
 * @file input_queue.cpp
 * @brief 系统输入队列：FreeRTOS Queue 封装
 *
 * 生产者：keyboard_input 解析层（非系统键全投递）
 * 消费者：文本类应用（BASIC 编辑器 / Shell / 记事本 / 拼音输入法）
 */

#include "input_queue.hpp"

extern "C" {
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_log.h>
}

namespace memoria {
namespace input {

static const char* TAG = "INPUTQ";

static QueueHandle_t s_queue = nullptr;
static constexpr uint8_t QUEUE_DEPTH = 32;

esp_err_t input_queue_init() {
    if (s_queue) return ESP_OK;
    s_queue = xQueueCreate(QUEUE_DEPTH, sizeof(InputEvent));
    if (!s_queue) {
        ESP_LOGE(TAG, "input queue create failed");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "input queue ready (%d deep)", QUEUE_DEPTH);
    return ESP_OK;
}

bool input_queue_send(uint16_t key, bool pressed, uint32_t timeout_ms) {
    if (!s_queue) return false;
    InputEvent ev{key, pressed};
    return xQueueSend(s_queue, &ev, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

bool input_queue_receive(InputEvent* ev, uint32_t timeout_ms) {
    if (!s_queue || !ev) return false;
    if (timeout_ms == 0xFFFFFFFFUL) {
        return xQueueReceive(s_queue, ev, portMAX_DELAY) == pdTRUE;
    }
    return xQueueReceive(s_queue, ev, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

} // namespace input
} // namespace memoria
/**
 * @file event_bus.hpp
 * @brief 全局事件总线：摇杆采样 → 窗口管理器 → 应用
 *
 * 极简 pub/sub：一个全局 EventBus 单例，摇杆采样任务 publish，
 * 窗口管理器 subscribe。避免每个驱动直接持有窗口管理器的引用。
 */

#pragma once

#include <functional>
#include <vector>
#include <cstdint>

extern "C" {
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
}

namespace memoria {
namespace kernel {

enum class EventType : uint8_t {
    JoystickNav,      /* NavEvent 方向事件 */
    JoystickEnter,    /* 摇杆按下 = 确认 */
    JoystickBack,     /* 摇杆长按(≥1.5s)抬起 = 返回 */
    BatteryWarn,
    BatteryCrit,
    WiFiConnected,
    WiFiDisconnected,
    BLEConnected,
    BLEDisconnected,
    AlarmFired,
};

struct Event {
    EventType type;
    uint8_t   nav_dir = 0;  /* JoystickNav 时有效 */
};

using EventHandler = std::function<void(const Event&)>;

/* 生产/消费分离：publish 只入队（任意任务安全），
 * 分发统一由 pump() 在主循环上下文执行。
 * 背景：WindowManager / 帧缓冲均无锁，若在采样任务里同步分发，
 * dispatch_nav 触发模式切换会与主循环 render_all 竞争窗口栈。 */
class EventBus {
public:
    static EventBus* instance() {
        static EventBus inst;
        return &inst;
    }

    void subscribe(EventHandler h) { _handlers.push_back(std::move(h)); }

    void publish(const Event& ev) {
        if (_queue) xQueueSend(_queue, &ev, 0);   /* 满则丢弃：导航事件无积压价值 */
    }

    /* 仅主循环调用：逐个取事件分发给订阅者 */
    void pump() {
        if (!_queue) return;
        Event ev;
        while (xQueueReceive(_queue, &ev, 0) == pdTRUE) {
            for (auto& h : _handlers) {
                if (h) h(ev);
            }
        }
    }

private:
    EventBus() { _queue = xQueueCreate(16, sizeof(Event)); }
    std::vector<EventHandler> _handlers;
    QueueHandle_t _queue = nullptr;
};

} // namespace kernel
} // namespace memoria
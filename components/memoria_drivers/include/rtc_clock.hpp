/**
 * @file rtc_clock.hpp
 * @brief ESP32-S3 内部 RTC：时间读写 + 闹钟（1 次性 / 周期性）
 *
 * 注意：ESP32-S3 没有外部 RTC 芯片，使用内部 RTC + esp_timer。
 *      时间通过 NTP 同步（WiFi 连上后自动）。
 *      闹钟通过 FreeRTOS 定时器触发回调。
 */

#pragma once

#include <esp_err.h>
#include <esp_timer.h>
#include <cstdint>
#include <ctime>
#include <functional>

namespace memoria {
namespace drivers {

using AlarmCb = std::function<void()>;

struct RtcTime {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint8_t  day_of_week;   /* 0=Sun .. 6=Sat */
};

class RtcClock {
public:
    static RtcClock* instance();
    esp_err_t init();

    /* 当前时间 */
    RtcTime now() const;
    uint32_t unix_now() const;

    /* 设置时间（Unix 秒） */
    void set_unix(uint32_t unix_ts);

    /* 闹钟：到点触发回调 */
    esp_err_t set_alarm(uint8_t hour, uint8_t minute, AlarmCb cb);
    void      cancel_alarm();
    bool      alarm_active() const { return _alarm_hour <= 23 && _alarm_min <= 59; }

    bool is_inited() const { return _inited; }

    /* NTP 同步（WiFi 连上后调用） */
    esp_err_t ntp_sync(const char* server = nullptr);

private:
    RtcClock() = default;
    static void _alarm_task_c(void* arg);
    esp_timer_handle_t _alarm_timer = nullptr;
    AlarmCb            _alarm_cb;
    uint8_t            _alarm_hour = 255;
    uint8_t            _alarm_min  = 255;
    bool               _alarm_fired = false;
    bool               _inited = false;
};

} // namespace drivers
} // namespace memoria
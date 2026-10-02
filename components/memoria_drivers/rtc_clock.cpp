/**
 * @file rtc_clock.cpp
 * @brief 内部 RTC：gettimeofday + esp_timer 闹钟 + NTP
 *
 * 时间来源：gettimeofday()（由 lwIP NTP 维护）
 * 闹钟：创建一个周期性 1 分钟定时器，检查当前时间是否匹配 alarm_hour:alarm_min
 */

#include "rtc_clock.hpp"

extern "C" {
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_sntp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sys/time.h>
}

namespace memoria {
namespace drivers {

static const char* TAG = "RTC";

RtcClock* RtcClock::instance() {
    static RtcClock inst;
    return &inst;
}

esp_err_t RtcClock::init() {
    if (_inited) return ESP_OK;
    ESP_LOGI(TAG, "RTC clock init (NTP ready after WiFi)");
    _inited = true;
    return ESP_OK;
}

RtcTime RtcClock::now() const {
    time_t t = time(nullptr);
    struct tm tm{};
    localtime_r(&t, &tm);
    RtcTime r{};
    r.year = static_cast<uint16_t>(tm.tm_year + 1900);
    r.month = static_cast<uint8_t>(tm.tm_mon + 1);
    r.day = static_cast<uint8_t>(tm.tm_mday);
    r.hour = static_cast<uint8_t>(tm.tm_hour);
    r.minute = static_cast<uint8_t>(tm.tm_min);
    r.second = static_cast<uint8_t>(tm.tm_sec);
    r.day_of_week = static_cast<uint8_t>(tm.tm_wday);
    return r;
}

uint32_t RtcClock::unix_now() const {
    return static_cast<uint32_t>(time(nullptr));
}

void RtcClock::set_unix(uint32_t unix_ts) {
    struct timeval tv{};
    tv.tv_sec = unix_ts;
    settimeofday(&tv, nullptr);
}

esp_err_t RtcClock::set_alarm(uint8_t hour, uint8_t minute, AlarmCb cb) {
    if (hour > 23 || minute > 59) return ESP_ERR_INVALID_ARG;
    _alarm_hour = hour;
    _alarm_min  = minute;
    _alarm_cb   = std::move(cb);
    _alarm_fired = false;

    /* 启动后台闹钟检查任务（1 分钟周期） */
    static BaseType_t started = 0;
    if (!started) {
        xTaskCreate(_alarm_task_c, "rtc_alarm", 2048, nullptr, 2, nullptr);
        started = 1;
    }
    ESP_LOGI(TAG, "Alarm set: %02u:%02u", hour, minute);
    return ESP_OK;
}

void RtcClock::cancel_alarm() {
    _alarm_hour = 255;
    _alarm_min  = 255;
    _alarm_cb   = nullptr;
    _alarm_fired = false;
}

void RtcClock::_alarm_task_c(void* arg) {
    (void)arg;
    static RtcClock* self = RtcClock::instance();
    while (true) {
        if (self->alarm_active() && !self->_alarm_fired) {
            RtcTime t = self->now();
            if (t.hour == self->_alarm_hour && t.minute == self->_alarm_min) {
                self->_alarm_fired = true;
                if (self->_alarm_cb) self->_alarm_cb();
                ESP_LOGI(TAG, "Alarm fired at %02u:%02u", t.hour, t.minute);
                self->cancel_alarm();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(30000));  /* 30s 检查一次足够精确 */
    }
}

esp_err_t RtcClock::ntp_sync(const char* server) {
    /* esp_sntp 初始化必须在 WiFi 连上之后 */
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    if (server) esp_sntp_setservername(0, server);
    else        esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    /* 等待同步（最多 15 秒） */
    for (int i = 0; i < 30; i++) {
        if (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            ESP_LOGI(TAG, "NTP synced: %lu", (unsigned long)unix_now());
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    ESP_LOGW(TAG, "NTP sync timeout");
    return ESP_ERR_TIMEOUT;
}

} // namespace drivers
} // namespace memoria
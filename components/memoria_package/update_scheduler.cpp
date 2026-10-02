/**
 * @file update_scheduler.cpp
 * @brief 夜间自动更新调度
 *
 * 后台 FreeRTOS 任务，每 30 秒检查一次 RTC 时间：
 *   1. 到点自动连 WiFi
 *   2. 检查更新（check_updates）
 *   3. 完成后保持 WiFi 还是断开
 *
 * NVS 配置：
 *   auto_update  (bool, 默认 true)
 *   auto_hour    (0-23, 默认 2)
 *   auto_min     (0-59, 默认 0)
 */

#include "update_scheduler.hpp"
#include "package_manager.hpp"
#include "wifi_manager.hpp"
#include "rtc_clock.hpp"

extern "C" {
#include <esp_log.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
}

namespace memoria {
namespace package {

static const char* TAG = "PKG_SCHED";

static BaseType_t s_started = 0;

void start_scheduler() {
    if (s_started) return;
    s_started = 1;

    xTaskCreate([](void*) {
        static bool fired_today[32] = {};   /* 当月已触发标记（按日去重） */
        static uint8_t last_month = 0;

        while (true) {
            bool auto_on = get_auto_update();
            auto t = drivers::RtcClock::instance()->now();

            /* 跨月清零 */
            if (t.month != last_month) {
                for (auto& f : fired_today) f = false;
                last_month = t.month;
            }

            if (auto_on && !fired_today[t.day]) {
                /* 到达设定时间窗口（±1 分钟）——窗口直接用配置值（不再硬编码凌晨） */
                uint8_t hh = get_auto_hour();
                uint8_t mm = get_auto_min();
                bool in_window = (t.hour == hh && t.minute >= mm && t.minute <= mm + 1);

                if (in_window) {
                    ESP_LOGI(TAG, "nightly update triggered (%02u:%02u)", t.hour, t.minute);
                    fired_today[t.day] = true;
                    run_nightly_update();
                }
            }

            vTaskDelay(pdMS_TO_TICKS(30000));  /* 30s */
        }
    }, "pkg_sched", 4096, nullptr, 2, nullptr);
}

int run_nightly_update() {
    auto* wifi = drivers::WifiManager::instance();

    /* 未连接 → 用保存凭据主动重连（到点自动连 WiFi），最多等 10s */
    if (!wifi->connected()) {
        esp_err_t rc = wifi->reconnect();
        if (rc != ESP_OK) {
            ESP_LOGW(TAG, "nightly: reconnect unavailable (%s) — skip", esp_err_to_name(rc));
            return 1;
        }
        for (int i = 0; i < 100 && !wifi->connected(); i++) vTaskDelay(pdMS_TO_TICKS(100));
        if (!wifi->connected()) {
            ESP_LOGW(TAG, "nightly: wifi connect timeout — skip");
            return 2;
        }
        ESP_LOGI(TAG, "nightly: wifi reconnected");
    }

    int added = 0, removed = 0;
    check_updates(&added, &removed);

    ESP_LOGI(TAG, "nightly done: +%d -%d", added, removed);
    return 0;
}

} }
/**
 * @file power.cpp
 * @brief LEDC PWM 背光驱动 + 空闲超时自动变暗
 */

#include "power.hpp"

extern "C" {
#include <driver/ledc.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
}

namespace memoria {
namespace drivers {

static const char* TAG = "POWER";

/* 用户/系统设定的背光值。渐暗过程只压 LEDC 输出、不改此值，
 * 否则触摸唤醒时会恢复到渐暗途中打穿的残值（约 6% 亮度 ≈ 全黑）。 */
static uint8_t s_user_brightness = 255;

Power* Power::instance() {
    static Power inst;
    return &inst;
}

esp_err_t Power::init() {
    if (_inited) return ESP_OK;

    /* LEDC 定时器：5kHz，8bit 精度 */
    ledc_timer_config_t tcfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = static_cast<ledc_timer_t>(LEDC_TIMER_BACKLIGHT),
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false,
    };
    ledc_timer_config(&tcfg);

    /* LEDC 通道：绑定 GPIO14 */
    ledc_channel_config_t ccfg = {
        .gpio_num = LCD_BLK_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = static_cast<ledc_channel_t>(LEDC_CHN_BACKLIGHT),
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = static_cast<ledc_timer_t>(LEDC_TIMER_BACKLIGHT),
        .duty = 255,
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags = { .output_invert = 0 },
        .deconfigure = false,
    };
    /* BOD 错峰：boot 期背光全灭。WiFi phy 上电校准是全 boot 最大电流峰
     * （160/240MHz 均实证复位循环，串口 E BOD 日志），半亮 80 仍会跌压，
     * 改为 0：由 wifi_manager 延迟任务在 phy 校准稳定后统一拉亮。 */
    ccfg.duty = 0;
    ledc_channel_config(&ccfg);

    _backlight = 0;
    _last_activity_ms = (esp_timer_get_time() / 1000);

    BaseType_t ok = xTaskCreate(_power_task_c, "power", 2048, this, 2, nullptr);
    if (ok != pdPASS) return ESP_ERR_NO_MEM;

    _inited = true;
    ESP_LOGI(TAG, "LEDC PWM OK. GPIO%d duty=0 (boot, wifi task raises to 255).", LCD_BLK_GPIO);
    return ESP_OK;
}

void Power::set_backlight(uint8_t brightness) {
    _backlight = brightness;
    s_user_brightness = brightness;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, static_cast<ledc_channel_t>(LEDC_CHN_BACKLIGHT), brightness);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, static_cast<ledc_channel_t>(LEDC_CHN_BACKLIGHT));
}

void Power::touch_event() {
    _last_activity_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    if (_dimmed) {
        _dimmed = false;
        set_backlight(s_user_brightness);   /* 恢复到设定亮度，而非渐暗残值 */
    }
}

uint32_t Power::idle_sec() const {
    /* 修：原实现返回毫秒差却被 _power_task_c 当秒与 _idle_timeout_sec 比较，
     * boot 后 ~1.4s 即满足"idle>60s"→ 背光灭（2916ms 灭屏实录）。 */
    uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    return (now - _last_activity_ms) / 1000;
}

void Power::deep_sleep() {
    set_backlight(0);
}

void Power::_power_task_c(void* arg) {
    auto* self = static_cast<Power*>(arg);
    while (true) {
        uint32_t idle = self->idle_sec();
        if (idle > self->_idle_timeout_sec && !self->_dimmed) {
            /* 分步渐暗：只压 LEDC 输出，不经过 set_backlight（保护 s_user_brightness） */
            for (int d = s_user_brightness; d >= 0; d -= 40) {
                ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, static_cast<uint32_t>(d));
                ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            self->_dimmed = true;
            ESP_LOGI(TAG, "idle timeout — backlight off");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

} // namespace drivers
} // namespace memoria
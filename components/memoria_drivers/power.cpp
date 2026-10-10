/**
 * @file power.cpp
 * @brief LEDC PWM 背光驱动 + 空闲超时自动变暗
 */

#include "power.hpp"

extern "C" {
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <nvs_flash.h>
#include <nvs.h>
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

/* ============ 深睡眠电源管理 ============ */

static const char* NVS_NS_PWR   = "pwr";
static const char* NVS_KEY_SHUT = "shutdown";

static void gpio_cfg_out(int pin, int level) {
    if (pin < 0) return;
    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << pin);
    io.mode = GPIO_MODE_OUTPUT;
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io);
    gpio_set_level(static_cast<gpio_num_t>(pin), level);
}

void Power::boot_defaults() {
    /* boot 最早：功放 EN 关（防滋啦）、次级 MOS 开（外围 5V 上电）、
     * WS2812 DIN 拉低（防 boot 闪灯，RMT 初始化前 GPIO46 悬空可能随机亮） */
    gpio_cfg_out(AMP_EN_GPIO, 0);
    gpio_cfg_out(SUBMOS_EN_GPIO, 0);   /* P-MOS：G 低 = 导通 = 外围 5V 供电 */
    gpio_cfg_out(KEY_LED_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(50));   /* 次级 MOS 打开后等 5V 轨稳定 */
    ESP_LOGI(TAG, "power boot defaults: AMP_EN=%d off, SUBMOS=%d on, LED_DIN low",
             AMP_EN_GPIO, SUBMOS_EN_GPIO);
}

void Power::amp_set_en(bool on) {
    gpio_set_level(static_cast<gpio_num_t>(AMP_EN_GPIO), on ? 1 : 0);
}

void Power::subm_rail(bool on) {
    /* 次级 MOS = AO3401 P-MOS（S=5V 输入，D=外围 5V 轨，G=GPIO48）：
     * G 低电平 = Vgs 负 = 导通（供电）；G 高电平 = 关断（深睡断电）。
     * 深睡时 GPIO48 输出高保持（S3 deep sleep 数字 IO 状态保留）。 */
    gpio_set_level(static_cast<gpio_num_t>(SUBMOS_EN_GPIO), on ? 0 : 1);
    ESP_LOGI(TAG, "SUB MOS rail %s (GPIO%d=%s)", on ? "ON (5V peripherals)" : "OFF (deep sleep)",
             SUBMOS_EN_GPIO, on ? "LOW" : "HIGH");
}

void Power::set_shutdown_flag() {
    nvs_handle_t h;
    if (nvs_open(NVS_NS_PWR, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, NVS_KEY_SHUT, 1);
        nvs_commit(h);
        nvs_close(h);
    }
}

void Power::clear_shutdown_flag() {
    nvs_handle_t h;
    if (nvs_open(NVS_NS_PWR, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, NVS_KEY_SHUT);
        nvs_commit(h);
        nvs_close(h);
    }
}

bool Power::shutdown_flag() const {
    nvs_handle_t h;
    uint8_t v = 0;
    if (nvs_open(NVS_NS_PWR, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, NVS_KEY_SHUT, &v);
        nvs_close(h);
    }
    return v == 1;
}

void Power::deep_sleep_now() {
    /* 侧面 ON/OFF 键（GPIO2 一端接地）拉低触发唤醒 */
    esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(WAKEUP_GPIO), 0);
    ESP_LOGW(TAG, "enter deep sleep (EXT0 wakeup on GPIO%d low)", WAKEUP_GPIO);
    esp_deep_sleep_start();   /* 不返回 */
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
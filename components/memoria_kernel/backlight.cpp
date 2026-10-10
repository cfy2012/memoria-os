/* ============================================================
 * @file backlight.cpp
 * @brief 键盘灯带控制（WS2812，单线 RMT）
 *
 * 硬件：GPIO46 接 WS2812 灯带 DIN；键盘区上下两条灯带。
 * 时序：800kHz，bit1 = 0.7us 高 + 0.6us 低，bit0 = 0.35us 高 + 0.8us 低。
 * 驱动：RMT 字节编码器（MSB first，GRB 排列）。
 * ============================================================ */
#include "backlight.hpp"
#include "drivers_config.hpp"

#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstring>

namespace memoria::input {

namespace {

const char* TAG = "backlight";

/* 灯带布局：键盘上边沿 + 下边沿，每条 LEDS_PER_STRIP 颗（可按灯带实际长度调整） */
constexpr size_t LEDS_PER_STRIP = 6;
constexpr size_t LEDS_TOTAL     = LEDS_PER_STRIP * 2;
constexpr size_t CHANNELS       = 3;   /* WS2812 GRB */

rmt_channel_handle_t s_channel = nullptr;
rmt_encoder_handle_t s_encoder = nullptr;
uint8_t s_frame[LEDS_TOTAL * CHANNELS] = {0};   /* 当前档位帧 */
esp_timer_handle_t s_flash_timer = nullptr;

/* 档位：关 / 低亮暖白 / 高亮暖白 */
enum : int { LEVEL_OFF = 0, LEVEL_LOW, LEVEL_HIGH, LEVEL_COUNT };
int s_level = LEVEL_OFF;
bool s_inited = false;
bool s_flashing = false;

/* 把 RGB 写入帧（GRB 排列 + 按 idx 分发到两条灯带） */
void frame_set(size_t led, uint8_t r, uint8_t g, uint8_t b) {
    if (led >= LEDS_TOTAL) return;
    uint8_t* p = s_frame + led * CHANNELS;
    p[0] = g; p[1] = r; p[2] = b;
}

esp_err_t frame_send() {
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    rmt_transmit_config_t cfg = {};
    cfg.loop_count = 0;
    esp_err_t ret = rmt_transmit(s_channel, s_encoder, s_frame, sizeof(s_frame), &cfg);
    if (ret == ESP_OK) ret = rmt_tx_wait_all_done(s_channel, 100);
    return ret;
}

void apply_level() {
    if (!s_inited) return;
    switch (s_level) {
        case LEVEL_OFF:
            std::memset(s_frame, 0, sizeof(s_frame));
            break;
        case LEVEL_LOW:
            for (size_t i = 0; i < LEDS_TOTAL; ++i) frame_set(i, 24, 16, 8);
            break;
        case LEVEL_HIGH:
            for (size_t i = 0; i < LEDS_TOTAL; ++i) frame_set(i, 255, 220, 140);
            break;
        default:
            break;
    }
    frame_send();
}

/* flash 定时器到期：恢复当前档位 */
void flash_timeout(void*) {
    s_flashing = false;
    apply_level();
}

} /* namespace */

esp_err_t backlight_init() {
    rmt_tx_channel_config_t ch_cfg = {};
    ch_cfg.gpio_num       = static_cast<gpio_num_t>(drivers::KEY_LED_GPIO);
    ch_cfg.clk_src        = RMT_CLK_SRC_DEFAULT;
    ch_cfg.resolution_hz  = 10 * 1000 * 1000;        /* 10MHz → 0.1us/tick */
    ch_cfg.mem_block_symbols = 64;
    ch_cfg.trans_queue_depth = 4;
    ch_cfg.flags.invert_out = false;
    esp_err_t ret = rmt_new_tx_channel(&ch_cfg, &s_channel);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "rmt channel: %s", esp_err_to_name(ret)); return ret; }

    rmt_bytes_encoder_config_t enc_cfg = {};
    enc_cfg.bit0.duration0 = 350;   /* 0.35us 高 */
    enc_cfg.bit0.level0    = 1;
    enc_cfg.bit0.duration1 = 800;   /* 0.8us 低 */
    enc_cfg.bit0.level1    = 0;
    enc_cfg.bit1.duration0 = 700;   /* 0.7us 高 */
    enc_cfg.bit1.level1    = 0;
    enc_cfg.bit1.duration1 = 600;   /* 0.6us 低 */
    enc_cfg.bit1.level0    = 1;
    enc_cfg.flags.msb_first = true;
    ret = rmt_new_bytes_encoder(&enc_cfg, &s_encoder);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "rmt encoder: %s", esp_err_to_name(ret)); return ret; }

    ret = rmt_enable(s_channel);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "rmt enable: %s", esp_err_to_name(ret)); return ret; }

    /* flash 定时器 */
    esp_timer_create_args_t targs = {};
    targs.callback = flash_timeout;
    targs.name     = "led_flash";
    ret = esp_timer_create(&targs, &s_flash_timer);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "flash timer: %s", esp_err_to_name(ret)); return ret; }

    s_inited = true;
    s_level = LEVEL_OFF;
    apply_level();
    ESP_LOGI(TAG, "WS2812 strip ready: %d LEDs on GPIO %d", (int)LEDS_TOTAL, drivers::KEY_LED_GPIO);
    return ESP_OK;
}

void backlight_toggle() {
    if (!s_inited) return;
    s_level = (s_level + 1) % LEVEL_COUNT;
    apply_level();
    ESP_LOGI(TAG, "strip level %d/%d", s_level, LEVEL_COUNT - 1);
}

void backlight_off() {
    if (!s_inited) return;
    esp_timer_stop(s_flash_timer);   /* 停打光定时器（防 120ms 后又亮） */
    s_flashing = false;
    s_level = LEVEL_OFF;
    apply_level();
}

void backlight_flash() {
    if (!s_inited) return;
    /* 全亮打光 120ms */
    for (size_t i = 0; i < LEDS_TOTAL; ++i) frame_set(i, 255, 255, 255);
    frame_send();
    s_flashing = true;
    esp_timer_stop(s_flash_timer);
    esp_timer_start_once(s_flash_timer, 120 * 1000);
}

bool backlight_on() {
    return s_inited && s_level != LEVEL_OFF;
}

} /* namespace memoria::input */
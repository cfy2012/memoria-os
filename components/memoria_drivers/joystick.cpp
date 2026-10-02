/**
 * @file joystick.cpp
 * @brief KY-023 摇杆：ADC ONE_SHOT 采样 + 消抖 + 方向识别
 */

#include "joystick.hpp"
#include "adc_share.hpp"

extern "C" {
#include <esp_adc/adc_oneshot.h>   /* esp_adc ONE_SHOT API */
#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
}

#include <cmath>

namespace memoria {
namespace drivers {

static const char* TAG = "JOYSTICK";

Joystick* Joystick::instance() {
    static Joystick inst;
    return &inst;
}

esp_err_t Joystick::init() {
    if (_inited) return ESP_OK;

    /* 共享 ADC1 unit：电池采样同用一把句柄，禁止重复 new_unit */
    adc_oneshot_unit_handle_t handle = adc1_shared_unit();
    if (!handle) { ESP_LOGE(TAG, "adc1 shared unit unavailable"); return ESP_FAIL; }

    /* 保存 handle 到 class 成员（而非 static：更干净） */
    static adc_oneshot_unit_handle_t s_handle = handle;

    adc_oneshot_chan_cfg_t chan = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_oneshot_config_channel(handle, static_cast<adc_channel_t>(JOY_X_ADC_CHANNEL), &chan);
    adc_oneshot_config_channel(handle, static_cast<adc_channel_t>(JOY_Y_ADC_CHANNEL), &chan);

    /* BTN GPIO：输入，内部上拉 */
    gpio_config_t io{};
    io.pin_bit_mask = (1ULL << JOY_BTN_GPIO);
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io);

    /* 启动采样任务（直接传 handle 指针） */
    BaseType_t ok = xTaskCreate([](void* arg) {
        adc_oneshot_unit_handle_t h = *static_cast<adc_oneshot_unit_handle_t*>(arg);
        static Joystick* self = Joystick::instance();

        constexpr int SAMPLES = 5;
        while (true) {
            int32_t x_sum = 0, y_sum = 0;
            for (int i = 0; i < SAMPLES; i++) {
                int raw_x = 0, raw_y = 0;
                adc_oneshot_read(h, static_cast<adc_channel_t>(JOY_X_ADC_CHANNEL), &raw_x);
                adc_oneshot_read(h, static_cast<adc_channel_t>(JOY_Y_ADC_CHANNEL), &raw_y);
                x_sum += raw_x; y_sum += raw_y;
                vTaskDelay(pdMS_TO_TICKS(2));
            }
            int32_t x_avg = x_sum / SAMPLES;
            int32_t y_avg = y_sum / SAMPLES;

            int32_t x_delta = x_avg - 2048;
            int32_t y_delta = y_avg - 2048;
            bool btn_now = (gpio_get_level(static_cast<gpio_num_t>(JOY_BTN_GPIO)) == 0);

            JoystickDir dir = JoystickDir::Center;
            if (std::abs(x_delta) > 512 || std::abs(y_delta) > 512) {
                if (std::abs(x_delta) > std::abs(y_delta))
                    dir = x_delta < 0 ? JoystickDir::Left : JoystickDir::Right;
                else
                    dir = y_delta < 0 ? JoystickDir::Up   : JoystickDir::Down;
            }

            bool changed = (dir != self->_last_dir) || (btn_now != self->_last_btn);
            self->_last_x = x_avg; self->_last_y = y_avg;
            self->_last_dir = dir; self->_last_btn = btn_now;

            if (changed && self->_cb) self->_cb(dir, btn_now);
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }, "joystick", 3072, &s_handle, 4, nullptr);

    if (ok != pdPASS) return ESP_ERR_NO_MEM;

    _inited = true;
    ESP_LOGI(TAG, "KY-023 OK. X=%d Y=%d BTN=%d", JOY_X_GPIO, JOY_Y_GPIO, JOY_BTN_GPIO);
    return ESP_OK;
}

void Joystick::read(int32_t& x, int32_t& y, bool& btn) const {
    x = _last_x; y = _last_y; btn = _last_btn;
}

JoystickDir Joystick::direction() const { return _last_dir; }

} // namespace drivers
} // namespace memoria
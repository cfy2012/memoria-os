/**
 * @file battery_adc.cpp
 * @brief 电池 ADC 采样：ONE_SHOT 模式读取分压电压
 */

#include "battery_adc.hpp"
#include "adc_share.hpp"

extern "C" {
#include <esp_adc/adc_oneshot.h>   /* esp_adc ONE_SHOT API */
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
}

#include <algorithm>

namespace memoria {
namespace drivers {

static const char* TAG = "BATTERY";

BatteryAdc* BatteryAdc::instance() {
    static BatteryAdc inst;
    return &inst;
}

esp_err_t BatteryAdc::init() {
    if (_inited) return ESP_OK;

    /* 共享 ADC1 unit：摇杆采样同用一把句柄，禁止重复 new_unit */
    adc_oneshot_unit_handle_t handle = adc1_shared_unit();
    if (!handle) { ESP_LOGE(TAG, "adc1 shared unit unavailable"); return ESP_FAIL; }

    adc_oneshot_chan_cfg_t chan = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    adc_oneshot_config_channel(handle, static_cast<adc_channel_t>(BATTERY_ADC_CHANNEL), &chan);

    static adc_oneshot_unit_handle_t s_handle = handle;

    BaseType_t ok = xTaskCreate([](void* arg) {
        adc_oneshot_unit_handle_t h = *static_cast<adc_oneshot_unit_handle_t*>(arg);
        static BatteryAdc* self = BatteryAdc::instance();

        while (true) {
            int32_t sum = 0;
            constexpr int N = 20;
            for (int i = 0; i < N; i++) {
                int raw = 0;
                adc_oneshot_read(h, static_cast<adc_channel_t>(BATTERY_ADC_CHANNEL), &raw);
                sum += raw;
                vTaskDelay(pdMS_TO_TICKS(5));
            }
            int32_t avg = sum / N;
            float vadc_mv = (static_cast<float>(avg) / 4096.0f) * 3300.0f;
            float vbat_mv = vadc_mv * BATTERY_DIV_RATIO;
            float vbat = vbat_mv / 1000.0f;
            self->_voltage = vbat;

            float pct = (vbat - BATTERY_VOLTAGE_EMPTY)
                      / (BATTERY_VOLTAGE_FULL - BATTERY_VOLTAGE_EMPTY) * 100.0f;
            pct = std::clamp(pct, 0.0f, 100.0f);
            self->_percent = static_cast<uint8_t>(pct);

            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }, "battery", 2048, &s_handle, 3, nullptr);

    if (ok != pdPASS) return ESP_ERR_NO_MEM;

    _inited = true;
    ESP_LOGI(TAG, "Battery ADC OK. GPIO%d R=200K+200K.", BATTERY_GPIO);
    return ESP_OK;
}

float BatteryAdc::voltage() const { return _voltage; }
uint8_t BatteryAdc::percent() const { return _percent; }

} // namespace drivers
} // namespace memoria
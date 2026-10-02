/* ============================================================
 * @file adc_share.hpp
 * @brief ADC1 oneshot 单元共享句柄
 *
 * IDF 的 adc_oneshot_new_unit 对同一单元仅允许申请一次，
 * 二次申请返回 ESP_ERR_NOT_FOUND。摇杆与电池采样必须共用本句柄，
 * 禁止各自 new_unit。
 * ============================================================ */
#pragma once

#include <esp_adc/adc_oneshot.h>

namespace memoria {
namespace drivers {

/* 取 ADC1 共享 oneshot 句柄（首次调用创建，失败返回 nullptr）
 * 仅允许在主任务串行初始化阶段调用 */
inline adc_oneshot_unit_handle_t adc1_shared_unit() {
    static adc_oneshot_unit_handle_t s_unit = [] {
        adc_oneshot_unit_handle_t h = nullptr;
        adc_oneshot_unit_init_cfg_t cfg = {
            .unit_id = ADC_UNIT_1,
            .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };
        adc_oneshot_new_unit(&cfg, &h);
        return h;
    }();
    return s_unit;
}

} // namespace drivers
} // namespace memoria

/**
 * @file main.cpp
 * @brief Memoria OS：extern "C" app_main() 入口
 *
 * 唯一职责：调用 Kernel::init() 完成启动序列，然后进入 main_loop() 永不返回。
 */

extern "C" {
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
}

#include "kernel.hpp"

extern "C" void app_main() {
    using namespace memoria::kernel;

    Kernel* k = Kernel::instance();

    /* 启动内核（包含所有驱动/FS/窗口/应用的初始化） */
    esp_err_t ret = k->init();
    if (ret != ESP_OK) {
        /* 启动失败：串口 + 红屏错误码（LCD 未就绪时仅串口），死循环保持现场 */
        k->show_boot_failure(ret);
        for (;;) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    /* 进入主循环（永不返回） */
    k->main_loop();
}
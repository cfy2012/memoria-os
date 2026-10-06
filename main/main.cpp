/**
 * @file main.cpp
 * @brief Memoria OS：extern "C" app_main() 入口
 *
 * 唯一职责：调用 Kernel::init() 完成启动序列，然后进入 main_loop() 永不返回。
 * 串口 REPL 后台任务提供 shell 命令行（调试口 + rec 录音入口）。
 */

extern "C" {
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
}

#include "kernel.hpp"
#include "shell.hpp"

namespace {

/* 串口 REPL：最低优先级，getchar 阻塞读让出 CPU，仅调试与录音入口 */
void shell_task(void*) {
    static memoria::shell::Shell s_shell;
    s_shell.menu();   /* 永不返回 */
}

} /* namespace */

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

    /* 串口 shell 后台任务（核 0，避免抢占主循环所在核） */
    xTaskCreatePinnedToCore(shell_task, "shell", 4096, nullptr, 1, nullptr, 0);

    /* 进入主循环（永不返回） */
    k->main_loop();
}
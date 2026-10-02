/**
 * @file kernel.hpp
 * @brief Memoria OS 自研内核：启动流程 + 主循环 + 事件桥接 + 后台任务
 *
 * 启动序列（严格顺序）：
 *   1.  NVS Flash 初始化
 *   2.  SPI 共享总线初始化
 *   3.  ILI9488 屏幕初始化 + PSRAM framebuffer 分配
 *   4.  Power 电源/背光初始化
 *   5.  SD 卡 SPI 挂载（失败仅记录，不阻断）
 *   6.  Battery 电池 ADC 采样启动
 *   7.  KY-023 摇杆采样启动 + 状态变化回调注册
 *   8.  RTC 时钟初始化
 *   9.  I2S 音频输出初始化
 *  10.  WiFi STA 初始化 + 自动连接保存的 SSID（失败仅记录）
 *  11.  NimBLE BLE 外设初始化 + 开始广播（失败仅记录）
 *  12.  WindowManager + StatusBar 初始化
 *  13.  Private FS 挂载 / FAT 同步扫描
 *  14.  注册全部模式（卡西欧范式主菜单）+ 启动 Launcher
 *  15.  订阅 EventBus：摇杆/电池/WiFi/BLE/闹钟 → WindowManager
 *  16.  夜间自动更新调度器（后台低优先级任务）
 *  17.  矩阵键盘 + 背光灯带 + 拼音输入法
 *  主循环：键盘输入队列 → 模式分发；窗口渲染 + 状态栏刷新（20ms 周期）
 */

#pragma once

#include <esp_err.h>
#include <esp_log.h>
#include <cstdint>
#include <atomic>

/* 启动步骤错误检查：
 * 表达式返回 esp_err_t，非 ESP_OK 时记录日志并终止启动序列。
 * 用法：MEMORIA_CHECK(drivers::SpiBus::instance()->init()); */
#define MEMORIA_CHECK(expr)                                                          \
    do {                                                                             \
        esp_err_t _err_ = (expr);                                                    \
        if (_err_ != ESP_OK) {                                                       \
            ESP_LOGE("KERNEL", "init failed: " #expr " -> %s", esp_err_to_name(_err_)); \
            return _err_;                                                            \
        }                                                                            \
    } while (0)

namespace memoria {
namespace kernel {

class Kernel {
public:
    static Kernel* instance();

    esp_err_t init();       /* 完整启动序列 */
    void show_boot_failure(esp_err_t ret);  /* 启动失败上屏：LCD 未就绪时仅串口 */
    uint64_t  uptime_ms() const;
    bool      ready() const { return _ready.load(); }
    void      reboot();

    /* 主循环（必须在 app_main 里调用，永不返回） */
    void main_loop();

    /* 供后台驱动/事件回调触发 */
    void mark_ready() { _ready.store(true); }

private:
    Kernel() = default;
    static void _joystick_cb_entry(void* self);

    std::atomic<bool> _ready{false};
};

} // namespace kernel
} // namespace memoria
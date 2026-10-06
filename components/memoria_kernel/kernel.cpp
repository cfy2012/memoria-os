/**
 * @file kernel.cpp
 * @brief Memoria OS 启动流程 + 主循环 + 摇杆事件桥接
 *
 * 每一步都有错误处理：如果某个驱动 init 失败，记录 ERR 日志但不崩溃
 * （部分驱动如 WiFi/BLE 允许后续再连接）。
 */

#include "kernel.hpp"
#include "event_bus.hpp"
#include "app_manager.hpp"

#include "spi_bus.hpp"
#include "ili9341.hpp"
#include "sdcard.hpp"
#include "power.hpp"
#include "i2s_audio.hpp"
#include "joystick.hpp"
#include "battery_adc.hpp"
#include "rtc_clock.hpp"
#include "wifi_manager.hpp"
#include "ble_manager.hpp"
#include "window.hpp"
#include "gb2312_font.hpp"

#include "private_fs.hpp"
#include "fat_scanner.hpp"
#include "update_scheduler.hpp"
#include "mode_manager.hpp"
#include "keyboard_input.hpp"
#include "backlight.hpp"
#include "input_queue.hpp"
#include "ime.hpp"

extern "C" {
#include <nvs_flash.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
}

#include <cstdio>

#include "system_info.h"

namespace memoria {
namespace kernel {

static const char* TAG = "KERNEL";

Kernel* Kernel::instance() {
    static Kernel inst;
    return &inst;
}

/* ---------- 摇杆状态回调：桥接到 EventBus ----------
 * 长按(≥1.5s)后抬起 → JoystickBack（返回上一级/退出模式） */
void Kernel::_joystick_cb_entry(void* self) {
    (void)self;
    static int64_t btn_down_ms = 0;
    drivers::JoystickCb cb = [](drivers::JoystickDir dir, bool btn) {
        Event ev;
        if (btn) {
            btn_down_ms = esp_timer_get_time() / 1000;
            ev.type = EventType::JoystickEnter;
            EventBus::instance()->publish(ev);
            /* 触发 Power 触摸事件（重置背光超时） */
            drivers::Power::instance()->touch_event();
            return;
        }
        /* 抬起：先判断是否为长按返回 */
        int64_t hold_ms = esp_timer_get_time() / 1000 - btn_down_ms;
        if (hold_ms >= 1500) {
            ev.type = EventType::JoystickBack;
            EventBus::instance()->publish(ev);
            drivers::Power::instance()->touch_event();
            return;
        }
        switch (dir) {
            case drivers::JoystickDir::Up:    ev.nav_dir = 1; break;
            case drivers::JoystickDir::Down:  ev.nav_dir = 2; break;
            case drivers::JoystickDir::Left:  ev.nav_dir = 3; break;
            case drivers::JoystickDir::Right: ev.nav_dir = 4; break;
            default: return;
        }
        ev.type = EventType::JoystickNav;
        EventBus::instance()->publish(ev);
        drivers::Power::instance()->touch_event();
    };
    drivers::Joystick::instance()->set_callback(std::move(cb));
}

void Kernel::show_boot_failure(esp_err_t ret) {
    /* 启动失败可视化：LCD 已就绪（init 序列第 3 步之后失败）则红屏显示错误码；
     * 不用中文字库（TF 可能是失败源），纯 ASCII。未就绪时仅串口。 */
    ESP_LOGE(TAG, "boot failed: %s (0x%x)", esp_err_to_name(ret), ret);
    auto* lcd = drivers::Ili9341::instance();
    if (!lcd->framebuffer()) return;
    auto* ui = window::UIRenderer::instance();
    if (!ui) return;
    ui->fill_rect(window::Rect{0, 0, static_cast<int16_t>(drivers::SCREEN_W),
                               static_cast<int16_t>(drivers::SCREEN_H)}, drivers::COLOR_RED);
    char msg[48];
    snprintf(msg, sizeof(msg), "BOOT FAIL 0x%x", static_cast<unsigned>(ret));
    ui->draw_text(16, 150, msg, drivers::COLOR_WHITE);
    lcd->flush();
}

esp_err_t Kernel::init() {
    ESP_LOGI(TAG, "========== Memoria OS Boot ==========");
    ESP_LOGI(TAG, "Build: %s %s", MEMORIA_OS_NAME, MEMORIA_OS_VERSION);

    /* 1. NVS Flash */
    {
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            nvs_flash_erase();
            ret = nvs_flash_init();
        }
        if (ret != ESP_OK) { ESP_LOGE(TAG, "NVS: %s", esp_err_to_name(ret)); return ret; }
        ESP_LOGI(TAG, "NVS OK");
    }

    /* 2. SPI 总线 */
    MEMORIA_CHECK(drivers::SpiBus::instance()->init());

    /* 3. ILI9341 */
    MEMORIA_CHECK(drivers::Ili9341::instance()->init());
    drivers::Ili9341::instance()->fill(drivers::COLOR_BLACK);
    drivers::Ili9341::instance()->flush();

    /* 4. Power（LEDC PWM 背光 + 空闲检测） */
    MEMORIA_CHECK(drivers::Power::instance()->init());

    /* 5. SD 卡 */
    {
        esp_err_t ret = drivers::SdCard::instance()->init();
        ESP_LOGI(TAG, "SD: %s", esp_err_to_name(ret));
    }

    /* 6. Battery ADC（后台采样任务） */
    MEMORIA_CHECK(drivers::BatteryAdc::instance()->init());

    /* 7. KY-023 摇杆 + 回调注册 */
    MEMORIA_CHECK(drivers::Joystick::instance()->init());
    _joystick_cb_entry(this);
    ESP_LOGI(TAG, "Joystick callback OK");

    /* 8. RTC 时钟（WiFi 连上后会自动 NTP 同步） */
    MEMORIA_CHECK(drivers::RtcClock::instance()->init());

    /* 9. I2S 音频 */
    MEMORIA_CHECK(drivers::I2sAudio::instance()->init());

    /* 10. WiFi STA（按需射频：boot 期只注册管理器不开射频，WIFISTAT/wifi 语句触发 start_rf） */
    {
        esp_err_t ret = drivers::WifiManager::instance()->init();
        ESP_LOGI(TAG, "WiFi: %s (RF on-demand)", esp_err_to_name(ret));
    }

    /* 11. BLE 外设（按需启动：设置页蓝牙开关 / BLE 语句触发，boot 期不启动） */
    ESP_LOGI(TAG, "BLE: on-demand (not started at boot)");

    /* 12. WindowManager */
    MEMORIA_CHECK(window::WindowManager::instance()->init());

    /* 13. Private FS + FAT 同步扫描 */
    {
        esp_err_t ret = fs::PrivateFs::instance()->init();
        ESP_LOGI(TAG, "Private FS: %s", esp_err_to_name(ret));
        if (ret == ESP_OK && drivers::SdCard::instance()->is_mounted()) {
            int added = 0, removed = 0;
            fs::FatScanner::instance()->scan_and_sync(&added, &removed);
            ESP_LOGI(TAG, "FAT sync: +%d -%d", added, removed);
        }
    }

    /* 13b. GB2312 字库：TF 卡 bin 读入 PSRAM（缺卡/缺文件时中文降级为方框） */
    {
        bool font_ok = false;
        if (drivers::SdCard::instance()->is_mounted()) {
            std::string p = drivers::SdCard::instance()->mount_point() + "/font/gb2312_16.bin";
            font_ok = gb2312_font_load(p.c_str());
        }
        ESP_LOGI(TAG, "GB2312 font: %s", font_ok ? "loaded from TF (256KB)" : "MISSING, CJK fallback");
    }

    /* 14. 注册全部模式（卡西欧范式主菜单） + 启动 Launcher */
    modes::memoria_modes_register_all();
    modes::ModeManager::instance()->boot();
    ESP_LOGI(TAG, "Mode launcher booted, %d modes", modes::ModeManager::instance()->count());

    /* 15. 订阅 EventBus：摇杆事件 → WindowManager */
    {
        EventBus::instance()->subscribe([](const Event& ev) {
            window::NavInput ni;
            if (ev.type == EventType::JoystickEnter) {
                ni.dir = window::NavEvent::NavEnter;
                window::WindowManager::instance()->dispatch_nav(ni);
            } else if (ev.type == EventType::JoystickBack) {
                ni.dir = window::NavEvent::NavBack;
                window::WindowManager::instance()->dispatch_nav(ni);
            } else if (ev.type == EventType::JoystickNav) {
                switch (ev.nav_dir) {
                    case 1: ni.dir = window::NavEvent::NavUp;    break;
                    case 2: ni.dir = window::NavEvent::NavDown;  break;
                    case 3: ni.dir = window::NavEvent::NavLeft;  break;
                    case 4: ni.dir = window::NavEvent::NavRight; break;
                    default: return;
                }
                window::WindowManager::instance()->dispatch_nav(ni);
            } else if (ev.type == EventType::BatteryCrit) {
                window::WindowManager::instance()->dispatch_sys(window::SysEvent::BatteryCrit);
            } else if (ev.type == EventType::BatteryWarn) {
                window::WindowManager::instance()->dispatch_sys(window::SysEvent::BatteryWarn);
            } else if (ev.type == EventType::WiFiConnected) {
                window::WindowManager::instance()->dispatch_sys(window::SysEvent::WiFiConnected);
            } else if (ev.type == EventType::WiFiDisconnected) {
                window::WindowManager::instance()->dispatch_sys(window::SysEvent::WiFiDisconnected);
            } else if (ev.type == EventType::BLEConnected) {
                window::WindowManager::instance()->dispatch_sys(window::SysEvent::BLEConnected);
            } else if (ev.type == EventType::BLEDisconnected) {
                window::WindowManager::instance()->dispatch_sys(window::SysEvent::BLEDisconnected);
            } else if (ev.type == EventType::AlarmFired) {
                window::WindowManager::instance()->dispatch_sys(window::SysEvent::AlarmFired);
            }
        });
    }

    /* 16. 夜间自动更新调度器（后台低优先级任务） */
    package::start_scheduler();
    ESP_LOGI(TAG, "Package scheduler OK");

    /* 17. 矩阵键盘（2× MCP23017 I2C 扫描，硬件定版将换 TCA8418） + 背光灯带 + 拼音输入法 */
    MEMORIA_CHECK(input::keyboard_input_init());
    MEMORIA_CHECK(input::backlight_init());
    ime::ime_init();
    ESP_LOGI(TAG, "Keyboard + LED strip OK");

    _ready.store(true);
    ESP_LOGI(TAG, "========== Boot complete ==========");

    /* 背光：boot 期全灭（power init duty=0），boot complete 拉回全亮
     * （boot 期已零射频无大电流峰，拉亮安全） */
    drivers::Power::instance()->set_backlight(255);
    return ESP_OK;
}

/* ---------- 主循环：渲染 + 状态栏刷新 ---------- */
void Kernel::main_loop() {
    auto* wm = window::WindowManager::instance();
    uint32_t tick = 0;

    while (true) {
        /* EventBus 泵：joystick/电池任务只入队，导航与系统事件在此统一分发 */
        EventBus::instance()->pump();

        /* 键盘输入队列 → 当前模式（每 20ms 轮询一次） */
        input::InputEvent kev;
        while (input::input_queue_receive(&kev, 0)) {
            modes::ModeManager::instance()->dispatch_key(kev.key, kev.pressed);
        }

        /* SD 热插拔轮询（约每 1s 一次：20ms × 50） */
        if (++tick % 50 == 0) {
            drivers::SdCard::instance()->poll();
        }

        /* 渲染所有窗口 + 状态栏每 1s 刷新 */
        wm->render_all();
        /* StatusBar 在 render_all 内作为底层窗口自动刷新 */
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

uint64_t Kernel::uptime_ms() const {
    return static_cast<uint64_t>(esp_timer_get_time() / 1000);
}

void Kernel::reboot() {
    ESP_LOGW(TAG, "reboot requested");
    esp_restart();
}

} // namespace kernel
} // namespace memoria
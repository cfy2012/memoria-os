/**
 * @file sys_mode.cpp
 * @brief SYS 系统模式
 *
 * 概览 + 系统信息 / 内存 / 自检动画 / 三层恢复出厂 / 关于。
 */

#include "mode_manager.hpp"
#include "battery_adc.hpp"
#include "private_fs.hpp"
#include "joystick.hpp"
#include "system_info.h"

extern "C" {
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
}

#include <cstdio>

namespace memoria {
using namespace drivers;
namespace modes {

class SysMode : public ModeWindow {
public:
    SysMode() {}

    void mode_render(window::UIRenderer* ui, bool focused) override {
        draw_title(ui, "SYS 系统", std::to_string(_tab + 1).append("/5").c_str());
        switch (_tab) {
            case 0: _render_overview(ui); break;
            case 1: _render_info(ui); break;
            case 2: _render_mem(ui); break;
            case 3: _render_test(ui); break;
            case 4: _render_about(ui); break;
        }
    }

    bool mode_nav(const window::NavInput& ni) override {
        switch (ni.dir) {
            case window::NavEvent::NavUp:    _tab = (_tab - 1 + 5) % 5; break;
            case window::NavEvent::NavDown:  _tab = (_tab + 1) % 5; break;
            case window::NavEvent::NavEnter:
                if (_tab == 3) { _test_step++; if (_test_step > 3) _test_step = 0; }
                if (_tab == 4) request_confirm("恢复出厂? 此操作将清空记事与照片");
                break;
            default: return false;
        }
        return true;
    }

    const char* const* fn_labels() override {
        static const char* f[6] = {"信息", "内存", "自检", "恢复", "关于", "菜单"};
        return f;
    }

    void on_fn(int idx) override {
        switch (idx) {
            case 0: _tab = 1; break;
            case 1: _tab = 2; break;
            case 2: _tab = 3; _test_step = 0; break;
            case 3: request_confirm("恢复出厂? 此操作将清空记事与照片"); break;
            case 4: _tab = 4; break;
            case 5: break;
        }
    }

    void on_confirm_ok() override {
        if (_factory_armed) {
            /* 第三层确认后执行 */
            if (fs::PrivateFs::instance()->is_inited()) fs::PrivateFs::instance()->format();
            _factory_armed = false;
            _tab = 0;
        } else {
            _factory_armed = true;
            request_confirm("再次确认: 清空所有数据?");
        }
    }

private:
    void _render_overview(window::UIRenderer* ui) {
        char line[40];
        snprintf(line, sizeof(line), "%s  v%s", MEMORIA_OS_NAME, MEMORIA_OS_VERSION);
        ui->draw_text(6, 40, line, COLOR_WHITE);
        uint32_t heap = esp_get_free_heap_size() / 1024;
        snprintf(line, sizeof(line), "内存 %u KB  电池 %d%%", (unsigned)heap, drivers::BatteryAdc::instance()->percent());
        ui->draw_text(6, 56, line, COLOR_LIGHT_GRAY);
        snprintf(line, sizeof(line), "运行 %d 秒", (int)(esp_timer_get_time() / 1000000));
        ui->draw_text(6, 72, line, COLOR_LIGHT_GRAY);
        draw_hint(ui, "↑↓ 切页 · Enter 自检/恢复");
    }

    void _render_info(window::UIRenderer* ui) {
        ui->draw_text(6, 40, "SoC   ESP32-S3  (Xtensa LX7)", COLOR_WHITE);
        ui->draw_text(6, 56, "屏    ILI9488 480x320", COLOR_WHITE);
        ui->draw_text(6, 72, "存储  TF 卡 + 私有区", COLOR_WHITE);
        ui->draw_text(6, 88, "音频  MAX98357A I2S", COLOR_WHITE);
        ui->draw_text(6, 104, "输入  KY-023 摇杆", COLOR_WHITE);
        /* 补充键盘条目（2×MCP23017 已定版） */
        ui->draw_text(6, 120, "键盘  2×MCP23017 60键", COLOR_WHITE);
    }

    void _render_mem(window::UIRenderer* ui) {
        uint32_t free_h = esp_get_free_heap_size() / 1024;
        /* 大块为最大连续空闲块（原先误用总空闲堆） */
        uint32_t big = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT) / 1024;
        uint32_t used = 0, total = 0, ue = 0, te = 0;
        if (fs::PrivateFs::instance()->is_inited())
            fs::PrivateFs::instance()->stats(used, total, ue, te);
        char line[40];
        snprintf(line, sizeof(line), "堆空闲  %u KB", (unsigned)free_h);
        ui->draw_text(6, 40, line, COLOR_WHITE);
        snprintf(line, sizeof(line), "大块    %u KB", (unsigned)big);
        ui->draw_text(6, 56, line, COLOR_LIGHT_GRAY);
        snprintf(line, sizeof(line), "私区    %u/%u KB", (unsigned)(used / 1024), (unsigned)(total / 1024));
        ui->draw_text(6, 72, line, COLOR_LIGHT_GRAY);
        snprintf(line, sizeof(line), "条目    %u/%u", (unsigned)ue, (unsigned)te);
        ui->draw_text(6, 88, line, COLOR_LIGHT_GRAY);
    }

    void _render_test(window::UIRenderer* ui) {
        char line[48];
        switch (_test_step) {
            case 0:
                ui->draw_text(6, 40, "按 Enter 开始自检", COLOR_WHITE);
                break;
            case 1: {   /* 屏幕：三色条目视确认 */
                ui->fill_rect({6, 40, 96, 60}, rgb565(255, 0, 0));
                ui->fill_rect({106, 40, 96, 60}, rgb565(0, 255, 0));
                ui->fill_rect({206, 40, 96, 60}, rgb565(0, 0, 255));
                ui->draw_text(6, 108, "看到红/绿/蓝三色条 → Enter", COLOR_LIGHT_GRAY);
                break;
            }
            case 2: {   /* 摇杆：实时读数 */
                int32_t x = 0, y = 0;
                bool btn = false;
                drivers::Joystick::instance()->read(x, y, btn);
                snprintf(line, sizeof(line), "X=%d  Y=%d  按钮=%s", (int)x, (int)y, btn ? "按下" : "松开");
                ui->draw_text(6, 40, line, COLOR_WHITE);
                ui->draw_text(6, 56, "拨动摇杆看数值变化 → Enter", COLOR_LIGHT_GRAY);
                break;
            }
            case 3: {   /* 电池：真实读数 */
                snprintf(line, sizeof(line), "电池 %d%%", (int)drivers::BatteryAdc::instance()->percent());
                ui->draw_text(6, 40, line, COLOR_WHITE);
                ui->draw_text(6, 56, "电池读数正常 → Enter 完成", COLOR_LIGHT_GRAY);
                break;
            }
        }
        /* 自检进度条 */
        ui->fill_rect({6, 76, 308, 6}, COLOR_DARK_GRAY);
        ui->fill_rect({6, 76, (int16_t)(308 * _test_step / 3), 6}, COLOR_GREEN);
    }

    void _render_about(window::UIRenderer* ui) {
        char line[44];
        snprintf(line, sizeof(line), "%s v%s", MEMORIA_OS_NAME, MEMORIA_OS_VERSION);
        ui->draw_text(6, 40, line, COLOR_WHITE);
        ui->draw_text(6, 56, "2026 · Open Source · MIT", COLOR_YELLOW);
        ui->draw_text(6, 76, "硬件  ESP32-S3 · ILI9488 480x320", COLOR_LIGHT_GRAY);
        ui->draw_text(6, 90, "存储  TF 卡 + 私有分区(加密表)", COLOR_LIGHT_GRAY);
        ui->draw_text(6, 104, "音频  MAX98357A I2S · 摇杆 KY-023", COLOR_LIGHT_GRAY);
        ui->draw_text(6, 118, "连接  WiFi · BLE · 软件商店/OTA", COLOR_LIGHT_GRAY);
        ui->draw_text(6, 138, "模式  相册/音乐/视频/时钟/记事", COLOR_WHITE);
        ui->draw_text(6, 152, "      计算器/脚本/商店/设置/文件/系统", COLOR_WHITE);
        ui->draw_text(6, 172, "内核  FreeRTOS + 模式管理器", COLOR_DARK_GRAY);
        ui->draw_text(6, 186, "脚本  memoria-script · .ms 解释器", COLOR_DARK_GRAY);
        draw_hint(ui, "Enter 恢复出厂（三层确认）");
    }

    int _tab = 0;
    int _test_step = 0;
    bool _factory_armed = false;
};

static std::shared_ptr<window::Window> sys_create() {
    return std::make_shared<SysMode>();
}

void sys_mode_register() {
    ModeDesc d{};
    d.id = "sys"; d.name = "SYS"; d.cn = "系统";
    d.color = rgb565(148, 163, 184);
    d.create = sys_create;
    ModeManager::instance()->register_mode(d);
}

} // namespace modes
} // namespace memoria
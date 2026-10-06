/**
 * @file app_mode.hpp
 * @brief APP 直启运行页：桌面 .bas 图标点击直达，跳过 PRGM
 *
 * 用户拍板（2026-10-06）：安装完上桌面，点图标直接进 APP 全屏图形界面，
 * 绝不通过解释器菜单打开；PRGM 保留为开发入口。长按摇杆返回桌面。
 *
 * 运行模型：独立 FreeRTOS 任务跑解释器（basic_host），主循环照常分发
 * 输入事件；渲染由 render_all 每帧重放控件状态。
 */
#pragma once

#include "mode_manager.hpp"
#include "basic_host.hpp"
#include <memory>
#include <string>

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}

namespace memoria {
namespace modes {

class AppRunWindow : public ModeWindow {
public:
    explicit AppRunWindow(const std::string& bas_path);
    ~AppRunWindow() override;

    /* 覆写：NavBack 必须先杀 BASIC 任务再回桌面（基类直通返回不杀任务） */
    bool on_nav(const window::NavInput& ni) override;

    /* 全屏接管：无 F 键行、无模式标题（界面由程序自画） */
    void on_render() override;
    bool on_key(uint16_t key, bool pressed) override;

    void mode_render(window::UIRenderer* ui, bool focused) override;
    bool mode_nav(const window::NavInput& ni) override;
    const char* const* fn_labels() override;
    void on_fn(int idx) override;

private:
    void _stop_task();   /* abort + 等任务收尾（主任务可安全等） */

    std::string _path;
    std::string _name;
    std::shared_ptr<BasicHost> _host;
    bool _host_running = false;   /* 任务存活（含收尾等待期） */
    bool _file_missing = false;   /* .bas 读不到（图标点了但 TF 里没文件） */

    bool _ctrl_down = false;      /* Ctrl+Shift 中英切换（与 PRGM 同款） */
    bool _shift_down = false;
    TaskHandle_t _task = nullptr; /* BASIC 任务句柄（任务自删，仅记录） */
};

} // namespace modes
} // namespace memoria

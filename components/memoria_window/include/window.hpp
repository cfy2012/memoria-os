/**
 * @file window.hpp
 * @brief 窗口管理器：摇杆导航 + 状态栏 + 文本渲染
 *
 * 输入模型：JoystickDir 事件（Up/Down/Left/Right/Enter）
 *   - WindowManager 管理顶层窗口栈
 *   - StatusBar 常驻顶部（电量/WiFi/BLE/时钟）
 *   - UIRenderer 提供字体渲染 + 图像贴图 + 光标矩形
 */

#pragma once

#include "drivers_config.hpp"
#include "rtc_clock.hpp"
#include <esp_err.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace memoria {
namespace window {

using Color = uint16_t;

struct Rect { int16_t x=0, y=0, w=0, h=0; };
struct Point { int16_t x=0, y=0; };

enum class NavEvent : uint8_t {
    None = 0,
    NavUp,
    NavDown,
    NavLeft,
    NavRight,
    NavEnter,       /* 摇杆按下 = 确认 */
    NavBack,        /* 长按 3 秒 = 返回 */
};

/* 屏幕底部功能键行（卡西欧范式 F1~F6）
 * 真机无物理 F 键：F1~F6 是"屏幕内按键"标签，
 * 由各模式把动作映射到摇杆方向 + Enter（详见 mode_manager） */
enum class FnKey : uint8_t {
    F1 = 0, F2, F3, F4, F5, F6,
};

struct NavInput {
    NavEvent dir = NavEvent::None;
    uint32_t timestamp_ms = 0;
};

/* 系统事件（非导航） */
enum class SysEvent : uint8_t {
    None = 0,
    BatteryWarn,
    BatteryCrit,
    WiFiConnected,
    WiFiDisconnected,
    BLEConnected,
    BLEDisconnected,
    AlarmFired,
    IdleDimmed,
};

/* ============================================================
 *  Window：单个窗口（基础属性 + 导航/渲染回调，不含触摸事件）
 * ============================================================ */
class Window : public std::enable_shared_from_this<Window> {
public:
    Window() = default;
    virtual ~Window() = default;

    virtual void on_open()  {}
    virtual void on_close() {}
    virtual void on_render() {}

    /* 导航事件处理：返回 true 表示已消费 */
    virtual bool on_nav(const NavInput& ni) {
        return _nav_handler ? _nav_handler(ni) : false;
    }

    /* 系统事件 */
    virtual bool on_sys(SysEvent se) { return false; }

    /* 布局 */
    void set_rect(const Rect& r) { _rect = r; }
    const Rect& rect() const { return _rect; }
    bool contains(const Point& p) const {
        return p.x >= _rect.x && p.x < _rect.x + _rect.w &&
               p.y >= _rect.y && p.y < _rect.y + _rect.h;
    }

    using NavHandler = std::function<bool(const NavInput&)>;
    void set_nav_handler(NavHandler h) { _nav_handler = std::move(h); }
    void set_z(int z)     { _z = z; }
    int  z()        const { return _z; }
    void set_visible(bool v) { _visible = v; }
    bool visible()  const { return _visible; }
    void set_focusable(bool f) { _focusable = f; }
    bool focusable() const { return _focusable; }

    /* 子窗口 */
    void add_child(std::shared_ptr<Window> c) { _children.push_back(std::move(c)); }
    const std::vector<std::shared_ptr<Window>>& children() const { return _children; }

    /* 光标焦点（在应用内页面导航时使用） */
    void set_cursor(int16_t cx, int16_t cy) { _cx = cx; _cy = cy; }
    int16_t cursor_x() const { return _cx; }
    int16_t cursor_y() const { return _cy; }

protected:
    Rect        _rect{};
    int         _z       = 0;
    bool        _visible = true;
    bool        _focusable = true;
    NavHandler  _nav_handler;
    std::vector<std::shared_ptr<Window>> _children;
    int16_t     _cx = 0;   /* 光标位置（相对于窗口 rect） */
    int16_t     _cy = 0;
};

/* ============================================================
 *  WindowManager：顶层窗口栈 + 导航事件中心
 * ============================================================ */
class WindowManager {
public:
    static WindowManager* instance();

    esp_err_t init();

    /* 顶层窗口 */
    void push_top(std::shared_ptr<Window> w);
    void pop_top();
    Window* top();

    /* 导航事件注入：主循环每 ~50ms 调一次（摇杆采样频率 20Hz） */
    void dispatch_nav(const NavInput& ni);
    /* 系统事件 */
    void dispatch_sys(SysEvent se);

    /* 渲染所有可见窗口 + flush */
    void render_all();

    /* 全局钩子（在窗口栈之前触发） */
    using NavGlobalCb = std::function<bool(const NavInput&)>;
    using SysGlobalCb = std::function<bool(SysEvent)>;
    void add_global_nav_cb(NavGlobalCb cb) { _global_navs.push_back(std::move(cb)); }
    void add_global_sys_cb(SysGlobalCb cb) { _global_sys.push_back(std::move(cb)); }

private:
    WindowManager() = default;
    bool _dispatch_to_stack(const NavInput& ni);
    bool _dispatch_sys_to_stack(SysEvent se);

    std::vector<std::shared_ptr<Window>> _stack;
    std::vector<NavGlobalCb> _global_navs;
    std::vector<SysGlobalCb> _global_sys;
};

/* ============================================================
 *  StatusBar：常驻顶部状态栏（在 WindowManager::init() 时注册）
 * ============================================================ */
class StatusBar : public Window {
public:
    StatusBar();

    void on_render() override;
};

/* ============================================================
 *  UIRenderer：高层图元 + 自制 8×12 ASCII 字体 + 光标绘制
 * ============================================================ */
class UIRenderer {
public:
    static UIRenderer* instance();

    void clear(Color c = drivers::COLOR_BLACK);
    void flush();

    /* 图元（ILI9341 驱动层已有，但 renderer 做封装） */
    void fill_rect(const Rect& r, Color c);
    void draw_rect(const Rect& r, Color c, uint16_t thickness = 1);

    /* 光标（聚焦框：1~2px 厚度） */
    void draw_cursor(const Rect& r, Color c, uint16_t thickness = 2);

    /* 图像贴图（RGB565 原始 buffer） */
    void draw_image(const Rect& r, const uint16_t* rgb565_buf,
                    uint16_t img_w, uint16_t img_h);

    /* 自制 8×12 ASCII 字体渲染：纯 C++ 无外部依赖 */
    void draw_text(int16_t x, int16_t y, const std::string& text, Color c);

    /* UTF-8 中英混排：ASCII 走 8×12，汉字走 GB2312 16×16 点阵 */
    void draw_text_utf8(int16_t x, int16_t y, const std::string& text, Color c);
    static int text_width_utf8(const std::string& text);   /* 绘制前宽度计算（ASCII 8 / 中文 16） */

    /* 字体尺寸 */
    static constexpr int FONT_W = 8;
    static constexpr int FONT_H = 12;
    static constexpr int CJK_W  = 16;
    static constexpr int CJK_H  = 16;

private:
    UIRenderer() = default;
};

} // namespace window
} // namespace memoria
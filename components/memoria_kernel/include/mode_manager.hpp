/**
 * @file mode_manager.hpp
 * @brief 卡西欧程序计算器范式：模式管理器
 *
 * 主菜单（3×4 模式网格）→ 进入模式（内容区 + 底部 F1~F6 功能键行）
 * 导航：
 *   摇杆方向 = 主菜单移动光标 / 模式内列表移动
 *   摇杆按下 = 进入模式 / 模式内确认（等价 F1）
 *   摇杆长按(≥1.5s) = NavBack 返回主菜单（等价 F6 菜单的退出）
 *
 * 真机没有物理 F1~F6 键：功能键行是屏幕内标签，
 * 模式内由摇杆方向 + Enter 触发对应动作（各模式自行映射）。
 */

#pragma once

#include "window.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace memoria {
namespace modes {

/* RGB565 便捷构造（与 drivers_config 的 Color=uint16_t 兼容） */
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/* 单个模式描述 */
struct ModeDesc {
    const char* id   = nullptr;   /* "photo" */
    const char* name = nullptr;   /* "PHOTO" 主菜单英文名 */
    const char* cn   = nullptr;   /* "相册"  中文名 */
    uint16_t    color = 0;        /* 主菜单色块 */
    std::function<std::shared_ptr<window::Window>()> create;  /* 创建模式窗口 */
};

/* ============================================================
 *  ModeManager：模式注册表 + 主菜单/模式切换
 * ============================================================ */
class ModeManager {
public:
    static ModeManager* instance();

    void register_mode(const ModeDesc& d);
    int  count() const { return static_cast<int>(_modes.size()); }
    const ModeDesc* get(int i) const;

    /* 按 id 查索引 / 进入 */
    int  index_of(const char* id) const;
    bool enter_by_id(const char* id);

    /* 启动：创建 Launcher 并压入窗口栈（在 kernel init 尾部调用） */
    void boot();
    /* 进入第 index 个模式 */
    void enter(int index);
    /* 返回主菜单 */
    void back_to_menu();

    /* 键盘事件分发：转发给当前模式窗口（Launcher 页忽略） */
    bool dispatch_key(uint16_t key, bool pressed);

private:
    ModeManager() = default;
    std::vector<ModeDesc> _modes;
    std::shared_ptr<window::Window> _launcher;
    std::shared_ptr<window::Window> _current;
};

/* ============================================================
 *  LauncherWindow：主菜单（3 列 × 4 行模式网格）
 * ============================================================ */
class LauncherWindow : public window::Window {
public:
    LauncherWindow();
    void on_render() override;
    bool on_nav(const window::NavInput& ni) override;
private:
    int _sel = 0;   /* 0..11（10 模式 + 2 空位） */
};

/* ============================================================
 *  ModeWindow：模式窗口基类
 *  布局：状态栏(0..16) / 标题+内容区(16..208) / 功能键行(208..224)
 * ============================================================ */
class ModeWindow : public window::Window {
public:
    ModeWindow();

    void on_render() override;
    bool on_nav(const window::NavInput& ni) override;

    /* ---- 子类必须实现 ---- */
    virtual void mode_render(window::UIRenderer* ui, bool focused) = 0;
    virtual bool mode_nav(const window::NavInput& ni) = 0;

    /* 键盘事件（输入队列 → 当前模式）：返回 true 表示已消费 */
    virtual bool on_key(uint16_t key, bool pressed) { (void)key; (void)pressed; return false; }
    /* 6 个功能键标签，空字符串 = 空键（不绘制） */
    virtual const char* const* fn_labels() = 0;
    /* F1~F6 触发（模拟器键盘/调试口） */
    virtual void on_fn(int idx) = 0;

    /* ---- 通用确认弹窗（可选使用） ---- */
    void request_confirm(const char* text);      /* 弹出"确认/取消" */
    bool confirm_active() const { return _confirm >= 0; }
    virtual void on_confirm_ok() {}              /* 用户确认后的动作 */

protected:
    int _cur = 0;   /* 通用光标 */

    void draw_fn_bar(window::UIRenderer* ui);
    void draw_title(window::UIRenderer* ui, const char* title, const char* right);
    void draw_hint(window::UIRenderer* ui, const char* text);
    void draw_confirm(window::UIRenderer* ui);

private:
    int  _confirm = -1;       /* -1 无确认；0=取消；1=确认（默认高亮确认） */
    const char* _confirm_text = nullptr;
};

/* 所有模式的注册入口（modes_bootstrap.cpp 实现） */
void memoria_modes_register_all();

} // namespace modes
} // namespace memoria
/**
 * @file desktop_manager.hpp
 * @brief 桌面布局管理器（安卓式可编辑桌面）
 *
 * 3×4 网格桌面，支持：
 *   - 图标移动（拿起 → 放下：交换 / 移入空位 / 移入文件夹）
 *   - 文件夹（空位创建、图标移入、逐层进入、文件夹套文件夹）
 *   - 布局持久化到 NVS（namespace "desktop"：root / f<id> / seq）
 *
 * 交互（摇杆）：
 *   编辑模式（Launcher 上长按摇杆进入/退出）
 *     Enter 图标     → 拿起；再按 → 放下（交换/移空位/入文件夹）
 *     Enter 文件夹   → 进入
 *     Enter 空位     → 新建文件夹
 *     长按（NavBack）→ 取消拿起 / 退出编辑；在文件夹内 → 先返回上级
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace memoria {
namespace modes {

enum class CellType : uint8_t { Empty = 0, App = 1, Folder = 2 };

struct Cell {
    CellType    type = CellType::Empty;
    std::string app_id;        /* App */
    int         folder_id = -1;
    std::string folder_name;
};

class DesktopManager {
public:
    static DesktopManager* instance();

    /* ---- 加载 / 保存（NVS） ---- */
    void load();
    void save();

    /* ---- 当前层访问 ---- */
    const std::vector<Cell>& cells() const;
    bool in_folder() const { return !_stack.empty(); }
    int  current_folder() const { return _stack.empty() ? -1 : _stack.back(); }
    const std::string& folder_name(int fid) const;

    /* ---- 文件夹导航 ---- */
    bool enter_folder(int idx);     /* 打开当前层第 idx 格的文件夹 */
    void go_up();                   /* 返回上级（根则无操作） */

    /* ---- 编辑操作 ---- */
    bool create_folder(int idx);    /* 空位新建文件夹 */
    void move_pick(int idx);        /* 拿起当前格图标 */
    void move_drop(int idx);        /* 放下：交换 / 移空位 / 入文件夹 */
    void cancel_pick() { _picked = -1; }
    int  picked() const { return _picked; }

    /* ---- 编辑模式 ---- */
    bool is_edit() const { return _edit; }
    void set_edit(bool e) { _edit = e; if (!e) _picked = -1; }

private:
    DesktopManager() = default;

    std::vector<Cell>* _layer();
    std::string _encode(const std::vector<Cell>& c) const;
    bool _decode(const std::string& s, std::vector<Cell>& out);
    int  _next_folder_id();

    std::vector<Cell> _root;                    /* 根桌面 12 格 */
    std::map<int, std::vector<Cell>> _folders;  /* 文件夹 id → 12 格 */
    std::map<int, std::string> _folder_names;
    std::vector<int> _stack;                    /* 路径栈（空 = 根） */
    int  _picked = -1;
    bool _edit = false;
    bool _loaded = false;
    int  _seq = 1;
};

} // namespace modes
} // namespace memoria
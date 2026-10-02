#pragma once
/* ============================================================
 * @file ime.hpp
 * @brief 拼音输入法（全拼，无字母选择）
 *
 * 模式：EN（英文直通）/ CN（中文拼音）
 * 键位约定（文字编辑界面内）：
 *   F1           切换 中/英
 *   a~z          拼音串（CN 模式）
 *   1~9          直接选候选上屏
 *   退格         删拼音尾字母（CN）；删行尾字符（EN 由调用方处理）
 *   空格/回车    上屏当前选中候选（默认第 0 个）
 *   左右摇杆     切换候选（CN 且候选存在时）
 * ============================================================ */
#include <cstdint>
#include <cstddef>
#include <functional>
#include "window.hpp"

namespace memoria {
namespace ime {

enum class Mode : uint8_t { EN, CN };

using CommitCb = std::function<void(const char* utf8, size_t len)>;

/* 初始化（清状态），kernel 启动时调用 */
void ime_init();

/* 设置"上屏"回调：候选确认后把 UTF-8 汉字交给调用方 */
void ime_set_commit_cb(CommitCb cb);

/* 中/英切换（F1） */
void ime_toggle();
Mode ime_mode();
const char* ime_mode_str();      /* "EN" / "中" */

/* 是否有候选（拼音串非空且命中字典） */
bool ime_active();
const char* ime_pinyin();        /* 当前拼音串 */
int  ime_candidate_count();      /* 0~9 */
const char* ime_candidate(int i);/* 第 i 个候选 UTF-8（i < count） */
int  ime_sel();
void ime_set_sel(int s);

/* 处理键：返回 true 表示已消费（调用方不应再当普通字符插入） */
bool ime_handle_key(uint16_t key, bool pressed);

/* 候选条渲染（建议画在编辑器字符条位置） */
void ime_draw_bar(window::UIRenderer* ui, int x, int y);

} // namespace ime
} // namespace memoria
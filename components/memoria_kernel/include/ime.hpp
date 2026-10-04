#pragma once
/* ============================================================
 * @file ime.hpp
 * @brief 拼音输入法（IME-SPEC v1.0 拍板版，2026-10-04）
 *
 * 模式：EN（英文直通）/ CN（中文拼音）
 * 键位约定（用户拍板）：
 *   a~z          拼音字母（实时过滤候选，大写自动转小写）
 *   F1~F4        选当前批第 1~4 个候选上屏
 *   F5 / F6      候选前翻 / 后翻一批（每批 4 字）
 *   退格         有拼音删拼音尾字母；拼音空时交还调用方删字
 *   回车（确认）  有拼音时上屏拼音原字母；无拼音时结束输入（交还调用方）
 *   Ctrl+Shift   中 / 英 模式切换（同时按，替代旧 F1 方案）
 *
 * 码表：TF 卡 /mem_fat/ime/pinyin.bin（PYIME1，6763 字，63KB）
 *   PSRAM 整表加载 + 前缀匹配 + 批次翻页；缺失/损坏时回退内置字典。
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

/* 中/英切换（Ctrl+Shift） */
void ime_toggle();
Mode ime_mode();
bool ime_cn();              /* 1=中文 / 0=英文（系统布尔状态，供预注入） */
const char* ime_mode_str(); /* "EN" / "中" */

/* 是否有候选（拼音串非空且命中字典） */
bool ime_active();
const char* ime_pinyin();   /* 当前拼音串 */
int  ime_candidate_count(); /* 当前批 0~4 */
const char* ime_candidate(int i); /* 第 i 个候选 UTF-8（i < count） */
int  ime_sel();
void ime_set_sel(int s);

/* 批次翻页（每批 4 字） */
int  ime_page();            /* 当前页（0 起） */
int  ime_page_count();      /* 总批数（0 = 无候选） */
void ime_set_page(int p);   /* 夹紧后重填当前批 */

/* 处理键：返回 true 表示已消费（调用方不应再当普通字符插入） */
bool ime_handle_key(uint16_t key, bool pressed);

/* 候选条渲染（建议画在编辑器字符条位置） */
void ime_draw_bar(window::UIRenderer* ui, int x, int y);

} // namespace ime
} // namespace memoria

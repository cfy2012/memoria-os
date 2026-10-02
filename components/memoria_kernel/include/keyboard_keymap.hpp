#pragma once
/* ============================================================
 * @file keyboard_keymap.hpp
 * @brief 6×10 矩阵键盘键位表（双层：主键 + Shift 层）
 * 布局来源：键盘按键布局表.xlsx（用户定义，2026-09-27 确认）
 *
 * 规则：
 *   键面 "符号 主键" = 主键为字母/数字（正常输入），Shift+键 = 符号
 *   纯字母键（E/R/T/D/F/G/H/J/K/C/V/B）：主键小写，Shift/Caps 输出大写
 *   ON/OFF、音量、背光等在矩阵内，物理位置可牵长线放置
 * ============================================================ */
#include <cstdint>
#include <cstddef>

namespace memoria::input {

/* 特殊键码（0x80 起，不与 ASCII 冲突） */
enum : uint16_t {
  K_NONE = 0x0000,
  K_BACKSPACE = 0x0008,   /* ASCII BS */
  K_TAB      = 0x0009,
  K_ENTER    = 0x000D,    /* ASCII CR */
  K_ESC      = 0x001B,
  K_SPACE    = 0x0020,
  /* 功能/系统键 */
  K_ONOFF    = 0x0080,
  K_F1, K_F2, K_F3, K_F4, K_F5, K_F6, K_F7,
  K_BACKLIGHT,
  K_SHIFT,
  K_CAPSLOCK,
  K_CTRL,
  K_VOL_DN,
  K_VOL_UP,
  K_LEFT, K_RIGHT, K_DOWN, K_UP,
};

struct KeyDef {
  uint16_t base;   /* 主键（无 Shift） */
  uint16_t shift;  /* Shift 层 */
};

constexpr size_t KEY_ROWS = 6;
constexpr size_t KEY_COLS = 10;

/* R1 功能键行 */
constexpr KeyDef KEYMAP_R1[KEY_COLS] = {
  {K_ONOFF, K_ONOFF}, {K_F1, K_F1}, {K_F2, K_F2}, {K_F3, K_F3},
  {K_F4, K_F4}, {K_F5, K_F5}, {K_F6, K_F6}, {K_F7, K_F7},
  {K_BACKLIGHT, K_BACKLIGHT}, {K_BACKSPACE, K_BACKSPACE},
};
/* R2 数字行：主键数字，Shift 层符号 */
constexpr KeyDef KEYMAP_R2[KEY_COLS] = {
  {'1','!'}, {'2','@'}, {'3','#'}, {'4','$'}, {'5','%'},
  {'6','^'}, {'7','&'}, {'8','*'}, {'9','('}, {'0',')'},
};
/* R3 字母行 Q-P */
constexpr KeyDef KEYMAP_R3[KEY_COLS] = {
  {'q','-'}, {'w','_'}, {'e','E'}, {'r','R'}, {'t','T'},
  {'y','Y'}, {'u','['}, {'i',']'}, {'o','{'}, {'p','}'},
};
/* R4 字母行 A-; */
constexpr KeyDef KEYMAP_R4[KEY_COLS] = {
  {'a','='}, {'s','+'}, {'d','D'}, {'f','F'}, {'g','G'},
  {'h','H'}, {'j','J'}, {'k','K'}, {'l','"'}, {';',':'},
};
/* R5 字母行 Z-. + Shift */
constexpr KeyDef KEYMAP_R5[KEY_COLS] = {
  {K_SHIFT, K_SHIFT}, {'z','\\'}, {'x','|'}, {'c','C'}, {'v','V'},
  {'b','B'}, {'n','/'}, {'m','?'}, {',','<'}, {'.','>'},
};
/* R6 控制/方向/音量行 */
constexpr KeyDef KEYMAP_R6[KEY_COLS] = {
  {K_CAPSLOCK, K_CAPSLOCK}, {K_CTRL, K_CTRL},
  {K_VOL_DN, K_VOL_DN}, {K_VOL_UP, K_VOL_UP},
  {K_SPACE, K_SPACE},
  {K_LEFT, K_LEFT}, {K_RIGHT, K_RIGHT}, {K_DOWN, K_DOWN}, {K_UP, K_UP},
  {K_ENTER, K_ENTER},
};

constexpr const KeyDef* KEYMAP_ROWS[KEY_ROWS] = {
  KEYMAP_R1, KEYMAP_R2, KEYMAP_R3, KEYMAP_R4, KEYMAP_R5, KEYMAP_R6,
};

/* 取键：行列 + Shift/Caps 状态 → 最终键码
 *   主键小写字母：Shift 或 Caps 时转大写；Shift 层符号直接返回 */
inline uint16_t resolve_key(size_t r, size_t c, bool shift, bool caps) {
  if (r >= KEY_ROWS || c >= KEY_COLS) return K_NONE;
  const KeyDef& k = KEYMAP_ROWS[r][c];
  uint16_t v = shift ? k.shift : k.base;
  if (v >= 'a' && v <= 'z') {
    if (shift || caps) v -= 32;
  }
  return v;
}

} /* namespace memoria::input */
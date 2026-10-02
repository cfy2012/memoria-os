/**
 * @file ime.cpp
 * @brief 拼音输入法实现：全拼联想 + 候选条渲染
 *
 * 字典：pinyin_dict.cpp（801 组全拼，高频字优先）
 * 只消费键盘事件；摇杆导航仍走系统（左右选候选在本层处理）
 */

#include "ime.hpp"
#include "pinyin_dict.hpp"
#include "keyboard_keymap.hpp"

#include <cstring>
#include <cctype>
#include <cstdio>
#include <string>

namespace memoria {
namespace ime {

using namespace memoria::drivers;   /* SCREEN_W / COLOR_* */
using namespace memoria::input;    /* K_LEFT/K_ENTER/... */

static Mode      s_mode  = Mode::EN;
static CommitCb  s_commit;
static char      s_pinyin[9]  = "";     /* 拼音串（<=8） */
static int       s_sel   = 0;
static char      s_cand[9][5] = {{0}};  /* 候选 UTF-8，4 字节字符 + \0 = 5 */

static void refresh_candidates() {
    s_sel = 0;
    for (int i = 0; i < 9; i++) s_cand[i][0] = 0;
    if (!s_pinyin[0]) return;
    int idx = pinyin_find(s_pinyin);
    if (idx < 0) return;
    const char* v = pinyin_val(idx);
    int n = 0;
    const char* p = v;
    while (*p && n < 9) {
        size_t l = 1;
        const uint8_t b = static_cast<uint8_t>(*p);
        if      (b >= 0xF0) l = 4;
        else if (b >= 0xE0) l = 3;
        else if (b >= 0xC0) l = 2;
        if (n + (int)l <= 9) { std::memcpy(s_cand[n], p, l); s_cand[n][l] = 0; }
        p += l;
        n++;
    }
}

static void commit(int i) {
    if (i < 0 || i >= 9 || !s_cand[i][0]) return;
    if (s_commit) s_commit(s_cand[i], std::strlen(s_cand[i]));
    s_pinyin[0] = 0;
    refresh_candidates();
}

/* ---------------- 公共接口 ---------------- */

void ime_init() {
    s_mode = Mode::EN;
    s_commit = nullptr;
    s_pinyin[0] = 0;
    refresh_candidates();
}

void ime_set_commit_cb(CommitCb cb) { s_commit = std::move(cb); }

void ime_toggle() {
    s_mode = (s_mode == Mode::EN) ? Mode::CN : Mode::EN;
    s_pinyin[0] = 0;
    refresh_candidates();
}

Mode ime_mode() { return s_mode; }

const char* ime_mode_str() { return s_mode == Mode::CN ? "中" : "EN"; }

bool ime_active() { return s_pinyin[0] != 0 && s_cand[0][0] != 0; }

const char* ime_pinyin() { return s_pinyin; }

int ime_candidate_count() {
    int n = 0;
    while (n < 9 && s_cand[n][0]) n++;
    return n;
}

const char* ime_candidate(int i) {
    if (i < 0 || i >= 9 || !s_cand[i][0]) return nullptr;
    return s_cand[i];
}

int ime_sel() { return s_sel; }
void ime_set_sel(int s) {
    if (s >= 0 && s < ime_candidate_count()) s_sel = s;
}

bool ime_handle_key(uint16_t key, bool pressed) {
    if (!pressed) return false;
    if (s_mode == Mode::EN) return false;

    if (key >= 'A' && key <= 'Z') key += 32;   /* 大写字母转小写进拼音 */
    if (key >= 'a' && key <= 'z') {
        const size_t n = std::strlen(s_pinyin);
        if (n < 8) { s_pinyin[n] = static_cast<char>(key); s_pinyin[n + 1] = 0; refresh_candidates(); }
        return true;
    }
    if (key >= '1' && key <= '9') {
        const int i = key - '1';
        if (s_cand[i][0]) commit(i);
        return true;
    }
    if (key == K_BACKSPACE) {
        const size_t n = std::strlen(s_pinyin);
        if (n > 0) { s_pinyin[n - 1] = 0; refresh_candidates(); }
        return true;
    }
    if (key == ' ' || key == K_ENTER) {
        if (s_cand[s_sel][0]) commit(s_sel);
        return true;
    }
    if (key == K_LEFT) {
        if (s_cand[0][0]) s_sel = (s_sel > 0) ? s_sel - 1 : 0;
        return true;
    }
    if (key == K_RIGHT) {
        const int cnt = ime_candidate_count();
        if (s_cand[0][0] && s_sel + 1 < cnt) s_sel++;
        return true;
    }
    return false;
}

void ime_draw_bar(window::UIRenderer* ui, int x, int y) {
    char head[24];
    std::snprintf(head, sizeof(head), "[%s] %s", ime_mode_str(), ime_pinyin());
    /* 背景条 */
    ui->fill_rect({(int16_t)x, (int16_t)y, (int16_t)(SCREEN_W - 2 * x), 16}, COLOR_DARK_GRAY);
    ui->draw_rect({(int16_t)x, (int16_t)y, (int16_t)(SCREEN_W - 2 * x), 16}, COLOR_YELLOW, 1);

    const int cnt = ime_candidate_count();
    if (cnt == 0) {
        ui->draw_text_utf8(x + 2, y + 2, head, COLOR_LIGHT_GRAY);
        return;
    }
    /* 整行候选：head + " 1字 2字 ..." */
    std::string line = head;
    for (int i = 0; i < cnt; i++) {
        line += ' ';
        line += std::to_string(i + 1);
        line += s_cand[i];
    }
    ui->draw_text_utf8(x + 2, y + 2, line, COLOR_WHITE);

    /* 计算选中字起点，用黄色重画以高亮 */
    int cx = x + 2;
    for (size_t i = 0; i < std::strlen(head); i++) {
        const uint8_t b = static_cast<uint8_t>(head[i]);
        cx += (b < 0x80) ? window::UIRenderer::FONT_W : window::UIRenderer::CJK_W;
        if (b >= 0x80) i += 2;   /* 跳 UTF-8 后续字节 */
    }
    for (int i = 0; i < cnt; i++) {
        cx += 2 * window::UIRenderer::FONT_W;   /* " N" = 空格 + 数字 */
        if (i == s_sel) {
            ui->draw_text_utf8(cx, y + 2, s_cand[i], COLOR_YELLOW);
        }
        cx += window::UIRenderer::CJK_W;
    }
}

} // namespace ime
} // namespace memoria
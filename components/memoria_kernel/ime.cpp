/**
 * @file ime.cpp
 * @brief 拼音输入法实现：PYIME1 码表引擎 + 批次候选 + 候选条渲染
 *
 * 键位（IME-SPEC v1.0 拍板版）：
 *   a~z 拼音 · F1~F4 选当前批 4 字 · F5/F6 翻批 · 退格删拼音 ·
 *   回车（有拼音上屏原字母 / 无拼音交还调用方）· Ctrl+Shift 中英切换
 * 码表：/mem_fat/ime/pinyin.bin（PYIME1，6763 字，PSRAM 整表加载 + 版本头校验）；
 *       缺失/损坏时回退内置 pinyin_dict（801 组全拼），保证可用。
 */

#include "ime.hpp"
#include "pinyin_dict.hpp"
#include "keyboard_keymap.hpp"

#include <cstring>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#include "esp_heap_caps.h"

namespace memoria {
namespace ime {

using namespace memoria::drivers;   /* SCREEN_W / COLOR_* */
using namespace memoria::input;    /* K_LEFT/K_ENTER/... */

/* ---------------- 内部状态 ---------------- */
static Mode      s_mode  = Mode::EN;
static CommitCb  s_commit;
static char      s_pinyin[9]  = "";     /* 拼音串（<=8） */
static int       s_sel   = 0;
static char      s_cand[4][5] = {{0}};  /* 当前批候选 UTF-8，4 字节字符 + \0 = 5 */
static int       s_page  = 0;           /* 当前页（0 起） */
static std::vector<std::string> s_hits; /* 当前拼音全部命中（表序，分页数据源） */

/* ---------------- PYIME1 码表（TF 卡，PSRAM 整表） ---------------- */
static const char* kDictPath = "/mem_fat/ime/pinyin.bin";
static uint8_t* s_dict   = nullptr;   /* PSRAM 整表内存 */
static size_t  s_dict_sz = 0;
static uint32_t s_dict_cnt = 0;
static bool    s_dict_tried = false;

static void load_pinyin_dict() {
    if (s_dict_tried) return;
    s_dict_tried = true;

    FILE* f = std::fopen(kDictPath, "rb");
    if (!f) return;   /* 缺卡/缺文件：回退内置字典 */
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz < 12) { std::fclose(f); return; }

    void* mem = heap_caps_malloc((size_t)sz, MALLOC_CAP_SPIRAM);
    if (!mem) { std::fclose(f); return; }
    if (std::fread(mem, 1, (size_t)sz, f) != (size_t)sz) {
        heap_caps_free(mem); std::fclose(f); return;
    }
    std::fclose(f);

    uint8_t* d = (uint8_t*)mem;
    /* 版本头校验：魔数 PYIME1\0 + 条目数 */
    if (std::memcmp(d, "PYIME1\0", 8) != 0) { heap_caps_free(mem); return; }
    uint32_t cnt = (uint32_t)d[8] | ((uint32_t)d[9] << 8) |
                   ((uint32_t)d[10] << 16) | ((uint32_t)d[11] << 24);
    if (cnt == 0 || cnt > 8000) { heap_caps_free(mem); return; }

    s_dict = d;
    s_dict_sz = (size_t)sz;
    s_dict_cnt = cnt;
}

/* 前缀匹配：拼音 p 是否以 q 开头 */
static bool pinyin_prefix_match(const uint8_t* p, uint8_t plen, const char* q) {
    size_t ql = std::strlen(q);
    if (ql > plen) return false;
    for (size_t i = 0; i < ql; i++)
        if (p[i] != (uint8_t)q[i]) return false;
    return true;
}

/* 顺序扫表收集命中字（PYIME1：汉字3B + 读音数1B + n×[len1B+拼音]） */
static void scan_dict_hits(const char* pinyin) {
    s_hits.clear();
    size_t pos = 12;
    for (uint32_t e = 0; e < s_dict_cnt && pos < s_dict_sz; e++) {
        if (pos + 4 > s_dict_sz) break;
        const char* han = (const char*)(s_dict + pos);   /* UTF-8 3B */
        uint8_t n = s_dict[pos + 3];
        pos += 4;
        bool matched = false;
        for (uint8_t k = 0; k < n; k++) {
            if (pos >= s_dict_sz) break;
            uint8_t plen = s_dict[pos];
            if (pos + 1 + plen > s_dict_sz) break;
            if (pinyin_prefix_match(s_dict + pos + 1, plen, pinyin)) matched = true;
            pos += 1 + plen;
        }
        if (matched) s_hits.emplace_back(han, 3);
    }
}

/* 内置字典回退（801 组全拼，值串 = 高频字连接，拆出单个汉字） */
static void scan_builtin_hits(const char* pinyin) {
    s_hits.clear();
    int idx = pinyin_find(pinyin);
    if (idx < 0) return;
    const char* v = pinyin_val(idx);
    while (*v) {
        const uint8_t b = (uint8_t)*v;
        size_t l = 1;
        if      (b >= 0xF0) l = 4;
        else if (b >= 0xE0) l = 3;
        else if (b >= 0xC0) l = 2;
        s_hits.emplace_back(v, l);
        v += l;
    }
}

/* 由 s_hits 填充当前页 s_cand（s_page 已夹紧） */
static void fill_page() {
    for (int i = 0; i < 4; i++) s_cand[i][0] = 0;
    for (int i = 0; i < 4; i++) {
        int gi = s_page * 4 + i;
        if (gi < (int)s_hits.size()) {
            std::memcpy(s_cand[i], s_hits[(size_t)gi].c_str(),
                        s_hits[(size_t)gi].size());
            s_cand[i][s_hits[(size_t)gi].size()] = 0;
        }
    }
}

static void refresh_candidates() {
    s_sel = 0;
    if (!s_pinyin[0]) { s_hits.clear(); s_page = 0; for (auto& c : s_cand) c[0] = 0; return; }
    load_pinyin_dict();
    if (s_dict) scan_dict_hits(s_pinyin);
    else        scan_builtin_hits(s_pinyin);
    int maxp = s_hits.empty() ? 0 : ((int)s_hits.size() - 1) / 4;
    if (s_page > maxp) s_page = maxp;
    if (s_page < 0) s_page = 0;
    fill_page();
}

static void commit(int i) {
    if (i < 0 || i >= 4 || !s_cand[i][0]) return;
    if (s_commit) s_commit(s_cand[i], std::strlen(s_cand[i]));
    s_pinyin[0] = 0;
    refresh_candidates();
}

/* 上屏拼音原字母（确认键在有拼音时：编辑行上屏原字母） */
static void commit_pinyin() {
    if (s_commit && s_pinyin[0]) s_commit(s_pinyin, std::strlen(s_pinyin));
    s_pinyin[0] = 0;
    refresh_candidates();
}

/* ---------------- 公共接口 ---------------- */

void ime_init() {
    s_mode = Mode::EN;
    s_commit = nullptr;
    s_pinyin[0] = 0;
    s_dict_tried = false;
    s_dict = nullptr;
    refresh_candidates();
}

void ime_set_commit_cb(CommitCb cb) { s_commit = std::move(cb); }

void ime_toggle() {
    s_mode = (s_mode == Mode::EN) ? Mode::CN : Mode::EN;
    s_pinyin[0] = 0;
    refresh_candidates();
}

Mode ime_mode() { return s_mode; }
bool ime_cn()   { return s_mode == Mode::CN; }

const char* ime_mode_str() { return s_mode == Mode::CN ? "中" : "EN"; }

bool ime_active() { return s_pinyin[0] != 0 && !s_hits.empty(); }

const char* ime_pinyin() { return s_pinyin; }

int ime_candidate_count() {
    int n = 0;
    while (n < 4 && s_cand[n][0]) n++;
    return n;
}

const char* ime_candidate(int i) {
    if (i < 0 || i >= 4 || !s_cand[i][0]) return nullptr;
    return s_cand[i];
}

int ime_sel() { return s_sel; }
void ime_set_sel(int s) {
    if (s >= 0 && s < ime_candidate_count()) s_sel = s;
}

int ime_page() { return s_page; }
int ime_page_count() {
    return s_hits.empty() ? 0 : ((int)s_hits.size() - 1) / 4 + 1;
}
void ime_set_page(int p) {
    int maxp = s_hits.empty() ? 0 : ((int)s_hits.size() - 1) / 4;
    if (p < 0) p = 0;
    if (p > maxp) p = maxp;
    s_page = p;
    s_sel = 0;
    fill_page();
}

bool ime_handle_key(uint16_t key, bool pressed) {
    if (!pressed) return false;
    if (s_mode == Mode::EN) return false;

    if (key >= 'A' && key <= 'Z') key += 32;   /* 大写字母转小写进拼音 */

    /* a~z：输入拼音（重置到首页） */
    if (key >= 'a' && key <= 'z') {
        const size_t n = std::strlen(s_pinyin);
        if (n < 8) { s_pinyin[n] = (char)key; s_pinyin[n + 1] = 0; s_page = 0; refresh_candidates(); }
        return true;
    }

    /* F1~F4：选当前批候选上屏 */
    if (key >= K_F1 && key <= K_F4) {
        const int i = (int)(key - K_F1);
        if (s_cand[i][0]) commit(i);
        return true;
    }

    /* F5 / F6：候选前翻 / 后翻一批 */
    if (key == K_F5) { ime_set_page(s_page - 1); return true; }
    if (key == K_F6) { ime_set_page(s_page + 1); return true; }

    /* 退格：有拼音删拼音尾字母；拼音空交还调用方删字 */
    if (key == K_BACKSPACE) {
        const size_t n = std::strlen(s_pinyin);
        if (n > 0) { s_pinyin[n - 1] = 0; s_page = 0; refresh_candidates(); }
        return n > 0;
    }

    /* 回车（确认）：有拼音上屏原字母；无拼音交还调用方（结束输入/换行） */
    if (key == K_ENTER) {
        if (s_pinyin[0]) { commit_pinyin(); }
        return s_pinyin[0] != 0;
    }

    return false;
}

/* ---------------- 渲染 ---------------- */

/* 计算 UTF-8 文本像素宽（与 draw_text_utf8 同一规则：1B=半角 8px，多字节=全角 16px） */
static int text_px_w(const char* s, size_t n) {
    int w = 0;
    for (size_t i = 0; i < n;) {
        const uint8_t b = (uint8_t)s[i];
        size_t l = 1;
        if      (b >= 0xF0) l = 4;
        else if (b >= 0xE0) l = 3;
        else if (b >= 0xC0) l = 2;
        w += (l == 1) ? window::UIRenderer::FONT_W : window::UIRenderer::CJK_W;
        i += l;
    }
    return w;
}

void ime_draw_bar(window::UIRenderer* ui, int x, int y) {
    /* 整行文本：模式 + 拼音 + 批号 + 候选（F1..F4 提示） */
    static const char* kFnSel[4] = {"F1", "F2", "F3", "F4"};
    std::string line;
    line += '[';
    line += ime_mode_str();
    line += ']';
    line += ' ';
    line += s_pinyin;
    if (!s_hits.empty()) {
        const int pc = ime_page_count();
        char pg[24];
        std::snprintf(pg, sizeof(pg), " %d/%d ", s_page + 1, pc);
        line += pg;
        for (int i = 0; i < ime_candidate_count(); i++) {
            line += kFnSel[i];
            line += s_cand[i];
            line += ' ';
        }
    }

    /* 背景条 */
    ui->fill_rect({(int16_t)x, (int16_t)y, (int16_t)(SCREEN_W - 2 * x), 16}, COLOR_DARK_GRAY);
    ui->draw_rect({(int16_t)x, (int16_t)y, (int16_t)(SCREEN_W - 2 * x), 16}, COLOR_YELLOW, 1);

    /* 空拼音（模式条） */
    if (!s_pinyin[0]) {
        ui->draw_text_utf8(x + 2, y + 2, line, COLOR_LIGHT_GRAY);
        return;
    }
    if (s_hits.empty()) {
        ui->draw_text_utf8(x + 2, y + 2, line, COLOR_LIGHT_GRAY);
        return;
    }

    /* 整行白色画出 */
    ui->draw_text_utf8(x + 2, y + 2, line, COLOR_WHITE);

    /* 选中候选黄色重画：位置 = x+2 + 前缀宽度（模式/拼音/批号/前 i 个候选） */
    int cx = x + 2;
    std::string prefix;
    prefix += '[';
    prefix += ime_mode_str();
    prefix += "] ";
    prefix += s_pinyin;
    char pg[24];
    std::snprintf(pg, sizeof(pg), " %d/%d ", s_page + 1, ime_page_count());
    prefix += pg;
    cx += text_px_w(prefix.data(), prefix.size());
    for (int i = 0; i < s_sel; i++) {
        cx += text_px_w(kFnSel[i], 2);
        cx += text_px_w(s_cand[i], std::strlen(s_cand[i]));
        cx += window::UIRenderer::FONT_W;   /* 尾随空格 */
    }
    if (s_cand[s_sel][0])
        ui->draw_text_utf8(cx, y + 2, s_cand[s_sel], COLOR_YELLOW);
}

} // namespace ime
} // namespace memoria

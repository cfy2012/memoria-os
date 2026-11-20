/**
 * @file window.cpp
 * @brief 窗口管理器实现：摇杆导航 + 状态栏 + 自制 8×12 ASCII 字体 + 光标
 */

#include "window.hpp"
#include "gb2312_font.hpp"
#include "ascii_font8x12.hpp"
#include "ili9341.hpp"
#include "rtc_clock.hpp"
#include "wifi_manager.hpp"
#include "ble_manager.hpp"
#include "battery_adc.hpp"
#include "joystick.hpp"

extern "C" {
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
}

#include <algorithm>
#include <cstring>

namespace memoria {
namespace window {

using namespace drivers;

static const char* TAG = "WINDOW";

/* ============================================================
 *  WindowManager：顶层窗口栈 + 导航事件分发
 * ============================================================ */
WindowManager* WindowManager::instance() {
    static WindowManager inst;
    return &inst;
}

esp_err_t WindowManager::init() {
    /* 创建状态栏：常驻顶层窗口栈底部 */
    auto sb = std::make_shared<StatusBar>();
    sb->set_rect({0, 0, static_cast<int16_t>(SCREEN_W), 16});
    sb->set_visible(true);
    _stack.push_back(std::move(sb));

    ESP_LOGI(TAG, "WindowManager OK. SCREEN=%dx%d", SCREEN_W, SCREEN_H);
    return ESP_OK;
}

void WindowManager::push_top(std::shared_ptr<Window> w) {
    if (!w) return;
    /* 如果栈里已经有一个顶层不是状态栏：把它 pop 出栈
     * 但不销毁（留在 children 里），保持应用内页面栈 */
    if (_stack.size() > 1) {
        _stack.pop_back();  /* 移除上次 push 的顶层窗口（但仍持有 shared_ptr） */
    }
    _stack.push_back(std::move(w));
}

void WindowManager::pop_top() {
    if (_stack.size() > 1) {
        _stack.pop_back();  /* 只 pop 应用窗口，状态栏不动 */
    }
}

Window* WindowManager::top() {
    if (_stack.empty()) return nullptr;
    /* 返回除了状态栏以外的顶层窗口 */
    if (_stack.size() > 1) return _stack.back().get();
    return nullptr;
}

void WindowManager::dispatch_nav(const NavInput& ni) {
    /* 全局钩子优先 */
    for (auto& cb : _global_navs) {
        if (cb(ni)) return;  /* 被消费 */
    }
    _dispatch_to_stack(ni);
}

void WindowManager::dispatch_sys(SysEvent se) {
    for (auto& cb : _global_sys) {
        if (cb(se)) return;
    }
    _dispatch_sys_to_stack(se);
}

bool WindowManager::_dispatch_to_stack(const NavInput& ni) {
    /* 从栈顶（最新）往底（状态栏）遍历：Z-order 自动冒泡 */
    for (auto it = _stack.rbegin(); it != _stack.rend(); ++it) {
        if (!it->get() || !(*it)->visible()) continue;
        if ((*it)->on_nav(ni)) return true;
    }
    return false;
}

bool WindowManager::_dispatch_sys_to_stack(SysEvent se) {
    for (auto it = _stack.rbegin(); it != _stack.rend(); ++it) {
        if (!it->get() || !(*it)->visible()) continue;
        if ((*it)->on_sys(se)) return true;
    }
    return false;
}

void WindowManager::render_all() {
    /* 清空 framebuffer（黑色） */
    drivers::Ili9341::instance()->fill(COLOR_BLACK);

    /* 从底到顶渲染：先画状态栏，再画应用窗口 */
    for (auto& w : _stack) {
        if (w && w->visible()) w->on_render();
    }

    /* 推屏 */
    drivers::Ili9341::instance()->flush();
}

/* ============================================================
 *  StatusBar：16px 高状态栏
 * ============================================================ */
StatusBar::StatusBar() {
    _rect = {0, 0, static_cast<int16_t>(SCREEN_W), 16};
    set_rect(_rect);
}

void StatusBar::on_render() {
    auto* ui = UIRenderer::instance();
    ui->fill_rect(_rect, COLOR_BG);
    /* 底部 1px 分隔线（替掉原来的整圈细框） */
    ui->fill_rect({0, 15, static_cast<int16_t>(SCREEN_W), 1}, COLOR_STROKE);

    /* 品牌点 + 名称 */
    ui->fill_round_rect({6, 5, 5, 5}, 2, COLOR_ACCENT);
    ui->draw_text(15, 2, "MEMORIA", COLOR_TEXT_DIM);

    /* 连接状态：两个小圆点（实色=已连接，暗色=未连接） */
    const bool wifi_on = drivers::WifiManager::instance()->connected();
    const bool ble_on  = drivers::BleManager::instance()->connected();
    ui->fill_round_rect({84, 5, 6, 6}, 2, wifi_on ? COLOR_OK : COLOR_CARD_HI);
    ui->fill_round_rect({94, 5, 6, 6}, 2, ble_on ? COLOR_OK : COLOR_CARD_HI);

    /* 时间：居中 */
    auto t = drivers::RtcClock::instance()->now();
    char time_str[8];
    snprintf(time_str, sizeof(time_str), "%02u:%02u", t.hour, t.minute);
    const int tw = static_cast<int>(strlen(time_str)) * UIRenderer::FONT_W;
    ui->draw_text(static_cast<int16_t>(SCREEN_W / 2 - tw / 2), 2, time_str, COLOR_TEXT);

    /* 电量：百分比 + 胶囊电池 */
    uint8_t pct = drivers::BatteryAdc::instance()->percent();
    bool crit  = drivers::BatteryAdc::instance()->is_crit();
    const Color bat_col = crit ? COLOR_DANGER : (pct < 25 ? COLOR_WARN : COLOR_OK);
    const int16_t pill_x = static_cast<int16_t>(SCREEN_W - 8 - 24);
    char bat[8];
    snprintf(bat, sizeof(bat), "%u%%", pct);
    const int bw = static_cast<int>(strlen(bat)) * UIRenderer::FONT_W;
    ui->draw_text(static_cast<int16_t>(pill_x - 6 - bw), 2, bat, COLOR_TEXT_DIM);
    ui->draw_round_rect({pill_x, 4, 24, 9}, 3, COLOR_STROKE, 1);
    int fill_w = static_cast<int>((pct > 100 ? 100 : pct) * 18 / 100);
    if (fill_w < 2 && pct > 0) fill_w = 2;
    if (fill_w > 0) ui->fill_round_rect({static_cast<int16_t>(pill_x + 3), 6,
                                         static_cast<int16_t>(fill_w), 5}, 2, bat_col);
}

/* ============================================================
 *  UIRenderer：图元封装 + 自制 8×12 ASCII 字体
 * ============================================================ */

/* 8x12 ASCII 点阵字库由 build/gen_ascii_font.py 生成，见 ascii_font8x12.hpp */

UIRenderer* UIRenderer::instance() {
    static UIRenderer inst;
    return &inst;
}

void UIRenderer::clear(Color c) {
    drivers::Ili9341::instance()->fill(c);
}

void UIRenderer::flush() {
    drivers::Ili9341::instance()->flush();
}

void UIRenderer::fill_rect(const Rect& r, Color c) {
    drivers::Ili9341::instance()->fill_rect(r.x, r.y, r.w, r.h, c);
}

void UIRenderer::draw_rect(const Rect& r, Color c, uint16_t thickness) {
    if (thickness == 0) thickness = 1;
    if (thickness >= r.w || thickness >= r.h) { fill_rect(r, c); return; }
    const int16_t t = static_cast<int16_t>(thickness);
    const int16_t h2 = static_cast<int16_t>(r.h - 2 * t);
    /* 四条粗边：上下横带 + 左右竖带（t=1 时与逐线绘制等价） */
    fill_rect({r.x, r.y, r.w, t}, c);
    fill_rect({r.x, static_cast<int16_t>(r.y + r.h - t), r.w, t}, c);
    fill_rect({r.x, static_cast<int16_t>(r.y + t), t, h2}, c);
    fill_rect({static_cast<int16_t>(r.x + r.w - t), static_cast<int16_t>(r.y + t), t, h2}, c);
}

void UIRenderer::draw_cursor(const Rect& r, Color c, uint16_t thickness) {
    draw_rect(r, c, thickness);
    /* 闪烁效果由窗口应用层控制重绘频率 */
}

/* ---------- 圆角图元 ---------- */

/* 圆角行缩进：dy = 距上边缘行号（0..r-1），返回该行左右各缩进多少像素 */
static int corner_inset(int r, int dy) {
    if (r <= 0) return 0;
    const int yc = r - 1 - dy;              /* 距圆心的垂直距离 */
    const int inside = r * r - yc * yc;
    int dx = 0;
    while ((dx + 1) * (dx + 1) <= inside) dx++;
    return r - dx;
}

void UIRenderer::fill_round_rect(const Rect& r, int radius, Color c) {
    if (r.w <= 0 || r.h <= 0) return;
    int rad = radius;
    if (rad > r.w / 2) rad = r.w / 2;
    if (rad > r.h / 2) rad = r.h / 2;
    if (rad <= 0) { fill_rect(r, c); return; }

    auto* lcd = drivers::Ili9341::instance();
    for (int i = 0; i < r.h; i++) {
        int inset = 0;
        if (i < rad)                inset = corner_inset(rad, i);
        else if (i >= r.h - rad)    inset = corner_inset(rad, r.h - 1 - i);
        if (inset * 2 >= r.w) continue;
        lcd->fill_rect(static_cast<uint16_t>(r.x + inset), static_cast<uint16_t>(r.y + i),
                       static_cast<uint16_t>(r.w - inset * 2), 1, c);
    }
}

void UIRenderer::draw_round_rect(const Rect& r, int radius, Color c, uint16_t thickness) {
    if (r.w <= 0 || r.h <= 0) return;
    if (thickness == 0) thickness = 1;
    int rad = radius;
    if (rad > r.w / 2) rad = r.w / 2;
    if (rad > r.h / 2) rad = r.h / 2;
    if (rad <= 0) { draw_rect(r, c, thickness); return; }

    const int t = static_cast<int>(thickness);
    auto* lcd = drivers::Ili9341::instance();
    for (int i = 0; i < r.h; i++) {
        int inset = 0;
        if (i < rad)                inset = corner_inset(rad, i);
        else if (i >= r.h - rad)    inset = corner_inset(rad, r.h - 1 - i);
        const bool cap_row = (i < t) || (i >= r.h - t);
        if (cap_row) {
            /* 顶/底横带：整行铺满（含圆角缩进） */
            if (inset * 2 < r.w)
                lcd->fill_rect(static_cast<uint16_t>(r.x + inset), static_cast<uint16_t>(r.y + i),
                               static_cast<uint16_t>(r.w - inset * 2), 1, c);
        } else {
            /* 中间行：只画左右两条竖带 */
            lcd->fill_rect(static_cast<uint16_t>(r.x + inset), static_cast<uint16_t>(r.y + i),
                           static_cast<uint16_t>(t), 1, c);
            lcd->fill_rect(static_cast<uint16_t>(r.x + r.w - inset - t), static_cast<uint16_t>(r.y + i),
                           static_cast<uint16_t>(t), 1, c);
        }
    }
}

void UIRenderer::card(const Rect& r, int radius, Color fill, Color stroke) {
    fill_round_rect(r, radius, fill);
    if (stroke != fill) draw_round_rect(r, radius, stroke, 1);
}

void UIRenderer::progress(const Rect& r, int permille, Color fg, Color bg, int radius) {
    if (r.w <= 0 || r.h <= 0) return;
    if (permille < 0) permille = 0;
    if (permille > 1000) permille = 1000;
    fill_round_rect(r, radius, bg);
    const int w = static_cast<int>(r.w) * permille / 1000;
    if (w > 0) {
        Rect f{r.x, r.y, static_cast<int16_t>(w), r.h};
        fill_round_rect(f, radius, fg);
    }
}

void UIRenderer::divider_h(int16_t x, int16_t y, int16_t w, Color c) {
    fill_rect({x, y, w, 1}, c);
}

/* ---------- 缺字兜底 & 定宽文本 ---------- */

void UIRenderer::draw_image(const Rect& r, const uint16_t* rgb565_buf,
                              uint16_t img_w, uint16_t img_h) {
    if (!rgb565_buf) return;
    /* 简化：只画左上部分的一部分（缩放由 JPEG 解码时已完成） */
    (void)img_w; (void)img_h;
    auto* fb = drivers::Ili9341::instance()->framebuffer();
    /* 把 buf 直接拷到 framebuffer 对应位置 */
    /* 注意：这里不做缩放，假定调用者已准备好目标尺寸 buffer */
    uint16_t copy_w = std::min<uint16_t>(r.w, img_w);
    uint16_t copy_h = std::min<uint16_t>(r.h, img_h);
    for (uint16_t y = 0; y < copy_h; y++) {
        for (uint16_t x = 0; x < copy_w; x++) {
            uint16_t screen_x = r.x + x;
            uint16_t screen_y = r.y + y;
            if (screen_x < SCREEN_W && screen_y < SCREEN_H) {
                fb[screen_y * SCREEN_W + screen_x] = rgb565_buf[y * img_w + x];
            }
        }
    }
}

int UIRenderer::text_width_utf8(const std::string& text) {
    return text_width_utf8(text, 1);
}

int UIRenderer::text_width_utf8(const std::string& text, int scale) {
    /* UTF-8 混排像素宽度：ASCII 8px（FONT_W），多字节序列（中文 GB2312 字库）16px。
     * 用于绘制前居中/裁剪判定——禁止用字节数估算（UTF-8 中文 3 字节/字）。
     * scale 1~3：宽度按比例放大（scale<1 按 1） */
    if (scale < 1) scale = 1;
    int w = 0;
    size_t i = 0;
    while (i < text.size()) {
        const uint8_t b0 = static_cast<uint8_t>(text[i]);
        if (b0 < 0x80) {
            w += FONT_W;
            i += 1;
        } else if ((b0 & 0xE0) == 0xC0) {
            w += 2 * FONT_W; i += 2;
        } else if ((b0 & 0xF0) == 0xE0) {
            w += 2 * FONT_W; i += 3;
        } else if ((b0 & 0xF8) == 0xF0) {
            w += 2 * FONT_W; i += 4;
        } else {                        /* 非法字节：按全宽回退处理，前进 1 字节防死循环 */
            w += 2 * FONT_W;
            i += 1;
        }
    }
    return w * scale;
}

void UIRenderer::draw_text(int16_t x, int16_t y, const std::string& text, Color c) {
    /* 统一走 UTF-8 混合渲染：ASCII 8×12 字模 + 中文 GB2312 字库（TF 卡） */
    draw_text_utf8(x, y, text, c);
}

std::string UIRenderer::clip_text(const std::string& text, int max_px) {
    if (max_px <= 0) return std::string();
    if (text_width_utf8(text) <= max_px) return text;

    /* 预留 ".." 的宽度，保证裁剪后不超过 max_px */
    const int budget = max_px - 2 * FONT_W;
    if (budget <= 0) return std::string();

    std::string out;
    int w = 0;
    size_t i = 0;
    while (i < text.size()) {
        const uint8_t b0 = static_cast<uint8_t>(text[i]);
        size_t len = 1;
        int cw = FONT_W;
        if (b0 >= 0x80) {
            if ((b0 & 0xE0) == 0xC0)      len = 2;
            else if ((b0 & 0xF0) == 0xE0) len = 3;
            else if ((b0 & 0xF8) == 0xF0) len = 4;
            else                          len = 1;
            cw = 2 * FONT_W;
        }
        if (i + len > text.size()) break;
        if (w + cw > budget) break;
        out.append(text, i, len);
        w += cw;
        i += len;
    }
    return out + "..";
}

void UIRenderer::draw_text_fit(int16_t x, int16_t y, const std::string& text, Color c, int max_px) {
    draw_text(x, y, clip_text(text, max_px), c);
}

void UIRenderer::draw_text_utf8(int16_t x, int16_t y, const std::string& text, Color c) {
    draw_text_utf8(x, y, text, c, 1);
}

void UIRenderer::draw_text_utf8(int16_t x, int16_t y, const std::string& text, Color c, int scale) {
    if (scale < 1) scale = 1;
    if (scale > 3) scale = 3;
    auto* fb = drivers::Ili9341::instance()->framebuffer();
    if (!fb) return;
    auto* lcd = drivers::Ili9341::instance();

    int16_t cx = x;
    size_t i = 0;
    while (i < text.size()) {
        const uint8_t b0 = static_cast<uint8_t>(text[i]);
        if (b0 < 0x80) {
            /* ASCII：直接复用 8×12 */
            if (b0 >= 0x20 && b0 <= 0x7E) {
                const uint8_t* glyph = FONT_8x12[b0 - 0x20];
                for (int row = 0; row < FONT_H; row++) {
                    uint8_t line = glyph[row];
                    for (int col = 0; col < 8; col++) {
                        if (line & (0x80 >> col)) {
                            int16_t bx = cx + col * scale, by = y + row * scale;
                            for (int dy = 0; dy < scale; dy++)
                                for (int dx = 0; dx < scale; dx++) {
                                    int16_t sx = bx + dx, sy = by + dy;
                                    if (sx >= 0 && sx < SCREEN_W && sy >= 0 && sy < SCREEN_H)
                                        fb[sy * SCREEN_W + sx] = c;
                                }
                        }
                    }
                }
            }
            cx += FONT_W * scale;
            i++;
            continue;
        }

        /* UTF-8 多字节 → Unicode */
        uint32_t cp = 0;
        size_t len = 0;
        if ((b0 & 0xE0) == 0xC0)      { cp = b0 & 0x1F; len = 2; }
        else if ((b0 & 0xF0) == 0xE0) { cp = b0 & 0x0F; len = 3; }
        else if ((b0 & 0xF8) == 0xF0) { cp = b0 & 0x07; len = 4; }
        else { cx += FONT_W; i++; continue; }
        if (i + len > text.size()) break;
        for (size_t k = 1; k < len; k++)
            cp = (cp << 6) | (static_cast<uint8_t>(text[i + k]) & 0x3F);

        int idx = gb2312_index(cp);
        if (idx < 0) {
            /* 缺字：画一个小实心方块占位（原空心框在 1px 描边下像乱码） */
            const int16_t s = 4 * scale;
            lcd->fill_rect(static_cast<uint16_t>(cx + (CJK_W * scale - s) / 2),
                           static_cast<uint16_t>(y + (CJK_H * scale - s) / 2),
                           static_cast<uint16_t>(s), static_cast<uint16_t>(s), c);
            cx += CJK_W * scale;
            i += len;
            continue;
        }
        const uint8_t* glyph = gb2312_glyph(idx);
        if (!glyph) {
            /* 字库未加载（缺 TF 卡）或索引越界：与缺字一致 */
            const int16_t s = 4 * scale;
            lcd->fill_rect(static_cast<uint16_t>(cx + (CJK_W * scale - s) / 2),
                           static_cast<uint16_t>(y + (CJK_H * scale - s) / 2),
                           static_cast<uint16_t>(s), static_cast<uint16_t>(s), c);
            cx += CJK_W * scale;
            i += len;
            continue;
        }
        for (int row = 0; row < CJK_H; row++) {
            uint8_t hi = glyph[row * 2];
            uint8_t lo = glyph[row * 2 + 1];
            for (int col = 0; col < 16; col++) {
                uint8_t bit = (col < 8) ? (hi & (0x80 >> col)) : (lo & (0x80 >> (col - 8)));
                if (bit) {
                    int16_t bx = cx + col * scale, by = y + row * scale;
                    for (int dy = 0; dy < scale; dy++)
                        for (int dx = 0; dx < scale; dx++) {
                            int16_t sx = bx + dx, sy = by + dy;
                            if (sx >= 0 && sx < SCREEN_W && sy >= 0 && sy < SCREEN_H)
                                fb[sy * SCREEN_W + sx] = c;
                        }
                }
            }
        }
        cx += CJK_W * scale;
        i += len;
    }
}

} // namespace window
} // namespace memoria
/**
 * @file ili9341.hpp
 * @brief ILI9341 SPI 屏幕驱动：帧缓冲 + 基础图元 + DMA 推屏
 *
 * Color 类型 = uint16_t（RGB565），唯一定义于 drivers_config.hpp。
 */

#pragma once

#include "drivers_config.hpp"
#include <esp_err.h>
#include <memory>
#include <cstdint>

namespace memoria {
namespace drivers {

using Color = uint16_t;

class Ili9341 {
public:
    static Ili9341* instance();

    esp_err_t init();

    /* 背光（由 Power 管理的 PWM 通道，这里只提供开关） */
    void set_backlight(uint8_t brightness);
    uint8_t get_backlight() const { return _backlight; }

    /* 图元（全部写入 PSRAM framebuffer） */
    void fill(Color c);
    void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, Color c);
    void draw_pixel(uint16_t x, uint16_t y, Color c);
    void draw_hline(uint16_t x1, uint16_t x2, uint16_t y, Color c);
    void draw_vline(uint16_t x, uint16_t y1, uint16_t y2, Color c);
    void draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, Color c);
    void draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, Color c);

    /* 真正的 Bresenham 圆 */
    void draw_circle(int16_t cx, int16_t cy, int16_t r, Color c);
    void fill_circle(int16_t cx, int16_t cy, int16_t r, Color c);

    /* 推屏：把 framebuffer 整块 DMA 写到 SPI */
    void flush();

    /* 休眠 / 唤醒 */
    void sleep();
    void wake();

    /* 访问 */
    constexpr uint16_t width()  const { return SCREEN_W; }
    constexpr uint16_t height() const { return SCREEN_H; }
    Color* framebuffer()       { return _fb.get(); }
    const Color* framebuffer() const { return _fb.get(); }
    bool is_inited() const { return _inited; }

private:
    Ili9341() = default;

    void _write_cmd(uint8_t cmd);
    void _write_data(uint8_t data);
    void _set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);

    std::unique_ptr<Color[]> _fb;   /* PSRAM 分配 */
    uint8_t  _backlight = 255;
    bool     _inited   = false;
};

} // namespace drivers
} // namespace memoria
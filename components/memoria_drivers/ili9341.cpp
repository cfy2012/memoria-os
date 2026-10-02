/**
 * @file ili9341.cpp
 * @brief SPI 屏幕驱动（ILI9341/ILI9488）：帧缓冲 + DMA 分块推屏 + 基础图元
 *
 * SPI 事务必须拿 mutex，DC 由本模块显式 GPIO 控制，CS 由 SPI 管理器自动管理。
 */

#include "ili9341.hpp"
#include "spi_bus.hpp"
#include "power.hpp"

extern "C" {
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
}

#include <algorithm>
#include <cstring>
#include <cmath>

namespace memoria {
namespace drivers {

static const char* TAG = "LCD";

Ili9341* Ili9341::instance() {
    static Ili9341 inst;
    return &inst;
}

/* ---------- SPI 底层 ---------- */
/* 注：与 flush() 构成嵌套取锁，总线 mutex 为递归锁（见 spi_bus.cpp） */
void Ili9341::_write_cmd(uint8_t cmd) {
    auto* bus = SpiBus::instance();
    xSemaphoreTakeRecursive(bus->mutex(), portMAX_DELAY);
    gpio_set_level((gpio_num_t)LCD_DC_GPIO, 0);
    spi_transaction_t t{};
    t.length = 8; t.tx_buffer = &cmd;
    spi_device_polling_transmit(bus->handles().lcd, &t);
    xSemaphoreGiveRecursive(bus->mutex());
}

void Ili9341::_write_data(uint8_t data) {
    auto* bus = SpiBus::instance();
    xSemaphoreTakeRecursive(bus->mutex(), portMAX_DELAY);
    gpio_set_level((gpio_num_t)LCD_DC_GPIO, 1);
    spi_transaction_t t{};
    t.length = 8; t.tx_buffer = &data;
    spi_device_polling_transmit(bus->handles().lcd, &t);
    xSemaphoreGiveRecursive(bus->mutex());
}

void Ili9341::_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    _write_cmd(0x2A);
    _write_data(x1 >> 8); _write_data(x1 & 0xFF);
    _write_data(x2 >> 8); _write_data(x2 & 0xFF);
    _write_cmd(0x2B);
    _write_data(y1 >> 8); _write_data(y1 & 0xFF);
    _write_data(y2 >> 8); _write_data(y2 & 0xFF);
    _write_cmd(0x2C);
}

/* ---------- 初始化 ---------- */
esp_err_t Ili9341::init() {
    if (_inited) return ESP_OK;

    /* GPIO: DC + RST 输出 */
    gpio_config_t io{};
    io.pin_bit_mask = (1ULL << LCD_DC_GPIO) | (1ULL << LCD_RST_GPIO);
    io.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io);

    /* 硬件复位 */
    gpio_set_level((gpio_num_t)LCD_RST_GPIO, 1); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level((gpio_num_t)LCD_RST_GPIO, 0); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level((gpio_num_t)LCD_RST_GPIO, 1); vTaskDelay(pdMS_TO_TICKS(120));

    /* 初始化序列：按面板类型分支（drivers_config.hpp 的 PANEL_ILI9488） */
    if (PANEL_ILI9488) {
        /* ILI9488 3.5" 480×320 RGB565 */
        _write_cmd(0x01); vTaskDelay(pdMS_TO_TICKS(120));  /* SWRESET */
        _write_cmd(0x3A); _write_data(0x55);              /* COLMOD: 16bit/pixel */
        _write_cmd(0x36); _write_data(0x48);              /* MADCTL: RGB + landscape */
        _write_cmd(0xC0); _write_data(0x0D); _write_data(0x0D);
        _write_cmd(0xC1); _write_data(0x43); _write_data(0x43);
        _write_cmd(0xC2); _write_data(0x00);
        _write_cmd(0xC5); _write_data(0x00); _write_data(0x48);
                         _write_data(0x00); _write_data(0x48);
                         _write_data(0x00); _write_data(0x48);
        _write_cmd(0xE0);
        static const uint8_t ipg[] = {0x0F,0x1F,0x1C,0x0C,0x0F,0x08,0x48,
                                      0x98,0x37,0x0A,0x13,0x04,0x11,0x0D,0x00};
        for (auto b : ipg) _write_data(b);
        _write_cmd(0xE1);
        static const uint8_t ing[] = {0x0F,0x32,0x2E,0x0B,0x0D,0x05,0x47,
                                      0x23,0x6F,0x06,0x06,0x04,0x0F,0x0D,0x00};
        for (auto b : ing) _write_data(b);
        _write_cmd(0x11); vTaskDelay(pdMS_TO_TICKS(120));  /* Sleep Out */
        _write_cmd(0x29);                                  /* Display On */
    } else {
        /* ILI9341 2.4" 240×320 RGB565 */
        _write_cmd(0x01); vTaskDelay(pdMS_TO_TICKS(10));
        _write_cmd(0xCF); _write_data(0x00); _write_data(0xD9); _write_data(0x30);
        _write_cmd(0xED); _write_data(0x64); _write_data(0x03); _write_data(0x12); _write_data(0x81);
        _write_cmd(0xE8); _write_data(0x85); _write_data(0x00); _write_data(0x78);
        _write_cmd(0xC0); _write_data(0x23);
        _write_cmd(0xC1); _write_data(0x10);
        _write_cmd(0xC5); _write_data(0x3E); _write_data(0x28);
        _write_cmd(0xC7); _write_data(0x86);
        _write_cmd(0x36); _write_data(0x48);           /* MADCTL: RGB + Portrait */
        _write_cmd(0x3A); _write_data(0x55);           /* COLMOD: 16bit RGB565 */
        _write_cmd(0xB1); _write_data(0x00); _write_data(0x18);
        _write_cmd(0xB6); _write_data(0x08); _write_data(0x82); _write_data(0x27);
        _write_cmd(0xE0);
        static const uint8_t pg[] = {0x0F,0x31,0x2B,0x0C,0x0E,0x08,0x4E,0xF1,
                                     0x37,0x07,0x10,0x03,0x0E,0x09,0x00};
        for (auto b : pg) _write_data(b);
        _write_cmd(0xE1);
        static const uint8_t ng[] = {0x00,0x0E,0x14,0x03,0x11,0x07,0x31,0xC1,
                                     0x48,0x08,0x0F,0x0C,0x31,0x36,0x0F};
        for (auto b : ng) _write_data(b);
        _write_cmd(0x11); vTaskDelay(pdMS_TO_TICKS(120));  /* Sleep Out */
        _write_cmd(0x29);                                    /* Display On */
    }

    /* 分配 framebuffer：PSRAM，8bit 对齐 */
    size_t fb_bytes = SCREEN_W * SCREEN_H * sizeof(Color);
    void* mem = heap_caps_malloc(fb_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) {
        ESP_LOGE(TAG, "framebuffer alloc failed (%zu KB)", fb_bytes / 1024);
        return ESP_ERR_NO_MEM;
    }
    _fb.reset(static_cast<Color*>(mem));
    std::memset(_fb.get(), 0, fb_bytes);

    _backlight = 255;
    _inited = true;
    ESP_LOGI(TAG, "panel OK. framebuffer=%zu KB (%dx%d)", fb_bytes / 1024, SCREEN_W, SCREEN_H);
    return ESP_OK;
}

/* ---------- 背光（Power 模块用 LEDC PWM 控制 GPIO14，这里只存亮度值） ---------- */
void Ili9341::set_backlight(uint8_t brightness) {
    _backlight = brightness;
    /* 实际 PWM 写由 Power 模块完成 */
}

/* ---------- 图元 ---------- */
void Ili9341::fill(Color c) {
    if (!_fb) return;
    std::fill(_fb.get(), _fb.get() + SCREEN_W * SCREEN_H, c);
}

void Ili9341::fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, Color c) {
    if (!_fb) return;
    if (x + w > SCREEN_W)  w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    for (uint16_t row = y; row < y + h; row++) {
        Color* start = _fb.get() + row * SCREEN_W + x;
        std::fill(start, start + w, c);
    }
}

void Ili9341::draw_pixel(uint16_t x, uint16_t y, Color c) {
    if (!_fb || x >= SCREEN_W || y >= SCREEN_H) return;
    _fb[y * SCREEN_W + x] = c;
}

void Ili9341::draw_hline(uint16_t x1, uint16_t x2, uint16_t y, Color c) {
    if (!_fb || y >= SCREEN_H) return;
    if (x1 > x2) std::swap(x1, x2);
    if (x2 >= SCREEN_W) x2 = SCREEN_W - 1;
    Color* p = _fb.get() + y * SCREEN_W + x1;
    std::fill(p, p + (x2 - x1 + 1), c);
}

void Ili9341::draw_vline(uint16_t x, uint16_t y1, uint16_t y2, Color c) {
    if (!_fb || x >= SCREEN_W) return;
    if (y1 > y2) std::swap(y1, y2);
    if (y2 >= SCREEN_H) y2 = SCREEN_H - 1;
    for (uint16_t y = y1; y <= y2; y++) _fb[y * SCREEN_W + x] = c;
}

/* Bresenham 直线 */
void Ili9341::draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, Color c) {
    if (!_fb) return;
    int16_t dx = static_cast<int16_t>(std::abs(static_cast<int32_t>(x2) - x1));
    int16_t sx = x1 < x2 ? 1 : -1;
    int16_t dy = -static_cast<int16_t>(std::abs(static_cast<int32_t>(y2) - y1));
    int16_t sy = y1 < y2 ? 1 : -1;
    int16_t err = dx + dy;
    int16_t x = x1, y = y1;
    while (true) {
        draw_pixel(x, y, c);
        if (x == x2 && y == y2) break;
        int16_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x += sx; }
        if (e2 <= dx) { err += dx; y += sy; }
    }
}

void Ili9341::draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, Color c) {
    draw_hline(x, x + w - 1, y, c);
    draw_hline(x, x + w - 1, y + h - 1, c);
    draw_vline(x, y, y + h - 1, c);
    draw_vline(x + w - 1, y, y + h - 1, c);
}

/* Bresenham 画圆 */
void Ili9341::draw_circle(int16_t cx, int16_t cy, int16_t r, Color c) {
    if (!_fb) return;
    int16_t x = r, y = 0, d = 1 - r;
    while (x >= y) {
        auto px = [cx, cy](int16_t a, int16_t b) {
            return std::pair<uint16_t, uint16_t>(
                static_cast<uint16_t>(cx + a), static_cast<uint16_t>(cy + b));
        };
        auto draw8 = [&](int16_t a, int16_t b) {
            draw_pixel(cx + a, cy + b, c);
            draw_pixel(cx + b, cy + a, c);
            draw_pixel(cx - b, cy + a, c);
            draw_pixel(cx - a, cy + b, c);
            draw_pixel(cx - a, cy - b, c);
            draw_pixel(cx - b, cy - a, c);
            draw_pixel(cx + b, cy - a, c);
            draw_pixel(cx + a, cy - b, c);
        };
        draw8(x, y);
        if (d < 0) d += 2 * y + 3;
        else { d += 2 * (y - x) + 5; x--; }
        y++;
        (void)px;
    }
}

/* 扫描填充圆 */
void Ili9341::fill_circle(int16_t cx, int16_t cy, int16_t r, Color c) {
    if (!_fb) return;
    for (int16_t dy = -r; dy <= r; dy++) {
        int16_t dx = static_cast<int16_t>(std::sqrt(static_cast<double>(r*r - dy*dy)));
        int16_t y = cy + dy;
        if (y < 0 || y >= SCREEN_H) continue;
        int16_t x1 = cx - dx, x2 = cx + dx;
        if (x1 < 0) x1 = 0;
        if (x2 >= SCREEN_W) x2 = SCREEN_W - 1;
        Color* p = _fb.get() + y * SCREEN_W + x1;
        std::fill(p, p + (x2 - x1 + 1), c);
    }
}

/* ---------- 推屏：DMA 分块 SPI 写入 ---------- */
void Ili9341::flush() {
    if (!_fb) return;
    auto* bus = SpiBus::instance();
    if (!bus->is_inited()) return;

    xSemaphoreTakeRecursive(bus->mutex(), portMAX_DELAY);

    _set_window(0, 0, SCREEN_W - 1, SCREEN_H - 1);

    constexpr uint32_t CHUNK = 4096;
    uint32_t total = SCREEN_W * SCREEN_H * sizeof(Color);
    uint32_t offset = 0;
    gpio_set_level((gpio_num_t)LCD_DC_GPIO, 1);

    const uint8_t* raw = reinterpret_cast<const uint8_t*>(_fb.get());
    while (offset < total) {
        uint32_t chunk = std::min(CHUNK, total - offset);
        spi_transaction_t t{};
        t.length    = chunk * 8;
        t.tx_buffer = raw + offset;
        spi_device_polling_transmit(bus->handles().lcd, &t);
        offset += chunk;
    }

    xSemaphoreGiveRecursive(bus->mutex());
}

/* ---------- 休眠 ---------- */
void Ili9341::sleep() {
    _write_cmd(0x28);  /* Display Off */
    _write_cmd(0x10);  /* Sleep In */
}
void Ili9341::wake() {
    _write_cmd(0x11); vTaskDelay(pdMS_TO_TICKS(120));
    _write_cmd(0x29);
}

} // namespace drivers
} // namespace memoria
/* ============================================================
 * @file keyboard_driver.cpp
 * @brief 矩阵键盘扫描驱动（2× MCP23017 I2C 扩展器）
 *
 * 扫描协议：
 *   1) 行输出：对 MCP #1 的 GPA0-5 逐行拉低（其余行高）
 *   2) 列读取：MCP #1 GPB0-3（C0-C3）+ MCP #2 GPA0-5（C4-C9）
 *   3) 低电平 = 按下；状态变化后按 20ms 消抖窗确认（非阻塞，
 *      时间戳状态机，扫描轮间自然间隔累积，不阻塞扫描任务）
 *
 * 寄存器（MCP23017 datasheet）：
 *   IODIRA/B 0x00/0x01  方向（1=输入）
 *   GPPUA/B  0x0C/0x0D  内部上拉（1=使能）
 *   GPIOA/B  0x12/0x13  读输入电平 / 写输出电平
 * ============================================================ */
#include "keyboard_driver.hpp"
#include "keyboard_keymap.hpp"
#include "drivers_config.hpp"

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include <cstring>

namespace memoria::input {

namespace {

const char* TAG = "kbd_drv";

/* MCP23017 寄存器地址 */
constexpr uint8_t REG_IODIRA = 0x00;
constexpr uint8_t REG_IODIRB = 0x01;
constexpr uint8_t REG_GPPUA  = 0x0C;
constexpr uint8_t REG_GPPUB  = 0x0D;
constexpr uint8_t REG_GPIOA  = 0x12;
constexpr uint8_t REG_GPIOB  = 0x13;

/* 扫描参数 */
constexpr size_t MAX_ROWS = KEY_ROWS;   /* 6 */
constexpr size_t MAX_COLS = KEY_COLS;   /* 10 */
constexpr uint8_t ROW_MASK   = 0x3F;    /* #1 GPA0-5 = 行 */
constexpr uint8_t COL_A_MASK = 0x0F;    /* #1 GPB0-3 = C0-C3 */
constexpr uint8_t COL_B_MASK = 0x3F;    /* #2 GPA0-5 = C4-C9 */

/* 消抖窗：状态变化后须保持 DEBOUNCE_MS 才确认（与旧版 20ms 阻塞等效） */
constexpr uint32_t DEBOUNCE_MS = 20;

i2c_master_bus_handle_t s_bus = nullptr;
i2c_master_dev_handle_t s_mcp_rows = nullptr;   /* 0x20：行 + C0-C3 */
i2c_master_dev_handle_t s_mcp_cols = nullptr;   /* 0x21：C4-C9 */
KeyCallback s_cb = nullptr;
bool s_prev[MAX_ROWS][MAX_COLS] = {{false}};
/* 非阻塞消抖状态：pending_active 置位时 deadline 为确认时刻，
 * 到期且状态仍保持目标值才发事件；无符号回绕安全（FreeRTOS 惯用） */
bool s_pending_active[MAX_ROWS][MAX_COLS];
TickType_t s_pending_deadline[MAX_ROWS][MAX_COLS];
bool s_pending_val[MAX_ROWS][MAX_COLS];

esp_err_t mcp_write(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val) {
    uint8_t tx[2] = {reg, val};
    return i2c_master_transmit(dev, tx, sizeof(tx), pdMS_TO_TICKS(50));
}

esp_err_t mcp_read(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t& val) {
    return i2c_master_transmit_receive(dev, &reg, 1, &val, 1, pdMS_TO_TICKS(50));
}

/* 驱动一行：拉低 row 位，其余行保持高 */
esp_err_t drive_row(size_t r) {
    uint8_t gpioa = static_cast<uint8_t>(~((1u << r) & ROW_MASK));  /* 选中行 0，其余 1 */
    return mcp_write(s_mcp_rows, REG_GPIOA, gpioa);
}

/* 读 10 位列状态：bit0-3=C0-C3, bit4-9=C4-C9；低电平 = 按下
 * I2C 失败返回 bit15=1 标记（成功值域仅 bit0-9，不冲突），调用方保持原状态不产事件 */
uint16_t read_cols() {
    uint8_t a = 0, b = 0;
    if (mcp_read(s_mcp_rows, REG_GPIOB, a) != ESP_OK) return 0x8000;
    if (mcp_read(s_mcp_cols, REG_GPIOA, b) != ESP_OK) return 0x8000;
    return (uint16_t)((a & COL_A_MASK) | ((b & COL_B_MASK) << 4));
}

void emit(size_t r, size_t c, bool pressed) {
    if (!s_cb) return;
    RawKeyEvent evt;
    evt.row = static_cast<uint8_t>(r);
    evt.col = static_cast<uint8_t>(c);
    evt.pressed = pressed;
    s_cb(evt);
}

void scan_task(void*) {
    for (;;) {
        for (size_t r = 0; r < MAX_ROWS; ++r) {
            if (drive_row(r) != ESP_OK) { vTaskDelay(pdMS_TO_TICKS(10)); break; }
            vTaskDelay(pdMS_TO_TICKS(1));               /* 电平稳定 */
            const uint16_t cols = read_cols();
            if (cols & 0x8000) {                        /* I2C 失败：保持按键状态，跳过本行 */
                vTaskDelay(pdMS_TO_TICKS(10));
                break;
            }
            const TickType_t now = xTaskGetTickCount();
            for (size_t c = 0; c < MAX_COLS; ++c) {
                const bool now_pressed = ((cols >> c) & 1u) == 0;   /* 低电平 = 按下 */
                if (now_pressed == s_prev[r][c]) {
                    /* 状态回到原值：抖动结束，取消本键 pending */
                    s_pending_active[r][c] = false;
                    continue;
                }
                if (!s_pending_active[r][c]) {
                    /* 首次变化：记确认时刻与目标状态，不阻塞，等扫描轮自然确认 */
                    s_pending_active[r][c] = true;
                    s_pending_deadline[r][c] = now + pdMS_TO_TICKS(DEBOUNCE_MS);
                    s_pending_val[r][c] = now_pressed;
                    continue;
                }
                /* 消抖窗已到且状态仍保持目标值 → 确认；否则维持 pending 等下一轮 */
                if ((TickType_t)(now - s_pending_deadline[r][c]) < 0x80000000u
                    && now_pressed == s_pending_val[r][c]) {
                    s_prev[r][c] = now_pressed;
                    emit(r, c, now_pressed);
                    s_pending_active[r][c] = false;
                }
            }
            drive_row(r);   /* 恢复本行高（下轮由 drive_row 重新拉低） */
        }
    }
}

} /* namespace */

void keyboard_init(KeyCallback cb) {
    s_cb = cb;
    std::memset(s_prev, 0, sizeof(s_prev));
    std::memset(s_pending_active, 0, sizeof(s_pending_active));

    /* I2C 总线（唯一来源：drivers_config.hpp） */
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port     = static_cast<i2c_port_num_t>(drivers::KBD_I2C_PORT);
    bus_cfg.sda_io_num   = static_cast<gpio_num_t>(drivers::KBD_I2C_SDA);
    bus_cfg.scl_io_num   = static_cast<gpio_num_t>(drivers::KBD_I2C_SCL);
    bus_cfg.clk_source   = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    /* 纯同步用法（i2c_master_transmit/_receive）：队列深度必须为 0。
     * >0 会启用 IDF "experimental" 异步队列机制，ISR 取队列事务时
     * ops/data 可为毒值 → LoadProhibited panic（0xeadb8f6a 实证） */
    bus_cfg.trans_queue_depth = 0;
    bus_cfg.flags.enable_internal_pullup = true;
    if (i2c_new_master_bus(&bus_cfg, &s_bus) != ESP_OK) {
        ESP_LOGE(TAG, "i2c bus init failed (SDA=%d SCL=%d)", drivers::KBD_I2C_SDA, drivers::KBD_I2C_SCL);
        return;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.scl_speed_hz    = drivers::KBD_I2C_FREQ_HZ;

    dev_cfg.device_address = drivers::KBD_MCP_ROWS;
    if (i2c_master_bus_add_device(s_bus, &dev_cfg, &s_mcp_rows) != ESP_OK) {
        ESP_LOGE(TAG, "mcp 0x%02X add failed", drivers::KBD_MCP_ROWS);
        return;
    }
    dev_cfg.device_address = drivers::KBD_MCP_COLS;
    if (i2c_master_bus_add_device(s_bus, &dev_cfg, &s_mcp_cols) != ESP_OK) {
        ESP_LOGE(TAG, "mcp 0x%02X add failed", drivers::KBD_MCP_COLS);
        return;
    }

    /* #1：GPA 全输出（行），GPB 全输入；C0-C3 上拉 */
    esp_err_t e = ESP_OK;
    e |= mcp_write(s_mcp_rows, REG_IODIRA, 0x00);
    e |= mcp_write(s_mcp_rows, REG_IODIRB, 0xFF);
    e |= mcp_write(s_mcp_rows, REG_GPPUB,  COL_A_MASK);
    e |= mcp_write(s_mcp_rows, REG_GPIOA,  0xFF);    /* 行默认全高 */

    /* #2：GPA 全输入（列 C4-C9 上拉） */
    e |= mcp_write(s_mcp_cols, REG_IODIRA, 0xFF);
    e |= mcp_write(s_mcp_cols, REG_GPPUA,  COL_B_MASK);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "mcp23017 init write failed: 0x%x", e);
        return;
    }

    ESP_LOGI(TAG, "2x MCP23017 ready: rows@0x%02X cols@0x%02X (%dx%d)",
             drivers::KBD_MCP_ROWS, drivers::KBD_MCP_COLS, (int)MAX_ROWS, (int)MAX_COLS);
}

void keyboard_start() {
    xTaskCreatePinnedToCore(scan_task, "kbd_scan", 4096, nullptr, 5, nullptr, 1);
}

} /* namespace memoria::input */
/**
 * @file drivers_config.hpp
 * @brief Memoria OS 硬件引脚唯一真实来源
 *
 * 所有驱动/应用必须从此文件读取引脚号/分辨率/时钟频率。
 * 禁止在其他文件里硬编码 GPIO 数字。
 *
 * 硬件：ESP32-S3-WROOM-2 N16R8
 *   16MB Flash (Octal 80MHz) + 8MB PSRAM (Octal 80MHz)
 *   1× ILI9488 SPI 屏幕（480×320 RGB565，无触摸）
 *   1× KY-023 模拟摇杆（X/Y 双 ADC + 按下按钮）
 *   1× MAX98357A I2S 功放 + 3W 喇叭
 *   1× SPI TF 卡（FAT32）
 *   电池：3.7V LiPo + TP4056；ADC 分压采样
 *   WiFi + BLE 内部射频
 */

#pragma once

#include <cstdint>

namespace memoria {
namespace drivers {

/* ============================================================
 *  全局屏幕分辨率 & 色彩
 *  默认面板：ILI9488 3.5寸 480×320（横屏，RGB565）
 *  如需换回 ILI9341 320×240：把 PANEL_ILI9488 置 false 并恢复 320/240
 * ============================================================ */
static constexpr bool     PANEL_ILI9488 = true;
static constexpr uint16_t SCREEN_W      = 480;
static constexpr uint16_t SCREEN_H      = 320;
static constexpr uint16_t COLOR_BLACK    = 0x0000;
static constexpr uint16_t COLOR_WHITE    = 0xFFFF;
static constexpr uint16_t COLOR_SOFT_ROSE= 0xF3B6;   /* RGB565 243/182/199 */
static constexpr uint16_t COLOR_DEEP_BLUE = 0x1C4F;
static constexpr uint16_t COLOR_LIGHT_GRAY= 0xBDF7;
static constexpr uint16_t COLOR_DARK_GRAY = 0x4208;
static constexpr uint16_t COLOR_RED       = 0xF800;
static constexpr uint16_t COLOR_GREEN      = 0x07E0;
static constexpr uint16_t COLOR_YELLOW    = 0xFFE0;

/* ============================================================
 *  SPI 共用总线 (SPI2_HOST)
 * ============================================================ */
static constexpr int SPI_BUS_HOST = 2;    /* SPI2_HOST */
static constexpr int SPI_SCLK_GPIO = 12;
static constexpr int SPI_MOSI_GPIO = 11;
static constexpr int SPI_MISO_GPIO = 13; /* 屏幕/SD 都是半双工，不需要 MISO 时悬空也安全 */
static constexpr int SPI_BUS_FREQ_HZ = 26000000;  /* 26MHz，SD 卡 SPI 上限 25MHz，总线保持保守 */

/* ============================================================
 *  ILI9341 屏幕（仅一块，共用 SPI）
 * ============================================================ */
static constexpr int LCD_CS_GPIO   = 10;
static constexpr int LCD_DC_GPIO   = 9;
static constexpr int LCD_RST_GPIO  = 8;
static constexpr int LCD_BLK_GPIO  = 14;   /* LEDC PWM 背光 */

/* ============================================================
 *  KY-023 模拟摇杆
 *  2026-10-01 引脚迁移（GPIO-ALLOC §2）：
 *  X 36→7 (ADC1_CH6)、Y 37→5 (ADC1_CH4)。
 *  原因：N16R8 开启 Octal PSRAM 后 GPIO33~37 被内部占用，
 *  且 ADC 仅 GPIO1~10 (ADC1) 可用，ADC2 与 WiFi 冲突禁用。
 * ============================================================ */
static constexpr int JOY_X_ADC_CHANNEL = 6;   /* GPIO7  ADC1_CH6 */
static constexpr int JOY_X_GPIO        = 7;
static constexpr int JOY_Y_ADC_CHANNEL = 4;   /* GPIO5  ADC1_CH4 */
static constexpr int JOY_Y_GPIO        = 5;
static constexpr int JOY_BTN_GPIO      = 38;  /* 摇杆按下按钮 */

/* ============================================================
 *  SD 卡 SPI（共用 SPI 总线）
 * ============================================================ */
static constexpr int SD_CS_GPIO = 17;  /* 2026-10-01 迁移：5→17（GPIO-ALLOC §2） */

/* ============================================================
 *  MAX98357A I2S 功放
 * ============================================================ */
static constexpr int I2S_PORT      = 0;
static constexpr int I2S_BCLK_GPIO = 18;  /* 2026-10-01 迁移：4→18 */
static constexpr int I2S_WS_GPIO   = 21;  /* 2026-10-01 迁移：3→21 */
static constexpr int I2S_DOUT_GPIO = 15;  /* 2026-10-01 迁移：2→15 */

/* ============================================================
 *  INMP441 数字麦克风（I2S RX）
 *  BCLK/WS 与功放共用（同 I2S0 端口，RX 只加数据脚）
 *  INMP441 数据在 WS 低电平有效（左声道），24-bit 左对齐
 *  SD 脚 GPIO16（GPIO-ALLOC/WIRING 预留池转正；45 是 strapping 脚弃用）
 * ============================================================ */
static constexpr int MIC_SD_GPIO   = 16;  /* GPIO16 数据脚（账本预留转已用） */

/* ============================================================
 *  电池 ADC 采样
 *  分压：LiPo 最高 4.2V → ADC 最大 1.2V
 *  分压电阻 R1=100K (接电池+), R2=100K (接GND)
 *  VADC = VBAT * R2/(R1+R2) = VBAT/2
 *  实际电池电压 = VADC * 2
 * ============================================================ */
static constexpr int BATTERY_ADC_CHANNEL = 3;  /* GPIO4  ADC1_CH3（S3 通道映射） */
static constexpr int BATTERY_GPIO        = 4;  /* 2026-10-01 迁移：39→4（GPIO39 无 ADC） */
static constexpr float BATTERY_DIV_RATIO  = 2.0f;
static constexpr float BATTERY_VOLTAGE_FULL   = 4.2f;
static constexpr float BATTERY_VOLTAGE_EMPTY  = 3.3f;
static constexpr float BATTERY_LOW_WARN_VOL   = 3.45f;   /* 15% 左右 */
static constexpr float BATTERY_LOW_CRIT_VOL   = 3.35f;   /* 5% 左右 */

/* ============================================================
 *  LEDC 通道分配
 * ============================================================ */
static constexpr int LEDC_TIMER_BACKLIGHT = 0;
static constexpr int LEDC_CHN_BACKLIGHT   = 0;

/* ============================================================
 *  6×10 矩阵键盘（2× MCP23017 I2C 扩展器扫描，2026-10-01 最终定版）
 *
 *  方案：键盘矩阵经 I2C 扩展器扫描，不占用任何 ESP32-S3 GPIO。
 *  MCP23017 #1 (0x20)：GPA0-5 = 行 R0-R5（输出，逐行拉低）
 *                      GPB0-3 = 列 C0-C3（输入，内部上拉，低=按下）
 *  MCP23017 #2 (0x21)：GPA0-5 = 列 C4-C9（输入，内部上拉）
 *  无 INT 脚，20ms 轮询消抖（TCA8418 因采购不可得弃用）。
 *
 *  I2C 总线复用原键盘行脚 GPIO1/GPIO6（已从矩阵释放）。
 * ============================================================ */
static constexpr int  KBD_I2C_PORT     = 0;        /* I2C_NUM_0 */
static constexpr int  KBD_I2C_SDA      = 1;
static constexpr int  KBD_I2C_SCL      = 6;
static constexpr int  KBD_I2C_FREQ_HZ  = 400000;   /* MCP23017 支持 400kHz fast mode */
static constexpr uint8_t KBD_MCP_ROWS  = 0x20;     /* 行 R0-R5 + 列 C0-C3 */
static constexpr uint8_t KBD_MCP_COLS  = 0x21;     /* 列 C4-C9 */

/* 键盘背光灯带（WS2812，单线 RMT）：GPIO46，上下两条 */
static constexpr int KEY_LED_GPIO     = 46;

} // namespace drivers
} // namespace memoria

#pragma once
/* ============================================================
 * @file keyboard_driver.hpp
 * @brief 矩阵键盘扫描驱动（2× MCP23017 I2C 扩展器）
 *
 * 硬件：
 *   MCP23017 #1 (0x20)：GPA0-5 = 行 R0-R5（输出）
 *                       GPB0-3 = 列 C0-C3（输入内部上拉）
 *   MCP23017 #2 (0x21)：GPA0-5 = 列 C4-C9（输入内部上拉）
 *
 * 扫描：逐行拉低 → 读两片列寄存器 → 低电平 = 按下
 * 消抖：状态变化后延时回读确认，输出 raw(row,col,pressed) 事件
 * 引脚与地址唯一来源：drivers_config.hpp（KBD_I2C_* / KBD_MCP_*）
 * ============================================================ */
#include <cstdint>
#include <cstddef>

namespace memoria::input {

struct RawKeyEvent {
  uint8_t row;
  uint8_t col;
  bool pressed;
};

using KeyCallback = void (*)(const RawKeyEvent& evt);

/* 初始化 I2C 总线 + 两片 MCP23017（行输出、列输入上拉） */
void keyboard_init(KeyCallback cb);

/* 启动后台扫描任务（FreeRTOS，固定 20ms 轮询） */
void keyboard_start();

/* 深睡眠前调用：两片 MCP23017 全引脚转输入（高阻）+ 输出清零 → 最低功耗态。
 * 唤醒后 keyboard_init() 会重新完整配置，无需恢复逻辑。 */
void keyboard_reset();

}
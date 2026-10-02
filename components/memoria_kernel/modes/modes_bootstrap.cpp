/**
 * @file modes_bootstrap.cpp
 * @brief 卡西欧范式模式注册入口
 *
 * 按模拟器主菜单顺序注册 11 个模式：
 * PHOTO MUSIC VIDEO CLOCK NOTES CALC PRGM STORE SETUP FMEM SYS
 */

#include "mode_manager.hpp"

namespace memoria {
using namespace drivers;
namespace modes {

/* 各模式注册函数（modes 目录下各 .cpp 文件） */
void photo_mode_register();
void music_mode_register();
void video_mode_register();
void clock_mode_register();
void notes_mode_register();
void calc_mode_register();
void prgm_mode_register();
void store_mode_register();
void setup_mode_register();
void fmem_mode_register();
void sys_mode_register();

void memoria_modes_register_all() {
    photo_mode_register();
    music_mode_register();
    video_mode_register();
    clock_mode_register();
    notes_mode_register();
    calc_mode_register();
    prgm_mode_register();
    store_mode_register();
    setup_mode_register();
    fmem_mode_register();
    sys_mode_register();
}

} // namespace modes
} // namespace memoria
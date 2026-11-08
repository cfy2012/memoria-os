/**
 * @file vault.hpp
 * @brief 用户数据保管：全部用户数据落在 TF 卡 /<mount>/sys 下
 *
 *   sys/nvs.bin      七个 NVS 命名空间的镜像（设置/桌面/包索引/WiFi/OTA/电源/BLE）
 *   sys/privfs.bin   私有文件系统镜像（应用包、存档、笔记、照片、录音）
 *
 * 卡不可用时全部降级回 flash（NVS 分区 + mem_priv 分区），系统照常能开。
 */

#pragma once

#include <esp_err.h>
#include <string>

namespace memoria {
namespace vault {

/* 建 sys 目录、探测可写；若 NVS 被擦空而卡上有镜像则回灌。卡不可用返回 ESP_FAIL。 */
esp_err_t prepare(const char* mount_point);

/* 卡数据是否可用（prepare 的结果） */
bool ready();

/* 卡上数据根目录，未 prepare 时为空串 */
const std::string& root();

/* sys 下某个文件的完整路径 */
std::string path(const char* name);

/* 立刻把 NVS 七个命名空间镜像到 sys/nvs.bin（临时文件 + rename） */
bool mirror_save();

/* 主循环调用：距上次镜像超过 60 秒则自动存一次 */
void tick();

/* "card" 或 "flash"，给 shell / 日志用 */
const char* state();

} }

/**
 * @file package_manager.hpp
 * @brief 公开接口
 */
#pragma once
#include <string>
#include <cstdint>

namespace memoria {
namespace package {

/* NVS 持久化 */
std::string get_mirror_url();
bool set_mirror_url(const std::string& url);
bool get_auto_update();
void set_auto_update(bool on);
uint8_t get_auto_hour();
void set_auto_hour(uint8_t hh);
uint8_t get_auto_min();
void set_auto_min(uint8_t mm);

/* CRC32（供外部复用） */
uint32_t crc32_compute(const uint8_t* data, size_t len);

/* 检查 + 下载 + 校验 + 安装 */
bool check_updates(int* out_added = nullptr, int* out_removed = nullptr);

} }
/**
 * @file package_format.hpp
 */
#pragma once
#include <string>
#include <cstdint>
#include <vector>

namespace memoria {
namespace package {
int  create_package(const std::string& name, uint32_t version,
                    const uint8_t* payload, uint32_t payload_len,
                    const std::string& out_path);
bool verify_package(const std::string& path, std::string* out_name = nullptr,
                    uint32_t* out_version = nullptr,
                    std::vector<uint8_t>* out_payload = nullptr);
} }

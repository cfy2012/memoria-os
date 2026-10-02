/**
 * @file json_fetcher.hpp
 * @brief 公开接口
 */
#pragma once
#include <string>
#include <cstdint>

namespace memoria {
namespace package {

class json_fetcher {
public:
    /* 拉取 URL 内容，支持从 resume_from 字节处续传 */
    static std::string fetch(const std::string& url, uint32_t resume_from = 0);
};

} }
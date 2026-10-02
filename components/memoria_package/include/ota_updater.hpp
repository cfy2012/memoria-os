/**
 * @file ota_updater.hpp
 */
#pragma once
#include <string>

namespace memoria {
namespace package {
std::string get_ota_url();
bool set_ota_url(const std::string& url);
int  ota_firmware(const std::string& url = "");
} }

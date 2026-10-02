/**
 * @file app_manager.hpp
 * @brief 应用注册表 + 生命周期：基于 FreeRTOS 任务
 *
 * 直接使用 FreeRTOS 原生 TaskHandle_t；回调统一以 std::function 包装 C++ 对象。
 */

#pragma once

#include <esp_err.h>
#include <cstdint>
#include <string>
#include <functional>
#include <memory>
#include <cstring>

extern "C" {
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
}

namespace memoria {
namespace kernel {

/* 运行时实例 */
struct AppInstance;

using AppCreateFn  = std::function<esp_err_t(AppInstance*&)>;
using AppDestroyFn = std::function<void(AppInstance*)>;
using AppMainFn    = std::function<void(AppInstance*)>;

struct AppDescriptor {
    const char*   id           = nullptr;
    const char*   display_name = nullptr;
    uint16_t      icon_color   = 0;
    AppCreateFn   create;
    AppDestroyFn  destroy;
    AppMainFn     main;
};

struct AppInstance {
    const AppDescriptor* desc        = nullptr;
    TaskHandle_t         task_handle = nullptr;
    enum class State { None, Running, Paused, Sleeping, Destroyed } state = State::None;
    int                  stack_size  = 4096;
    int                  priority    = 3;
    void*                priv        = nullptr;
};

inline constexpr int MAX_RUNNING_APPS  = 4;
inline constexpr int MAX_REGISTERED    = 16;

class AppManager {
public:
    static AppManager* instance();

    esp_err_t register_app(const AppDescriptor& desc);

    const AppDescriptor* find(const std::string& id) const;
    void foreach(std::function<bool(const AppDescriptor&)> cb) const;
    int  count() const { return _count; }
    const AppDescriptor* get(int i) const;

private:
    AppManager() = default;
    AppDescriptor _registry[MAX_REGISTERED]{};
    int _count = 0;
};

class AppLifecycle {
public:
    static AppLifecycle* instance();

    esp_err_t start(const std::string& id, AppInstance** out = nullptr);
    esp_err_t pause (AppInstance* inst);
    esp_err_t resume(AppInstance* inst);
    esp_err_t sleep (AppInstance* inst);
    esp_err_t destroy(AppInstance* inst);

    int  list_active(AppInstance** out_list, int max_count);
    void shutdown_all();

private:
    AppLifecycle() = default;
    AppInstance* _instances[MAX_RUNNING_APPS]{};
    int _instance_count = 0;
};

} // namespace kernel
} // namespace memoria
/**
 * @file app_manager.cpp
 * @brief AppManager (注册表) + AppLifecycle (基于 FreeRTOS 任务)
 *
 * 关键约束：FreeRTOS xTaskCreate 只接受 C 函数指针，
 * 所以每个应用的 AppInstance::main 执行必须通过一个静态 C 入口。
 * 这里用一个统一的 app_task_wrapper 包装，通过 priv 指针拿到实例。
 */

#include "app_manager.hpp"

extern "C" {
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
}

#include <cstdlib>

#include <algorithm>

namespace memoria {
namespace kernel {

static const char* TAG = "APP";

/* ============================================================
 *  AppManager
 * ============================================================ */
AppManager* AppManager::instance() {
    static AppManager inst;
    return &inst;
}

esp_err_t AppManager::register_app(const AppDescriptor& desc) {
    if (!desc.id) return ESP_ERR_INVALID_ARG;
    for (int i = 0; i < _count; i++) {
        if (std::strcmp(_registry[i].id, desc.id) == 0) {
            ESP_LOGW(TAG, "'%s' already registered", desc.id);
            return ESP_OK;
        }
    }
    if (_count >= MAX_REGISTERED) {
        ESP_LOGE(TAG, "registry full (%d)", MAX_REGISTERED);
        return ESP_ERR_NO_MEM;
    }
    _registry[_count++] = desc;
    ESP_LOGI(TAG, "registered [%d] '%s' (%s)", _count, desc.id, desc.display_name);
    return ESP_OK;
}

const AppDescriptor* AppManager::find(const std::string& id) const {
    for (int i = 0; i < _count; i++) {
        if (std::strcmp(_registry[i].id, id.c_str()) == 0) return &_registry[i];
    }
    return nullptr;
}

void AppManager::foreach(std::function<bool(const AppDescriptor&)> cb) const {
    if (!cb) return;
    for (int i = 0; i < _count && cb(_registry[i]); i++) {}
}

const AppDescriptor* AppManager::get(int index) const {
    return (index >= 0 && index < _count) ? &_registry[index] : nullptr;
}

/* ============================================================
 *  AppLifecycle
 * ============================================================ */
AppLifecycle* AppLifecycle::instance() {
    static AppLifecycle inst;
    return &inst;
}

/* FreeRTOS 静态 C 入口：调 C++ 的 AppInstance::desc->main(inst) */
static void app_task_entry(void* arg) {
    auto* inst = static_cast<AppInstance*>(arg);
    if (inst && inst->desc && inst->desc->main) {
        inst->desc->main(inst);
    }
    inst->state = AppInstance::State::Destroyed;
    vTaskDelete(nullptr);
}

esp_err_t AppLifecycle::start(const std::string& id, AppInstance** out) {
    const AppDescriptor* desc = AppManager::instance()->find(id);
    if (!desc) {
        ESP_LOGE(TAG, "'%s' not found", id.c_str());
        return ESP_ERR_NOT_FOUND;
    }

    /* 已在运行？ */
    for (int i = 0; i < MAX_RUNNING_APPS; i++) {
        auto* inst = _instances[i];
        if (inst && inst->desc == desc && inst->state != AppInstance::State::Destroyed) {
            if (out) *out = inst;
            return ESP_OK;
        }
    }

    /* 找空槽；顺带回收已自杀的应用（main 已返回、任务已删，堆实例可安全释放）。
     * 契约：应用 main() 返回前自清资源，故回收不调 desc->destroy、不碰已悬垂的 task_handle。 */
    int slot = -1;
    for (int i = 0; i < MAX_RUNNING_APPS; i++) {
        auto* old = _instances[i];
        if (!old) { if (slot < 0) slot = i; continue; }
        if (old->state == AppInstance::State::Destroyed) {
            ESP_LOGI(TAG, "reclaim dead slot '%s'", old->desc ? old->desc->id : "?");
            delete old;
            _instances[i] = nullptr;
            _instance_count--;
            if (slot < 0) slot = i;
        }
    }
    if (slot < 0) {
        ESP_LOGE(TAG, "max running apps reached (%d)", MAX_RUNNING_APPS);
        return ESP_ERR_NO_MEM;
    }

    auto* inst = new AppInstance();
    inst->desc = desc;
    inst->state = AppInstance::State::Running;

    if (desc->create) {
        esp_err_t ret = desc->create(inst);
        if (ret != ESP_OK) {
            delete inst;
            return ret;
        }
    }

    /* P1 双核：应用逻辑任务钉 core1（core0 留给系统/协议栈/UI，core1 不再闲坐） */
    BaseType_t ok = xTaskCreatePinnedToCore(app_task_entry, desc->id,
                                inst->stack_size, inst,
                                inst->priority, &inst->task_handle, 1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate '%s' failed", desc->id);
        if (desc->destroy) desc->destroy(inst);
        delete inst;
        return ESP_ERR_NO_MEM;
    }

    _instances[slot] = inst;
    _instance_count++;
    ESP_LOGI(TAG, "started '%s'", desc->id);
    if (out) *out = inst;
    return ESP_OK;
}

esp_err_t AppLifecycle::pause(AppInstance* inst) {
    if (!inst || inst->state != AppInstance::State::Running)
        return ESP_ERR_INVALID_STATE;
    vTaskSuspend(inst->task_handle);
    inst->state = AppInstance::State::Paused;
    ESP_LOGI(TAG, "paused '%s'", inst->desc->id);
    return ESP_OK;
}

esp_err_t AppLifecycle::resume(AppInstance* inst) {
    if (!inst || inst->state != AppInstance::State::Paused)
        return ESP_ERR_INVALID_STATE;
    vTaskResume(inst->task_handle);
    inst->state = AppInstance::State::Running;
    ESP_LOGI(TAG, "resumed '%s'", inst->desc->id);
    return ESP_OK;
}

esp_err_t AppLifecycle::sleep(AppInstance* inst) {
    if (!inst) return ESP_ERR_INVALID_ARG;
    if (inst->state == AppInstance::State::Running) vTaskSuspend(inst->task_handle);
    inst->state = AppInstance::State::Sleeping;
    ESP_LOGI(TAG, "sleeping '%s'", inst->desc->id);
    return ESP_OK;
}

esp_err_t AppLifecycle::destroy(AppInstance* inst) {
    if (!inst) return ESP_ERR_INVALID_ARG;
    ESP_LOGI(TAG, "destroying '%s'", inst->desc->id);

    if (inst->task_handle && inst->state != AppInstance::State::Destroyed) {
        if (eTaskGetState(inst->task_handle) == eSuspended) {
            vTaskResume(inst->task_handle);
        }
        vTaskDelete(inst->task_handle);
    }

    if (inst->desc && inst->desc->destroy) inst->desc->destroy(inst);

    for (int i = 0; i < MAX_RUNNING_APPS; i++) {
        if (_instances[i] == inst) {
            _instances[i] = nullptr;
            _instance_count--;
            break;
        }
    }
    delete inst;
    return ESP_OK;
}

int AppLifecycle::list_active(AppInstance** out_list, int max_count) {
    int count = 0;
    for (int i = 0; i < MAX_RUNNING_APPS && count < max_count; i++) {
        if (_instances[i] && _instances[i]->state != AppInstance::State::Destroyed) {
            out_list[count++] = _instances[i];
        }
    }
    return count;
}

void AppLifecycle::shutdown_all() {
    for (int i = 0; i < MAX_RUNNING_APPS; i++) {
        if (_instances[i]) destroy(_instances[i]);
    }
}

} // namespace kernel
} // namespace memoria
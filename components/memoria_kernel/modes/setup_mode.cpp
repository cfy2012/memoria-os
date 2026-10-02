/**
 * @file setup_mode.cpp
 * @brief SETUP 设置模式
 *
 * 卡西欧式"分组 + 循环切换"：左右循环改值，Enter 保存（NVS）。
 * 与旧 settings_app 共用 NVS namespace "settings"（backlight/volume/auto_update）。
 */

#include "mode_manager.hpp"
#include "ili9341.hpp"
#include "i2s_audio.hpp"
#include "package_manager.hpp"
#include "firmware_update.hpp"

extern "C" {
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
}

#include <cstdio>
#include <cstring>

#include <string>
#include <vector>
#include <algorithm>

namespace memoria {
using namespace drivers;
namespace modes {

static const char* NVS_NS = "settings";

class SetupMode : public ModeWindow {
public:
    SetupMode() { _load(); }

    void mode_render(window::UIRenderer* ui, bool focused) override {
        draw_title(ui, "SETUP 设置", std::to_string(_cur + 1).append("/").append(std::to_string(_items.size())).c_str());

        const Item& it = _items[_cur];
        ui->draw_text(6, 44, it.name.c_str(), COLOR_LIGHT_GRAY);
        char val[24];
        snprintf(val, sizeof(val), "%s", it.value().c_str());
        ui->draw_text((SCREEN_W - 8 - strlen(val) * 8) / 2, 60, val, COLOR_YELLOW);

        /* 分组指示条 */
        ui->fill_rect({6, 82, 18, 3}, _group_color(_cur));
        ui->draw_text(6, 96, _group_name(_cur), COLOR_WHITE);

        draw_hint(ui, focused ? "左右 切换值 · Enter 保存 · 长按返回" : "");
    }

    bool mode_nav(const window::NavInput& ni) override {
        switch (ni.dir) {
            case window::NavEvent::NavUp:    _cur = (_cur - 1 + (int)_items.size()) % _items.size(); break;
            case window::NavEvent::NavDown:  _cur = (_cur + 1) % _items.size(); break;
            case window::NavEvent::NavLeft:  _items[_cur].change(-1); break;
            case window::NavEvent::NavRight: _items[_cur].change(+1); break;
            case window::NavEvent::NavEnter:
                if (_items[_cur].enter) _items[_cur].enter();
                else _save(_cur);
                break;
            default: return false;
        }
        return true;
    }

    const char* const* fn_labels() override {
        static const char* f[6] = {"-", "确认", "+", "默认", "保存", "菜单"};
        return f;
    }

    void on_fn(int idx) override {
        switch (idx) {
            case 0: _items[_cur].change(-1); break;
            case 1: _save(_cur); break;
            case 2: _items[_cur].change(+1); break;
            case 3: _reset(); break;
            case 4: _save_all(); break;
            case 5: break;
        }
    }

private:
    struct Item {
        std::string name;
        std::function<std::string()> value;
        std::function<void(int)> change;
        std::function<void()> enter{};   /* 可选：NavEnter 时执行（优先于保存） */
    };

    static const char* _group_name(int i) {
        static const char* g[4] = {"显示", "声音 · 网络", "系统", "商店 · 源"};
        return g[i < 1 ? 0 : (i < 3 ? 1 : (i < 8 ? 2 : 3))];
    }
    static uint16_t _group_color(int i) {
        static const uint16_t c[4] = {rgb565(96, 165, 250), rgb565(52, 211, 153), rgb565(167, 139, 250), rgb565(34, 211, 238)};
        return c[i < 1 ? 0 : (i < 3 ? 1 : (i < 8 ? 2 : 3))];
    }

    void _save_u8(const char* key, uint8_t v) {
        nvs_handle_t h;
        if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
            nvs_set_u8(h, key, v); nvs_commit(h); nvs_close(h);
        }
    }
    uint8_t _get_u8(const char* key, uint8_t def) {
        nvs_handle_t h; uint8_t v = def;
        if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
            nvs_get_u8(h, key, &v); nvs_close(h);
        }
        return v;
    }

    void _load() {
        _items.clear();
        auto* lcd = drivers::Ili9341::instance();
        auto* aud = drivers::I2sAudio::instance();
        uint8_t bl = _get_u8("backlight", 200);
        uint8_t vol = _get_u8("volume", 50);
        uint8_t au = _get_u8("auto_update", 1);
        _auto_update = au != 0;
        _hour12 = _get_u8("hour12", 0) != 0;
        _sleep_min = _get_u8("sleep_min", 3);

        _items.push_back({"背光亮度", [lcd, bl]() { return std::to_string(lcd->get_backlight()) + "/255"; },
                          [lcd](int d) { lcd->set_backlight((uint8_t)std::clamp<int>(lcd->get_backlight() + d * 25, 20, 255)); }});
        _items.push_back({"音量", [aud, vol]() { return std::to_string(aud->get_volume()) + "%"; },
                          [aud](int d) { aud->set_volume((uint8_t)std::clamp<int>(aud->get_volume() + d * 10, 0, 100)); }});
        _items.push_back({"WiFi 自动更新", [this]() { return _auto_update ? "开" : "关"; },
                          [this](int) { _auto_update = !_auto_update; }});
        _items.push_back({"日期格式", [this]() { return _hour12 ? "12 小时" : "24 小时"; },
                          [this](int) { _hour12 = !_hour12; }});
        _items.push_back({"自动休眠", [this]() { return std::to_string(_sleep_min) + " 分钟"; },
                          [this](int d) { _sleep_min = (uint8_t)std::clamp<int>(_sleep_min + d, 1, 30); }});
        _items.push_back({"软件源", [this]() { return _source_domain(); },
                          [](int) { /* 换源方式见提示 */ }});
        _items.push_back({"系统版本", [this]() { return package::firmware_current_version(); },
                          [](int) {}});
        _items.push_back({"检查更新", [this]() { return _upd_msg.empty() ? std::string("未检查") : _upd_msg; },
                          [](int) {},
                          [this]() { _check_update(); }});
        _items.push_back({"立即更新", [this]() { return _upd_ready ? std::string("按 Enter 下载安装") : std::string("先检查更新"); },
                          [](int) {},
                          [this]() { _apply_update(); }});
    }

    /* 检查更新：拉版本清单对比，结果缓存到 _upd_msg/_upd_ready */
    void _check_update() {
        package::FirmwareInfo info;
        if (!package::firmware_check(info)) {
            _upd_msg = "失败: " + info.error;
            _upd_ready = false;
            return;
        }
        if (info.update_available) {
            _upd_msg = "发现新版本 " + info.latest + (info.major ? "（大版本）" : "");
            _upd_ready = true;
            _upd_url = info.url;
        } else {
            _upd_msg = "已是最新 " + info.latest;
            _upd_ready = false;
        }
    }

    /* 立即更新：后台任务下载安装（成功自动重启），UI 保持响应 */
    void _apply_update() {
        if (!_upd_ready || _upd_url.empty()) { _upd_msg = "先检查更新"; return; }
        _upd_msg = "下载更新中…";
        std::string* url = new std::string(_upd_url);
        BaseType_t ok = xTaskCreate([](void* arg) {
            std::string* u = static_cast<std::string*>(arg);
            package::firmware_apply(*u);   /* 成功即重启，不返回 */
            delete u;
            vTaskDelete(nullptr);
        }, "fw_ota", 8192, url, 3, nullptr);
        if (ok != pdPASS) { delete url; _upd_msg = "任务创建失败"; }
    }

    std::string _source_domain() {
        std::string u = package::get_mirror_url();
        size_t p = u.find("://");
        if (p != std::string::npos) u = u.substr(p + 3);
        p = u.find('/');
        if (p != std::string::npos) u = u.substr(0, p);
        return u.empty() ? "默认源" : u;
    }

    void _save(int idx) {
        switch (idx) {
            case 0: _save_u8("backlight", drivers::Ili9341::instance()->get_backlight()); break;
            case 1: _save_u8("volume", drivers::I2sAudio::instance()->get_volume()); break;
            case 2: _save_u8("auto_update", _auto_update ? 1 : 0); break;
            case 3: _save_u8("hour12", _hour12 ? 1 : 0); break;
            case 4: _save_u8("sleep_min", _sleep_min); break;
            case 5: break; /* 软件源：由商店页/TF卡管理 */
        }
    }

    void _save_all() {
        _save(0); _save(1); _save(2); _save(3); _save(4); _save(5);
    }

    void _reset() {
        drivers::Ili9341::instance()->set_backlight(200);
        drivers::I2sAudio::instance()->set_volume(50);
        _auto_update = true; _hour12 = false; _sleep_min = 3;
        _save_all();
    }

    std::vector<Item> _items;
    bool _auto_update = true;
    bool _hour12 = false;
    uint8_t _sleep_min = 3;
    std::string _upd_msg;      /* 更新状态缓存 */
    std::string _upd_url;
    bool _upd_ready = false;
};

static std::shared_ptr<window::Window> setup_create() {
    return std::make_shared<SetupMode>();
}

void setup_mode_register() {
    ModeDesc d{};
    d.id = "setup"; d.name = "SETUP"; d.cn = "设置";
    d.color = rgb565(251, 191, 36);
    d.create = setup_create;
    ModeManager::instance()->register_mode(d);
}

} // namespace modes
} // namespace memoria
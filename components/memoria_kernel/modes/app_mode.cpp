/**
 * @file app_mode.cpp
 * @brief APP 直启运行页实现
 */

#include "app_mode.hpp"
#include "ime.hpp"
#include "keyboard_keymap.hpp"

#include "ili9341.hpp"

extern "C" {
#include "esp_log.h"
#include <sys/stat.h>
}

#include <cstdio>

namespace memoria {
using namespace drivers;
namespace modes {

static const char* TAG = "APPRUN";

AppRunWindow::AppRunWindow(const std::string& bas_path)
    : _path(bas_path) {
    _host = std::make_shared<BasicHost>();
    _host->set_path(bas_path);
    _name = _host->name_stem();

    struct stat st{};
    _file_missing = (stat(bas_path.c_str(), &st) != 0);
    if (_file_missing) {
        ESP_LOGW(TAG, "bas missing: %s", bas_path.c_str());
        return;   /* 不开任务，页面给提示 */
    }

    /* IME 上屏回调：全局单槽，APP 打开期间归本窗口（PRGM 进入时会重设） */
    BasicHost* h = _host.get();
    ime::ime_set_commit_cb([h](const char* utf8, size_t len) {
        h->feed_text(utf8, len);
    });

    /* 任务持有 shared_ptr（堆上 holder），窗口先亡也不会悬垂 */
    auto* holder = new std::shared_ptr<BasicHost>(_host);
    /* P1 双核：BASIC 程序任务钉 core1（解释器+渲染重活，core0 留给系统） */
    if (xTaskCreatePinnedToCore(BasicHost::task_tramp, "basapp", 12288, holder, 3, &_task, 1) != pdPASS) {
        ESP_LOGE(TAG, "task create failed: %s", bas_path.c_str());
        delete holder;
        _task = nullptr;
        _file_missing = true;   /* 借用同一提示位：跑不起来 */
        return;
    }
    _host_running = true;
}

AppRunWindow::~AppRunWindow() {
    _stop_task();
    /* _host shared_ptr：任务 holder 与本窗口各持一份，任务收尾自动释放 */
}

void AppRunWindow::_stop_task() {
    if (!_host_running) return;
    _host->abort();
    /* 最长等 4s（httpget 最长 10s，超时则任务收尾后自灭并释放 holder） */
    int waited = 0;
    while (!_host->finished() && waited < 400) {
        vTaskDelay(pdMS_TO_TICKS(10));
        waited++;
    }
    _host_running = false;
    _task = nullptr;
}

/* ================= 导航 ================= */
bool AppRunWindow::on_nav(const window::NavInput& ni) {
    if (ni.dir == window::NavEvent::NavBack) {
        _stop_task();
        ModeManager::instance()->back_to_menu();
        return true;
    }
    return ModeWindow::on_nav(ni);   /* 其余交 mode_nav（确认弹窗基类逻辑保留） */
}

bool AppRunWindow::mode_nav(const window::NavInput& ni) {
    switch (ni.dir) {
        case window::NavEvent::NavEnter:
            if (!_host->running() && _host->finished()) {
                ModeManager::instance()->back_to_menu();
                return true;
            }
            _host->feed_nav(5);
            return true;
        case window::NavEvent::NavUp:    _host->feed_nav(1); return true;
        case window::NavEvent::NavDown:  _host->feed_nav(2); return true;
        case window::NavEvent::NavLeft:  _host->feed_nav(3); return true;
        case window::NavEvent::NavRight: _host->feed_nav(4); return true;
        default: return true;
    }
}

/* ================= 渲染（全屏接管，无 F 键行） ================= */
void AppRunWindow::on_render() {
    auto* ui = window::UIRenderer::instance();
    ui->fill_rect(_rect, COLOR_BLACK);

    if (_file_missing) {
        ui->draw_text_utf8(4, SCREEN_H / 2 - 8, "程序跑不起来：" + _path, COLOR_RED);
        return;
    }

    _host->render(ui);
    if (!_host->running() && _host->finished()) {
        /* 结束页脚：0 正常 / 2 读不到文件 / 3 返回键终止 */
        const char* why = _host->result() == 0 ? "结束"
                        : (_host->result() == 3 ? "已退出" : "出错");
        std::string s = _name + " · " + why + " · Enter 返回桌面";
        ui->fill_rect({0, (int16_t)(SCREEN_H - 16), (int16_t)SCREEN_W, 14}, COLOR_DARK_GRAY);
        ui->draw_text_utf8(4, SCREEN_H - 15, s, COLOR_LIGHT_GRAY);
    }
}

void AppRunWindow::mode_render(window::UIRenderer*, bool) {}

/* ================= 键盘 ================= */
bool AppRunWindow::on_key(uint16_t key, bool pressed) {
    /* Ctrl / Shift：同时按下 = 中英切换（IME-SPEC 拍板版） */
    if (key == input::K_CTRL) {
        _ctrl_down = pressed;
        if (pressed && _shift_down) ime::ime_toggle();
        return true;
    }
    if (key == input::K_SHIFT) {
        _shift_down = pressed;
        if (pressed && _ctrl_down) ime::ime_toggle();
        return true;
    }
    if (!pressed) return true;

    /* input a$ 会话：中文走 IME，其余进输入缓冲 */
    if (_host->input_active()) {
        if (ime::ime_mode() == ime::Mode::CN) {
            if (ime::ime_handle_key(key, pressed)) return true;
        }
        _host->input_key(key);
        return true;
    }

    /* 常规：进 key() 事件环 */
    _host->feed_key(key, pressed);
    return true;
}

/* ================= ModeWindow 必备（APP 不用 F 键行） ================= */
const char* const* AppRunWindow::fn_labels() {
    static const char* f[6] = {"", "", "", "", "", ""};
    return f;
}

void AppRunWindow::on_fn(int) {}

} // namespace modes
} // namespace memoria

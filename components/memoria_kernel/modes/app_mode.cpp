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
    ui->fill_rect(_rect, COLOR_BG);

    if (_file_missing) {
        const window::Rect card{40, 92, 400, 132};
        ui->card(card, 12, COLOR_CARD, COLOR_STROKE);
        ui->fill_round_rect({(int16_t)(card.x + 1), (int16_t)(card.y + 16), 3, 20},
                            1, _accent);
        ui->draw_text_utf8((int16_t)(card.x + 14), (int16_t)(card.y + 16),
                           "无法启动", COLOR_DANGER);
        ui->draw_text_fit((int16_t)(card.x + 14), (int16_t)(card.y + 42),
                          "程序文件不存在", COLOR_TEXT, card.w - 28);
        ui->draw_text_fit((int16_t)(card.x + 14), (int16_t)(card.y + 64),
                          _path, COLOR_TEXT_DIM, card.w - 28);
        ui->divider_h((int16_t)(card.x + 12), (int16_t)(card.y + 92),
                      (int16_t)(card.w - 24), COLOR_STROKE);
        ui->draw_text_utf8((int16_t)(card.x + 14), (int16_t)(card.y + 102),
                           "Enter 返回桌面", COLOR_TEXT_DIM);
        return;
    }

    /* 画布外框：圆角卡片 + 1px 描边；BASIC 坐标原点仍在屏幕 y=16，逻辑不变 */
    const window::Rect frame{4, 18, (int16_t)(SCREEN_W - 8), (int16_t)(SCREEN_H - 24)};
    ui->fill_round_rect(frame, 8, COLOR_BG);
    ui->draw_round_rect(frame, 8, COLOR_STROKE, 1);

    _host->render(ui);

    /* 顶部一行小状态（右上角浮层，不改 BASIC 坐标语义） */
    const bool running = _host->running();
    const bool finished = _host->finished();
    const char* state = running ? "RUNNING" : (finished ? "EXIT" : "READY");
    const uint16_t sc = running ? COLOR_OK
                      : (finished ? (_host->result() == 0 ? COLOR_OK : COLOR_WARN)
                                  : COLOR_TEXT_DIM);
    std::string nm = _name.empty() ? std::string("BASIC") : _name;
    if (window::UIRenderer::text_width_utf8(nm) > 96)
        nm = window::UIRenderer::clip_text(nm, 96);

    const int tw = window::UIRenderer::text_width_utf8(state);
    const int nw = window::UIRenderer::text_width_utf8(nm);
    const int pw = tw + nw + 28;
    const int16_t px = (int16_t)(SCREEN_W - 8 - pw);
    ui->fill_round_rect({px, 20, (int16_t)pw, 16}, 8, COLOR_CARD);
    ui->draw_round_rect({px, 20, (int16_t)pw, 16}, 8, COLOR_STROKE, 1);
    ui->fill_round_rect({(int16_t)(px + 7), 25, 6, 6}, 3, sc);
    ui->draw_text_fit((int16_t)(px + 18), 22, nm, COLOR_TEXT_DIM, nw + 2);
    ui->draw_text((int16_t)(px + 18 + nw + 6), 22, state, sc);

    if (!running && finished) {
        /* 结束页脚：0 正常 / 2 读不到文件 / 3 返回键终止 */
        const char* why = _host->result() == 0 ? "结束"
                        : (_host->result() == 3 ? "已退出" : "出错");
        const std::string s = _name + " | " + why;
        ui->divider_h(12, (int16_t)(SCREEN_H - 24), (int16_t)(SCREEN_W - 24), COLOR_STROKE);
        ui->draw_text_fit(8, (int16_t)(SCREEN_H - 20), s, COLOR_TEXT_DIM, SCREEN_W - 160);
        const std::string act = "Enter 返回桌面";
        const int aw = window::UIRenderer::text_width_utf8(act);
        ui->draw_text_utf8((int16_t)(SCREEN_W - 8 - aw), (int16_t)(SCREEN_H - 20),
                           act, _accent);
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

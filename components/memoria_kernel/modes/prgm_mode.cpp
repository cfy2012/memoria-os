/**
 * @file prgm_mode.cpp
 * @brief PRGM 编程模式
 *
 * 孩子写程序的地方。三页状态机：
 *   列表页  ：/mem_fat/scripts 下 .ms（旧引擎，隐藏保留）与 .bas（BASIC）混合列出
 *   编辑页  ：BASIC 编辑器（摇杆字符条输入，行号自动步进，保存到 TF 卡）
 *   运行页  ：运行 .bas（屏幕滚动输出 + 摇杆 INPUT 数字输入 + 死循环保护）
 *
 * BASIC 硬件扩展（宿主注入，组件本身零硬件依赖）：
 *   显示 TEXT/RECT/FILL/CLEAR/COLOR/PIXEL/LINE/CIRCLE
 *   连接 WIFI  "ssid","pass"  ·  WIFISTAT()  ·  GPIOIN(pin)
 *   串口 UART "text"（UART0 串口控制台输出）· 蓝牙 BLE "text"（NimBLE 透传，需先连上位机）
 *   系统 SYSTEM "REBOOT"（重启）
 * 解释器语法（memoria_basic）：
 *   PRINT/LET/INPUT/GOTO/GOSUB/RETURN/IF..THEN..ELSE/END/SYSTEM
 *   FOR..NEXT / WHILE..WEND 循环；':' 多语句同行；条件可为数值（非 0 即真）
 *   INCLUDE "file.bas" 合并另一个程序文件的行
 *   GOTO 半禁用（运行时会提示一次注意事项）
 * 注：v1.3 预注入变量经 _inject_env() 弱依赖 net_manager.hpp（__has_include），
 *     豆包-1 落码后重编自动接通真数据（2026-10-04）。
 */

#include "mode_manager.hpp"
#include "basic_interpreter.hpp"
#include "ime.hpp"
#include "keyboard_keymap.hpp"

#include "ili9341.hpp"
#include "joystick.hpp"
#include "wifi_manager.hpp"
#include "ble_manager.hpp"
#include "i2s_audio.hpp"

/* v1.3 预注入：net_manager 弱依赖（豆包-1 网络线落码后自动接通） */
#if __has_include("net_manager.hpp")
#include "net_manager.hpp"
#define HAS_NET_MGR 1
#else
#define HAS_NET_MGR 0
#endif

extern "C" {
#include <dirent.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "esp_http_client.h"
}

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <cctype>

#include <vector>
#include <string>
#include <algorithm>
#include <atomic>

namespace memoria {
using namespace drivers;
namespace script {
int run_file(const std::string& path);
int run_file_out(const std::string& path,
                 const std::function<void(const std::string&)>& out);
}

namespace modes {

/* ---------------- 编辑/运行视图常量 ---------------- */
static const int EDIT_ROWS    = 11;      /* 内容区行数 */
static const int EDIT_LINE_Y  = 34;
static const int CHAR_BAR_Y   = 180;     /* 字符条 */
static const int RUN_ROWS     = 13;
static const int RUN_Y        = 34;
static const int MAX_LINE_LEN = 96;      /* 单行上限（UTF-8 字节，约 32 汉字或 96 ASCII） */

class PrgmMode : public ModeWindow {
public:
    enum View { List = -1, Edit = 0, Run = 1, Src = 2 };

    PrgmMode() {
        s_self = this;
        ime::ime_set_commit_cb([](const char* utf8, size_t len) {
            if (s_self) s_self->_ime_commit(utf8, len);
        });
        _scan();
        _setup_basic();
    }

    /* ================= 键盘输入（输入队列 → 编辑器） ================= */
    bool on_key(uint16_t key, bool pressed) override {
        /* Ctrl / Shift 状态跟踪：同时按下 = 中英切换（IME-SPEC 拍板版，两模式均生效） */
        if (key == input::K_CTRL) {
            _ctrl_down = pressed;
            if (pressed && _shift_down) _ime_toggle_cn();
            return true;
        }
        if (key == input::K_SHIFT) {
            _shift_down = pressed;
            if (pressed && _ctrl_down) _ime_toggle_cn();
            return true;
        }

        /* 运行页字符串 INPUT：中文走输入法面板，英文键盘直录，Enter 确认 */
        if (_view == Run && _str_active) {
            if (!pressed) return true;
            if (ime::ime_mode() == ime::Mode::CN) {
                bool consumed = ime::ime_handle_key(key, pressed);
                if (consumed) return true;
            }
            if (key >= 0x20 && key <= 0x7E) { if (_str_buf.size() < MAX_LINE_LEN) _str_buf += (char)key; return true; }
            if (key == input::K_BACKSPACE) { if (!_str_buf.empty()) _str_buf.pop_back(); return true; }
            if (key == input::K_ENTER) { _str_enter = true; return true; }
            return true;
        }
        if (_view != Edit) return false;
        if (!pressed) return true;

        if (ime::ime_mode() == ime::Mode::CN) {
            /* 中文模式：a~z 拼音 / F1~F4 选字 / F5 F6 翻批 / 退格 / 回车 交给输入法；
             * 未消费（如拼音空时回车=换行、退格=删字）回退到编辑器 */
            bool consumed = ime::ime_handle_key(key, pressed);
            if (consumed) return true;
        }
        /* 英文模式 / CN 未消费：可打印字符直接上屏 */
        if (key >= 0x20 && key <= 0x7E) {
            std::string& line = _edit_lines[_edit_cur].second;
            if ((int)line.size() < MAX_LINE_LEN) line += (char)key;
            return true;
        }
        if (key == input::K_BACKSPACE) { _edit_backspace(); return true; }
        if (key == input::K_ENTER)     { _edit_new_line(); return true; }
        return false;
    }

    void _ime_toggle_cn() {
        ime::ime_toggle();
        _hint = ime::ime_mode() == ime::Mode::CN
                    ? "中文输入 · Ctrl+Shift 切英文" : "英文输入 · Ctrl+Shift 切中文";
    }

    void _ime_commit(const char* utf8, size_t len) {
        if (_view == Run && _str_active) {
            if ((int)_str_buf.size() + (int)len <= MAX_LINE_LEN) _str_buf.append(utf8, len);
            return;
        }
        std::string& line = _edit_lines[_edit_cur].second;
        if ((int)line.size() + (int)len <= MAX_LINE_LEN) line.append(utf8, len);
    }

    /* ================= 渲染 ================= */
    void mode_render(window::UIRenderer* ui, bool focused) override {
        switch (_view) {
            case Edit: _render_editor(ui); break;
            case Run:  _render_run(ui); break;
            case Src:  _render_src(ui); break;
            default:   _render_list(ui); break;
        }
        if (!_hint.empty() && _view != Edit && _view != Run) draw_hint(ui, _hint.c_str());
    }

    /* ================= 导航 ================= */
    bool mode_nav(const window::NavInput& ni) override {
        switch (_view) {
            case Edit: return _nav_edit(ni);
            case Run:
                if (_str_active) return true;   /* 字符串 INPUT 中：吞掉摇杆事件防误退出 */
                if (ni.dir == window::NavEvent::NavEnter || ni.dir == window::NavEvent::NavBack) {
                    _view = List; _hint.clear();
                }
                return true;
            case Src:
                if (ni.dir == window::NavEvent::NavEnter || ni.dir == window::NavEvent::NavBack)
                    _view = List;
                return true;
            default: return _nav_list(ni);
        }
    }

    const char* const* fn_labels() override {
        if (_view == Edit) {
            static const char* f[6] = {"退格", "新行", "删行", "保存", "-", "退出"};
            return f;
        }
        if (_view == Run || _view == Src) {
            static const char* f[6] = {"返回", "-", "-", "-", "-", "菜单"};
            return f;
        }
        static const char* f[6] = {"运行", "编辑", "新建", "删除", "刷新", "菜单"};
        return f;
    }

    void on_fn(int idx) override {
        if (_view == Edit) {
            switch (idx) {
                case 0: _edit_backspace(); break;
                case 1: _edit_new_line(); break;
                case 2: _edit_del_line(); break;
                case 3: _edit_save(); break;
                case 5: _view = List; _hint = "未保存改动已放弃"; break;
            }
            return;
        }
        if (_view == Run || _view == Src) {
            if (idx == 0 || idx == 5) { _view = List; _hint.clear(); }
            return;
        }
        switch (idx) {
            case 0: _run_sel(); break;
            case 1:
                if (_is_bas(_files[_cur])) _enter_edit(_files[_cur]);
                else { _view = Src; _scroll = 0; _load_src(); }
                break;
            case 2: _edit_new_file(); break;
            case 3: if (!_files.empty()) { _pending = _cur; request_confirm("删除这个程序?"); } break;
            case 4: _scan(); break;
            case 5: _hint = "basic: print/let/if/for/while/goto"; break;
        }
    }

    void on_confirm_ok() override {
        if (_pending >= 0 && _pending < (int)_files.size()) {
            remove(("/mem_fat/scripts/" + _files[_pending]).c_str());
        }
        _pending = -1;
        _scan();
    }

private:
    /* ================= 基础引擎注入 ================= */
    void _setup_basic() {
        _interp.set_io([this](const std::string& s) { _run_out(s); },
                       [this](const std::string& p) { return _basic_input(p); });

        /* ---- 显示 ---- */
        _interp.add_cmd("TEXT", [this](const std::vector<basic::BasicArg>& a) {
            if (a.size() < 3) return;
            int x = (int)a[0].num, y = (int)a[1].num;
            _ui()->draw_text(x, y, a[2].str, _fg);
        });
        _interp.add_cmd("RECT", [this](const std::vector<basic::BasicArg>& a) {
            if (a.size() < 4) return;
            window::Rect r{(int16_t)(int)a[0].num, (int16_t)(int)a[1].num,
                           (int16_t)(int)a[2].num, (int16_t)(int)a[3].num};
            _ui()->draw_rect(r, _fg, 1);
        });
        _interp.add_cmd("FILL", [this](const std::vector<basic::BasicArg>& a) {
            if (a.size() < 4) return;
            window::Rect r{(int16_t)(int)a[0].num, (int16_t)(int)a[1].num,
                           (int16_t)(int)a[2].num, (int16_t)(int)a[3].num};
            _ui()->fill_rect(r, _fg);
        });
        _interp.add_cmd("CLEAR", [this](const std::vector<basic::BasicArg>&) {
            _ui()->clear(COLOR_BLACK);
        });
        _interp.add_cmd("COLOR", [this](const std::vector<basic::BasicArg>& a) {
            if (a.size() < 3) return;
            _fg = rgb565((uint8_t)(int)a[0].num, (uint8_t)(int)a[1].num, (uint8_t)(int)a[2].num);
        });
        _interp.add_cmd("PIXEL", [this](const std::vector<basic::BasicArg>& a) {
            if (a.size() < 2) return;
            _ui()->fill_rect({(int16_t)(int)a[0].num, (int16_t)(int)a[1].num, 1, 1}, _fg);
        });
        _interp.add_cmd("LINE", [this](const std::vector<basic::BasicArg>& a) {
            if (a.size() < 4) return;
            /* Bresenham 整数画线：端点 (x0,y0)-(x1,y1)，逐点写像素 */
            int x0 = (int)a[0].num, y0 = (int)a[1].num;
            int x1 = (int)a[2].num, y1 = (int)a[3].num;
            int dx = x1 > x0 ? x1 - x0 : x0 - x1;
            int dy = y1 > y0 ? y1 - y0 : y0 - y1;
            int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
            int err = dx - dy;
            while (true) {
                _ui()->fill_rect({(int16_t)x0, (int16_t)y0, 1, 1}, _fg);
                if (x0 == x1 && y0 == y1) break;
                int e2 = 2 * err;
                if (e2 > -dy) { err -= dy; x0 += sx; }
                if (e2 < dx)  { err += dx; y0 += sy; }
            }
        });
        _interp.add_cmd("CIRCLE", [this](const std::vector<basic::BasicArg>& a) {
            if (a.size() < 3) return;
            /* 中点圆算法：圆心 (cx,cy) 半径 r，8 对称点逐点写像素 */
            int cx = (int)a[0].num, cy = (int)a[1].num;
            int r = (int)a[2].num;
            if (r < 0) return;
            int x = 0, y = r, d = 1 - r;
            auto plot = [&](int px, int py) {
                _ui()->fill_rect({(int16_t)(cx + px), (int16_t)(cy + py), 1, 1}, _fg);
                _ui()->fill_rect({(int16_t)(cx - px), (int16_t)(cy + py), 1, 1}, _fg);
                _ui()->fill_rect({(int16_t)(cx + px), (int16_t)(cy - py), 1, 1}, _fg);
                _ui()->fill_rect({(int16_t)(cx - px), (int16_t)(cy - py), 1, 1}, _fg);
                _ui()->fill_rect({(int16_t)(cx + py), (int16_t)(cy + x), 1, 1}, _fg);
                _ui()->fill_rect({(int16_t)(cx - py), (int16_t)(cy + x), 1, 1}, _fg);
                _ui()->fill_rect({(int16_t)(cx + py), (int16_t)(cy - x), 1, 1}, _fg);
                _ui()->fill_rect({(int16_t)(cx - py), (int16_t)(cy - x), 1, 1}, _fg);
            };
            while (x <= y) {
                plot(x, y);
                x++;
                if (d < 0) d += 2 * x + 1;
                else { y--; d += 2 * (x - y) + 1; }
            }
        });

        /* ---- 连接 / 系统 ---- */
        _interp.add_cmd("WIFI", [](const std::vector<basic::BasicArg>& a) {
            if (a.size() < 2) return;
            WifiManager::instance()->connect(a[0].str, a[1].str);
        });
        _interp.add_func("WIFISTAT", [](const std::vector<basic::BasicArg>&) -> double {
            return WifiManager::instance()->connected() ? 1.0 : 0.0;
        });
        _interp.add_func("GPIOIN", [](const std::vector<basic::BasicArg>& a) -> double {
            if (a.empty() || a[0].is_string) return 0;
            return (double)gpio_get_level((gpio_num_t)(int)a[0].num);
        });
        _interp.add_cmd("UART", [](const std::vector<basic::BasicArg>& a) {
            if (a.empty()) return;
            /* UART0 串口控制台输出（USB 转串口 / 调试口都能看到） */
            std::printf("%s\n", a[0].str.c_str());
        });
        _interp.add_cmd("BLE", [](const std::vector<basic::BasicArg>& a) {
            if (a.empty()) return;
            auto* b = drivers::BleManager::instance();
            if (!b->connected()) { std::printf("BLE: 未连接手机/上位机\n"); return; }
            esp_err_t rc = b->send(a[0].str);
            if (rc != ESP_OK) std::printf("BLE: 发送失败 %d\n", (int)rc);
        });
        _interp.add_cmd("SYSTEM", [](const std::vector<basic::BasicArg>& a) {
            if (a.empty() || a[0].str == "REBOOT") esp_restart();
        });

        /* ---- 字符串 INPUT（键盘录入，英文） ---- */
        _interp.set_input_str([this](const std::string& p) { return _basic_input_str(p); });

        /* ---- 网络 HTTP（明文；HTTPS 后补） ----
         * HTTPGET "url", R$          → GET，响应体（上限 8KB）存入字符串变量 R$
         * HTTPPOST "url", "body", R$ → POST（octet-stream 原样字节），响应体存 R$
         * HTTPUP "url", "path" [,R$] → POST 文件字节（≤2MB，octet-stream），响应体存 R$（可选）
         * HTTPDL "url", "path"       → GET 流式下载直写文件（不占内存），httpstat() 查结果
         * HTTPSTAT()                 → 上一次 HTTP 状态码 */
        _interp.add_raw_cmd("HTTPGET", [this](const std::string& raw) { _http_exec(raw, false); });
        _interp.add_raw_cmd("HTTPPOST", [this](const std::string& raw) { _http_exec(raw, true); });
        _interp.add_raw_cmd("HTTPUP", [this](const std::string& raw) { _http_file(raw, true); });
        _interp.add_raw_cmd("HTTPDL", [this](const std::string& raw) { _http_file(raw, false); });
        _interp.add_func("HTTPSTAT", [](const std::vector<basic::BasicArg>&) -> double {
            return (double)s_http_status.load();
        });

        /* ---- 语音（WAV 全链路，无服务端转码） ----
         * RECORD "path", 秒 → INMP441 录音到 WAV（16kHz 单声道 16bit），阻塞到秒数到（1~60）
         * PLAY "path"       → 播放 WAV/MP3（后台播放不阻塞） */
        _interp.add_cmd("RECORD", [](const std::vector<basic::BasicArg>& a) {
            if (a.empty() || !a[0].is_string) return;
            uint32_t sec = a.size() > 1 ? (uint32_t)a[1].num : 5;
            if (sec < 1) sec = 1;
            if (sec > 60) sec = 60;
            auto* au = I2sAudio::instance();
            if (au->rec_start(a[0].str, 16000) != ESP_OK) return;
            uint32_t waited = 0;
            while (au->is_recording() && waited < sec * 10) {
                vTaskDelay(pdMS_TO_TICKS(100));
                waited++;
            }
            au->rec_stop();
        });
        _interp.add_cmd("PLAY", [](const std::vector<basic::BasicArg>& a) {
            if (a.empty() || !a[0].is_string) return;
            I2sAudio::instance()->play_audio(a[0].str);
        });
    }

    window::UIRenderer* _ui() { return window::UIRenderer::instance(); }

    /* ================= 列表页 ================= */
    void _render_list(window::UIRenderer* ui) {
        char right[16];
        snprintf(right, sizeof(right), "%d 个", (int)_files.size());
        draw_title(ui, "PRGM 编程", right);
        if (_files.empty()) { draw_hint(ui, "空 · F3 新建第一个程序"); return; }
        int y = 34;
        int start = std::max(0, _cur - 10);
        for (int i = start; i < (int)_files.size() && y < SCREEN_H - 48; i++, y += 12) {
            bool bas = _is_bas(_files[i]);
            std::string line = (i == _cur ? "> " : "  ") + _files[i] +
                               (bas ? "  [BASIC]" : "  [ms]");
            ui->draw_text_utf8(2, y, line, i == _cur ? COLOR_YELLOW : (bas ? COLOR_WHITE : COLOR_LIGHT_GRAY));
        }
    }

    bool _nav_list(const window::NavInput& ni) {
        switch (ni.dir) {
            case window::NavEvent::NavUp:    _cur = std::max(0, _cur - 1); break;
            case window::NavEvent::NavDown:  _cur = std::min((int)_files.size() - 1, _cur + 1); break;
            case window::NavEvent::NavEnter:
                if (_files.empty()) _edit_new_file();
                else if (_is_bas(_files[_cur])) _enter_edit(_files[_cur]);
                else _run_sel();
                break;
            default: return false;
        }
        return true;
    }

    void _scan() {
        _files.clear();
        DIR* d = opendir("/mem_fat/scripts");
        if (!d) return;
        struct dirent* e;
        while ((e = readdir(d)) != nullptr) {
            std::string n = e->d_name;
            if (_is_bas(n) || _is_ms(n)) _files.push_back(n);
        }
        closedir(d);
        std::sort(_files.begin(), _files.end());
        _cur = std::min(_cur, (int)_files.size() - 1);
    }
    static bool _is_bas(const std::string& n) { return n.size() > 4 && n.substr(n.size() - 4) == ".bas"; }
    static bool _is_ms(const std::string& n)  { return n.size() > 3 && n.substr(n.size() - 3) == ".ms"; }

    /* ================= v1.3 环境变量预注入 =================
     * 每次运行 .bas 前刷新：系统连什么 WiFi，程序就读到什么。
     * net_manager 未落码时注入空值（__has_include 弱依赖，落码后自动接通）。 */
    void _inject_env() {
#if HAS_NET_MGR
        _interp.set_svar("wifi_ssid$", net::sta_connected() ? net::sta_ssid() : "");
        _interp.set_svar("wifi_pass$", net::sta_pass());
        _interp.set_svar("ap_ssid$",   net::ap_ssid());
        _interp.set_svar("ap_pass$",   net::ap_pass());
        _interp.set_svar("ble_name$",  net::ble_name());
#else
        _interp.set_svar("wifi_ssid$", "");
        _interp.set_svar("ap_ssid$",   "");
        _interp.set_svar("ble_name$",  "");
#endif
        const double cn = ime::ime_cn() ? 1.0 : 0.0;
        _interp.set_nvar("ime_cn", cn);
    }

    /* ================= 运行 ================= */
    void _run_sel() {
        if (_files.empty()) return;
        _run_out_buf.clear();
        _run_lines.clear();
        _fg = COLOR_WHITE;
        std::string path = "/mem_fat/scripts/" + _files[_cur];

        if (_is_bas(_files[_cur])) {
            _interp.clear_program();
            if (!_interp.load_file(path)) { _hint = "读取失败"; return; }
            _inject_env();
            _view = Run;
            _ui()->clear(COLOR_BLACK);
            int rc = _interp.run(100000);
            _run_out("--- 结束: " + std::string(rc == 0 ? "正常" : (rc == 1 ? "超时保护" : "错误")) + " ---");
        } else {
            _view = Run;
            _ui()->clear(COLOR_BLACK);
            int rc = script::run_file_out(path, [this](const std::string& s) { _run_out(s); });
            _run_out("--- 结束: " + std::to_string(rc) + " ---");
        }
    }

    void _run_out(const std::string& s) {
        /* 输出缓冲上限 64KB，超限丢弃最旧段，防止长循环输出导致内存膨胀 */
        const size_t BUF_MAX = 64 * 1024;
        if (_run_out_buf.size() + s.size() > BUF_MAX) {
            size_t drop = _run_out_buf.size() + s.size() - BUF_MAX;
            _run_out_buf.erase(0, drop);
            _run_out_buf += "\n[输出过长 · 已丢弃开头]\n";
        }
        /* 追加 + 按 \n 拆行，只保留最近 RUN_ROWS 行 */
        _run_out_buf += s;
        _run_lines.clear();
        std::string cur;
        for (size_t i = 0; i < _run_out_buf.size(); i++) {
            if (_run_out_buf[i] == '\n') { _run_lines.push_back(cur); cur.clear(); }
            else cur += _run_out_buf[i];
        }
        if (!cur.empty()) _run_lines.push_back(cur);
        size_t keep = _run_lines.size() > (size_t)RUN_ROWS ? _run_lines.size() - RUN_ROWS : 0;
        if (keep) _run_lines.erase(_run_lines.begin(), _run_lines.begin() + (ptrdiff_t)keep);
        if (_run_lines.size() > (size_t)RUN_ROWS) _run_lines.resize(RUN_ROWS);
    }

    void _render_run(window::UIRenderer* ui) {
        draw_title(ui, _files[_cur].c_str(), "Enter 返回");
        int y = RUN_Y;
        for (size_t i = 0; i < _run_lines.size() && y < SCREEN_H - 40; i++, y += 12) {
            ui->draw_text_utf8(2, y, _run_lines[i], COLOR_WHITE);
        }
    }

    /* INPUT 数字输入：摇杆上/下 ±1、左/右 ±10、按下确认 */
    double _basic_input(const std::string& prompt) {
        double v = 0;
        bool done = false;
        while (!done) {
            int32_t x = 2048, y = 2048; bool btn = false;
            Joystick::instance()->read(x, y, btn);
            if (btn) done = true;
            else if (y < 1200)      v += 1;
            else if (y > 2800)      v -= 1;
            else if (x < 1200)      v += 10;
            else if (x > 2800)      v -= 10;

            /* 渲染输入框 */
            _ui()->fill_rect({4, 96, SCREEN_W - 8, 40}, COLOR_DARK_GRAY);
            _ui()->draw_rect({4, 96, SCREEN_W - 8, 40}, COLOR_YELLOW, 1);
            _ui()->draw_text(10, 104, prompt + std::to_string((int)v), COLOR_WHITE);
            _ui()->draw_text_utf8(10, 118, "上/下 ±1  左/右 ±10  按下确定", COLOR_LIGHT_GRAY);
            _ui()->flush();
            vTaskDelay(pdMS_TO_TICKS(80));
        }
        return v;
    }

    /* INPUT 字符串输入：键盘录入（英文），Enter 确认；摇杆事件被 on_key/mode_nav 吞掉 */
    std::string _basic_input_str(const std::string& prompt) {
        _str_buf.clear();
        _str_enter = false;
        _str_active = true;
        _str_buf.clear();
        while (!_str_enter) {
            _ui()->fill_rect({4, 96, SCREEN_W - 8, 40}, COLOR_DARK_GRAY);
            _ui()->draw_rect({4, 96, SCREEN_W - 8, 40}, COLOR_YELLOW, 1);
            _ui()->draw_text_utf8(10, 100, prompt + _str_buf + "_", COLOR_WHITE);
            if (ime::ime_mode() == ime::Mode::CN) {
                /* 中文模式：拼音面板（候选上屏走 _ime_commit → _str_buf） */
                ime::ime_draw_bar(_ui(), 2, 140);
                _ui()->draw_text_utf8(2, 160,
                    ime::ime_active()
                        ? "F1-F4 选字  F5/F6 翻批  退格删拼音  回车上屏  Ctrl+Shift 切英"
                        : "输入拼音，如 ni → 候选  F1-F4 选字  Ctrl+Shift 切英文",
                    COLOR_DARK_GRAY);
            } else {
                _ui()->draw_text_utf8(10, 118, "键盘输入 · Enter 确认 · 退格删除 · Ctrl+Shift 中文", COLOR_LIGHT_GRAY);
            }
            _ui()->flush();
            vTaskDelay(pdMS_TO_TICKS(60));
        }
        _str_active = false;
        return _str_buf;
    }

    /* ================= HTTP 扩展语句 ================= */
    void _http_exec(const std::string& raw, bool post) {
        s_http_status.store(0);
        _http_resp.clear();
        size_t need = post ? 3 : 2;
        auto parts = split_top(raw);
        if (parts.size() < need) { _run_out("HTTP: 参数不足"); return; }

        /* 最后一个参数 = 目标字符串变量名 */
        std::string var = parts.back();
        bool var_ok = var.size() >= 2 && var.back() == '$';
        for (size_t i = 0; var_ok && i + 1 < var.size(); i++)
            if (!(std::isalnum((unsigned char)var[i]) || var[i] == '_')) var_ok = false;
        if (!var_ok) { _run_out("HTTP: 末参数须为字符串变量名"); return; }

        /* 前面的参数 = URL（POST 再加 body），复用解释器参数求值 */
        std::string head_args;
        for (size_t i = 0; i + 1 < parts.size(); i++) {
            if (i) head_args += ",";
            head_args += parts[i];
        }
        auto num_l = [this](const std::string& s) { return _interp.eval_num_expr(s); };
        auto str_l = [this](const std::string& s) { return _interp.eval_str_expr(s); };
        auto vals = basic::parse_args(head_args, num_l, str_l);
        if (vals.empty() || !vals[0].is_string) { _run_out("HTTP: URL 需为字符串"); return; }
        std::string body;
        if (post) {
            if (vals.size() < 2 || !vals[1].is_string) { _run_out("HTTP: body 需为字符串"); return; }
            body = vals[1].str;
        }

        _http_request(vals[0].str, body, post);
        if (!_interp.set_svar(var, _http_resp)) _run_out("HTTP: 变量写入失败");
    }

    /* ================= HTTP 文件语句（掌机语音链路） ================= */
    void _http_file(const std::string& raw, bool up) {
        s_http_status.store(0);
        auto parts = split_top(raw);
        if (parts.size() < 2) { _run_out("HTTP: 参数不足"); return; }
        auto num_l = [this](const std::string& s) { return _interp.eval_num_expr(s); };
        auto str_l = [this](const std::string& s) { return _interp.eval_str_expr(s); };
        auto vals = basic::parse_args(parts[0] + "," + parts[1], num_l, str_l);
        if (vals.size() < 2 || !vals[0].is_string || !vals[1].is_string) {
            _run_out("HTTP: url/path 需为字符串"); return;
        }
        const std::string& url = vals[0].str;
        const std::string& path = vals[1].str;

        if (up) {
            /* 上传：整读文件（≤2MB）作 body，复用 _http_request */
            FILE* f = std::fopen(path.c_str(), "rb");
            if (!f) { _run_out("HTTP: 文件打开失败"); return; }
            std::fseek(f, 0, SEEK_END);
            long sz = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            if (sz < 0 || sz > 2 * 1024 * 1024) { std::fclose(f); _run_out("HTTP: 文件超 2MB"); return; }
            std::string body((size_t)sz, '\0');
            size_t rd = sz ? std::fread(&body[0], 1, (size_t)sz, f) : 0;
            std::fclose(f);
            if ((long)rd != sz) { _run_out("HTTP: 文件读取失败"); return; }
            _http_request(url, body, true);
        } else {
            /* 下载：流式直写 TF，不占内存 */
            FILE* f = std::fopen(path.c_str(), "wb");
            if (!f) { _run_out("HTTP: 文件创建失败"); return; }
            _http_request(url, "", false, f);
            std::fclose(f);
        }

        /* 可选第三参：响应体写入字符串变量（up 后查 ok/err 用） */
        if (parts.size() >= 3) {
            std::string var = parts[2];
            bool var_ok = var.size() >= 2 && var.back() == '$';
            for (size_t i = 0; var_ok && i + 1 < var.size(); i++)
                if (!(std::isalnum((unsigned char)var[i]) || var[i] == '_')) var_ok = false;
            if (var_ok) _interp.set_svar(var, _http_resp);
        }
    }

    void _http_request(const std::string& url, const std::string& body, bool post, FILE* sink = nullptr) {
        esp_http_client_config_t cfg = {};
        cfg.url = url.c_str();
        cfg.timeout_ms = 10000;
        cfg.buffer_size = 4096;
        esp_http_client_handle_t c = esp_http_client_init(&cfg);
        if (!c) { _run_out("HTTP: 初始化失败"); return; }

        esp_http_client_set_method(c, post ? HTTP_METHOD_POST : HTTP_METHOD_GET);
        if (post) esp_http_client_set_header(c, "Content-Type", "application/octet-stream");
        int send_len = post ? (int)body.size() : 0;

        if (esp_http_client_open(c, send_len) == ESP_OK) {
            if (send_len) esp_http_client_write(c, body.data(), (size_t)send_len);
            esp_http_client_fetch_headers(c);
            s_http_status.store(esp_http_client_get_status_code(c));
            char buf[512];
            const size_t CAP = 8 * 1024;
            while (true) {
                int n = esp_http_client_read(c, buf, sizeof(buf));
                if (n <= 0) break;
                if (sink) {
                    std::fwrite(buf, 1, (size_t)n, sink);
                } else {
                    if (_http_resp.size() + (size_t)n > CAP) { _run_out("HTTP: 响应超 8KB 截断"); break; }
                    _http_resp.append(buf, (size_t)n);
                }
            }
        } else {
            _run_out("HTTP: 连接失败");
        }
        esp_http_client_cleanup(c);
    }

    static std::vector<std::string> split_top(const std::string& s) {
        std::vector<std::string> out;
        std::string cur;
        bool in_str = false;
        for (size_t i = 0; i < s.size(); i++) {
            char ch = s[i];
            if (ch == '"') in_str = !in_str;
            if (ch == ',' && !in_str) { out.push_back(cur); cur.clear(); }
            else cur += ch;
        }
        out.push_back(cur);
        return out;
    }

    /* ================= 编辑器 ================= */
    void _enter_edit(const std::string& name) {
        _edit_name = name;
        _edit_lines.clear();
        _edit_cur = 0;
        _load_edit_lines();
        _view = Edit;
        _hint.clear();
    }

    void _edit_new_file() {
        /* 自动编号 progN.bas */
        int n = 1;
        while (true) {
            char buf[24];
            snprintf(buf, sizeof(buf), "prog%d.bas", n);
            struct stat st{};
            if (stat(("/mem_fat/scripts/" + std::string(buf)).c_str(), &st) != 0) {
                _edit_name = buf;
                break;
            }
            n++;
        }
        _edit_lines.clear();
        _edit_lines.push_back({10, ""});
        _edit_cur = 0;
        _view = Edit;
    }

    void _load_edit_lines() {
        FILE* f = std::fopen(("/mem_fat/scripts/" + _edit_name).c_str(), "r");
        if (!f) return;
        char buf[128];
        while (std::fgets(buf, sizeof(buf), f)) {
            std::string line = buf;
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
            if (trim(line).empty()) continue;
            int ln = 0; size_t i = 0;
            while (i < line.size() && line[i] >= '0' && line[i] <= '9') { ln = ln * 10 + (line[i] - '0'); i++; }
            if (i > 0) _edit_lines.push_back({ln, trim(line.substr(i))});
        }
        std::fclose(f);
    }

    static std::string trim(const std::string& s) {
        size_t a = 0, b = s.size();
        while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
        while (b > a && (s[b-1] == ' ' || s[b-1] == '\t' || s[b-1] == '\r')) b--;
        return s.substr(a, b - a);
    }

    void _render_editor(window::UIRenderer* ui) {
        char right[24];
        snprintf(right, sizeof(right), "%s · 第 %d 行", _edit_name.c_str(), _edit_cur + 1);
        draw_title(ui, "编辑 BASIC", right);

        /* 行列表 */
        int y = EDIT_LINE_Y;
        int start = std::max(0, _edit_cur - EDIT_ROWS + 1);
        for (int i = start; i < (int)_edit_lines.size() && y < CHAR_BAR_Y - 8; i++, y += 12) {
            char ln[12];
            snprintf(ln, sizeof(ln), "%3d ", _edit_lines[i].first);
            std::string line = (i == _edit_cur ? "> " : "  ") + std::string(ln) + _edit_lines[i].second;
            ui->draw_text_utf8(2, y, line, i == _edit_cur ? COLOR_YELLOW : COLOR_WHITE);
        }

        /* 中文输入法激活：画候选条，替代字符条（IME-SPEC v1.0 键位） */
        if (ime::ime_active() || ime::ime_mode() == ime::Mode::CN) {
            ime::ime_draw_bar(ui, 2, CHAR_BAR_Y);
            if (ime::ime_active()) {
                ui->draw_text_utf8(2, CHAR_BAR_Y + 18,
                    "F1-F4 选字  F5/F6 翻批  退格删拼音  回车上屏", COLOR_DARK_GRAY);
                ui->draw_text_utf8(2, CHAR_BAR_Y + 30,
                    "Ctrl+Shift 中英切换", COLOR_DARK_GRAY);
            } else {
                ui->draw_text_utf8(2, CHAR_BAR_Y + 18,
                    "输入拼音，如 ni → F1-F4 选字  Ctrl+Shift 切英文", COLOR_DARK_GRAY);
            }
            return;
        }

        /* 字符条（40 格，可左右滚动） */
        static const char kChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 . ,\"()=+-*/<>?!_:;%";
        const int bar_len = (int)strlen(kChars);
        const int bar_off = std::max(0, std::min(_char_off, bar_len - 40));
        for (int i = 0; i < 40; i++) {
            int idx = bar_off + i;
            if (idx >= bar_len) break;
            int x = 2 + i * 8;
            if (idx == _char_sel) {
                ui->fill_rect({(int16_t)x, CHAR_BAR_Y, 8, 12}, COLOR_DARK_GRAY);
                ui->draw_rect({(int16_t)x, CHAR_BAR_Y, 8, 12}, COLOR_YELLOW, 1);
                ui->draw_text(x, CHAR_BAR_Y, std::string(1, kChars[idx]), COLOR_YELLOW);
            } else {
                ui->draw_text(x, CHAR_BAR_Y, std::string(1, kChars[idx]), COLOR_LIGHT_GRAY);
            }
        }
        ui->draw_text_utf8(2, CHAR_BAR_Y + 16, "键盘直接输入 · 摇杆左右选字 Enter 插入 · 上下切行 · Ctrl+Shift 中文", COLOR_DARK_GRAY);
    }

    bool _nav_edit(const window::NavInput& ni) {
        static const char kChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 . ,\"()=+-*/<>?!_:;%";
        const int bar_len = (int)strlen(kChars);
        switch (ni.dir) {
            case window::NavEvent::NavLeft:
                _char_sel = (_char_sel + bar_len - 1) % bar_len;
                _char_off = std::max(0, _char_sel - 39);
                break;
            case window::NavEvent::NavRight:
                _char_sel = (_char_sel + 1) % bar_len;
                _char_off = std::max(0, _char_sel - 39);
                break;
            case window::NavEvent::NavUp:
                if (_edit_cur > 0) _edit_cur--;
                break;
            case window::NavEvent::NavDown:
                if (_edit_cur + 1 < (int)_edit_lines.size()) _edit_cur++;
                break;
            case window::NavEvent::NavEnter: {
                std::string& line = _edit_lines[_edit_cur].second;
                if ((int)line.size() < MAX_LINE_LEN) line += kChars[_char_sel];
                break;
            }
            default: return false;
        }
        return true;
    }

    void _edit_backspace() {
        std::string& line = _edit_lines[_edit_cur].second;
        if (!line.empty()) line.pop_back();
    }
    void _edit_new_line() {
        int cur_ln = _edit_lines[_edit_cur].first;
        int new_ln = cur_ln + 10;
        _edit_lines.insert(_edit_lines.begin() + _edit_cur + 1, {new_ln, ""});
        _edit_cur++;
    }
    void _edit_del_line() {
        if (_edit_lines.empty()) return;
        _edit_lines.erase(_edit_lines.begin() + _edit_cur);
        if (_edit_cur >= (int)_edit_lines.size()) _edit_cur = (int)_edit_lines.size() - 1;
        if (_edit_lines.empty()) _edit_lines.push_back({10, ""});
    }
    void _edit_save() {
        FILE* f = std::fopen(("/mem_fat/scripts/" + _edit_name).c_str(), "w");
        if (!f) { _hint = "保存失败：TF 卡不可写"; return; }
        for (auto& p : _edit_lines) {
            std::fprintf(f, "%d %s\n", p.first, p.second.c_str());
        }
        std::fclose(f);
        _hint = "已保存 " + _edit_name;
        _view = List;
        _scan();
    }

    /* ================= .ms 源码查看 ================= */
    void _load_src() {
        _src.clear();
        FILE* fp = fopen(("/mem_fat/scripts/" + _files[_cur]).c_str(), "r");
        if (!fp) { _src = "（读取失败）"; return; }
        char buf[64];
        while (fgets(buf, sizeof(buf), fp)) _src += buf;
        fclose(fp);
    }
    void _render_src(window::UIRenderer* ui) {
        draw_title(ui, _files[_cur].c_str(), "Enter 返回");
        int y = 34, line = 0, i = 0;
        while (i < (int)_src.size() && y < SCREEN_H - 16 - 12) {
            int take = std::min((int)(SCREEN_W - 4) / window::UIRenderer::FONT_W, (int)_src.size() - i);
            ui->draw_text_utf8(2, y, _src.substr(i, take), line == _scroll ? COLOR_YELLOW : COLOR_WHITE);
            i += take; y += 12; line++;
        }
    }

    /* ================= 状态 ================= */
    int _view = List;
    std::vector<std::string> _files;
    std::string _hint;
    int _scroll = 0;
    int _pending = -1;

    /* 运行 */
    basic::BasicInterpreter _interp;
    std::string _run_out_buf;
    std::vector<std::string> _run_lines;
    uint16_t _fg = COLOR_WHITE;

    /* HTTP / 字符串 INPUT */
    std::string _http_resp;
    std::string _str_buf;
    bool _str_enter = false;
    bool _str_active = false;
    bool _ctrl_down = false;   /* Ctrl/Shift 组合键状态（Ctrl+Shift = 中英切换） */
    bool _shift_down = false;
    static std::atomic<int> s_http_status;

    /* 键盘/IME */
    static PrgmMode* s_self;

    /* 编辑 */
    std::string _edit_name;
    std::vector<std::pair<int, std::string>> _edit_lines;
    int _edit_cur = 0;
    int _char_sel = 0;
    int _char_off = 0;

    /* .ms 源码 */
    std::string _src;
};

PrgmMode* PrgmMode::s_self = nullptr;
std::atomic<int> PrgmMode::s_http_status{0};

static std::shared_ptr<window::Window> prgm_create() {
    return std::make_shared<PrgmMode>();
}

void prgm_mode_register() {
    ModeDesc d{};
    d.id = "prgm"; d.name = "PRGM"; d.cn = "编程";
    d.color = rgb565(34, 211, 238);
    d.create = prgm_create;
    ModeManager::instance()->register_mode(d);
}

} // namespace modes
} // namespace memoria
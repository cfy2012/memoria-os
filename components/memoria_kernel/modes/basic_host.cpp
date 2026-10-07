/**
 * @file basic_host.cpp
 * @brief BASIC APP 宿主层实现：GUI 控件状态机 + 事件环 + 任务运行器
 *
 * 命令注册一览见 basic_host.hpp 文件头。渲染重放模型：
 * BASIC 任务写状态 → 主循环 render() 每帧重放 → framebuffer 全量 flush。
 */

#include "basic_host.hpp"
#include "mode_manager.hpp"
#include "ime.hpp"
#include "keyboard_keymap.hpp"

#include "wifi_manager.hpp"
#include "ble_manager.hpp"
#include "i2s_audio.hpp"

/* v1.3 环境预注入：net_manager 弱依赖（与 PRGM 同源语义） */
#if __has_include("net_manager.hpp")
#include "net_manager.hpp"
#define HOST_HAS_NET 1
#else
#define HOST_HAS_NET 0
#endif

extern "C" {
#include "esp_system.h"
#include "esp_http_client.h"
#include "driver/gpio.h"
}

#include <cstdio>
#include <cstring>
#include <cctype>
#include <algorithm>

namespace memoria {
using namespace drivers;
namespace modes {

std::atomic<int> BasicHost::s_http_status{0};

/* ============================================================
 *  命令注册
 * ============================================================ */
void BasicHost::install(basic::BasicInterpreter& interp) {
    _interp = &interp;

    /* ---- 直接绘制（重放表） ---- */
    interp.add_cmd("TEXT",    [this](auto& a) { _cmd_draw_op(Op::Text, a); });
    interp.add_cmd("RECT",    [this](auto& a) { _cmd_draw_op(Op::Rect, a); });
    interp.add_cmd("FILL",    [this](auto& a) { _cmd_draw_op(Op::Fill, a); });
    interp.add_cmd("PIXEL",   [this](const std::vector<basic::BasicArg>& a) {
        if (a.size() < 2 || a[0].is_string) return;
        std::vector<basic::BasicArg> p = a;
        p.resize(4);
        p[2] = basic::BasicArg{}; p[2].num = 1;   /* w=1 */
        p[3] = basic::BasicArg{}; p[3].num = 1;   /* h=1 */
        _cmd_draw_op(Op::Fill, p);
    });
    interp.add_cmd("LINE",    [this](auto& a) { _cmd_draw_op(Op::Line, a); });
    interp.add_cmd("CIRCLE",  [this](auto& a) { _cmd_draw_op(Op::Circle, a); });
    interp.add_cmd("COLOR",   [this](const std::vector<basic::BasicArg>& a) {
        if (a.size() < 3) return;
        std::lock_guard<std::mutex> lk(_mtx);
        _cur_color = rgb565((uint8_t)(int)a[0].num, (uint8_t)(int)a[1].num, (uint8_t)(int)a[2].num);
    });
    interp.add_cmd("CLEAR",   [this](const std::vector<basic::BasicArg>&) { _state_reset(); });

    /* ---- GUI 控件 ---- */
    interp.add_cmd("WIN",     [this](auto& a) { _cmd_win(a); });
    interp.add_cmd("LIST",    [this](auto& a) { _cmd_list(a); });
    interp.add_cmd("LADD",    [this](auto& a) { _cmd_ladd(a); });
    interp.add_cmd("LCLR",    [this](const std::vector<basic::BasicArg>&) {
        std::lock_guard<std::mutex> lk(_mtx);
        _list_lines.clear();
        _list_scroll = 0;
    });
    interp.add_cmd("LSCR",    [this](const std::vector<basic::BasicArg>& a) {
        if (a.empty()) return;
        std::lock_guard<std::mutex> lk(_mtx);
        if (!_has_list || _list_lines.empty()) return;
        int visible = std::max(1, (_list_r.h - 4) / 16);
        int lo = 0, hi = std::max(0, (int)_list_lines.size() - visible);
        _list_scroll += (int)a[0].num;
        _list_scroll = std::max(lo, std::min(hi, _list_scroll));
    });
    interp.add_cmd("BTN",     [this](auto& a) { _cmd_btn(a); });
    interp.add_cmd("BSEL",    [this](auto& a) { _cmd_bsel(a); });
    interp.add_cmd("INPUTAT", [this](const std::vector<basic::BasicArg>& a) {
        if (a.size() < 4) return;
        std::lock_guard<std::mutex> lk(_mtx);
        _input_r = {(int16_t)(int)a[0].num, (int16_t)(int)a[1].num,
                    (int16_t)(int)a[2].num, (int16_t)(int)a[3].num};
        _has_input_r = true;
    });
    interp.add_cmd("DELAY",   [this](const std::vector<basic::BasicArg>& a) {
        int ms = a.empty() ? 0 : (int)a[0].num;
        if (ms < 1) ms = 1;
        if (ms > 60000) ms = 60000;
        vTaskDelay(pdMS_TO_TICKS(ms));
    });
    /* WAIT：PRGM 模式标准延时（wait(毫秒)，教材 0x41.6）。
       与 DELAY 同实现：vTaskDelay 按毫秒停。wait(150) / wait 150 均兼容 */
    interp.add_cmd("WAIT",    [this](const std::vector<basic::BasicArg>& a) {
        int ms = a.empty() ? 0 : (int)a[0].num;
        if (ms < 1) ms = 1;
        if (ms > 60000) ms = 60000;
        vTaskDelay(pdMS_TO_TICKS(ms));
    });
    /* v1.3 GUI 缺口①⑤③②④：bkey / text 样式 / img / lbl 控件 / after */
    interp.add_func("BKEY", [this](const std::vector<basic::BasicArg>& a) -> double {
        return _func_bkey(a);
    });
    interp.add_cmd("LBL",    [this](auto& a) { _cmd_lbl(a); });
    interp.add_cmd("LLBL",   [this](auto& a) { _cmd_llbl(a); });
    interp.add_cmd("LBLD",   [this](auto& a) { _cmd_lbld(a); });
    interp.add_cmd("IMG",    [this](auto& a) { _cmd_img(a); });
    interp.add_cmd("AFTER",  [this](auto& a) { _cmd_after(a); });

    /* ---- key()：非阻塞键事件 ---- */
    interp.add_func("KEY", [this](const std::vector<basic::BasicArg>&) -> double {
        return (double)_ring_pop();
    });

    /* ---- 连接 / 系统（网络系统托管：wifi 语句保留但忽略） ---- */
    interp.add_cmd("WIFI", [this](const std::vector<basic::BasicArg>&) {
        std::lock_guard<std::mutex> lk(_mtx);
        if (_has_list) _list_add("系统托管网络：wifi 语句已忽略（读 wifi_ssid$）");
    });
    interp.add_func("WIFISTAT", [](const std::vector<basic::BasicArg>&) -> double {
        /* 按需射频：联网程序第一步查询即触发 RF 启动 + NVS 自动重连（幂等） */
        WifiManager::instance()->start_rf();
        return WifiManager::instance()->connected() ? 1.0 : 0.0;
    });
    interp.add_func("GPIOIN", [](const std::vector<basic::BasicArg>& a) -> double {
        if (a.empty() || a[0].is_string) return 0;
        return (double)gpio_get_level((gpio_num_t)(int)a[0].num);
    });
    interp.add_cmd("UART", [](const std::vector<basic::BasicArg>& a) {
        if (a.empty()) return;
        std::printf("%s\n", a[0].str.c_str());
    });
    interp.add_cmd("BLE", [](const std::vector<basic::BasicArg>& a) {
        if (a.empty()) return;
        auto* b = drivers::BleManager::instance();
        if (!b->connected()) { std::printf("BLE: 未连接手机/上位机\n"); return; }
        esp_err_t rc = b->send(a[0].str);
        if (rc != ESP_OK) std::printf("BLE: 发送失败 %d\n", (int)rc);
    });
    interp.add_cmd("SYSTEM", [](const std::vector<basic::BasicArg>& a) {
        if (a.empty() || a[0].str == "REBOOT") esp_restart();
    });

    /* ---- 网络 HTTP（与 PRGM 同语义：响应上限 1MB PSRAM） ---- */
    interp.add_raw_cmd("HTTPGET", [this](const std::string& raw) { _http_exec(raw, false); });
    interp.add_raw_cmd("HTTPPOST", [this](const std::string& raw) { _http_exec(raw, true); });
    interp.add_raw_cmd("HTTPUP", [this](const std::string& raw) { _http_file(raw, true); });
    interp.add_raw_cmd("HTTPDL", [this](const std::string& raw) { _http_file(raw, false); });
    interp.add_func("HTTPSTAT", [](const std::vector<basic::BasicArg>&) -> double {
        return (double)s_http_status.load();
    });

    /* ---- 语音（WAV 全链路） ---- */
    interp.add_cmd("RECORD", [this](const std::vector<basic::BasicArg>& a) {
        if (a.empty() || !a[0].is_string) return;
        uint32_t sec = a.size() > 1 ? (uint32_t)a[1].num : 5;
        if (sec < 1) sec = 1;
        if (sec > 60) sec = 60;
        auto* au = I2sAudio::instance();
        if (au->rec_start(a[0].str, 16000) != ESP_OK) return;
        uint32_t waited = 0;
        while (au->is_recording() && waited < sec * 10 && !_abort_flag.load()) {
            vTaskDelay(pdMS_TO_TICKS(100));
            waited++;
        }
        au->rec_stop();
    });
    interp.add_cmd("PLAY", [](const std::vector<basic::BasicArg>& a) {
        if (a.empty() || !a[0].is_string) return;
        I2sAudio::instance()->play_audio(a[0].str);
    });

    /* ---- input a$：位置由 inputat 指定，IME 候选条画框下方 ---- */
    interp.set_input_str([this](const std::string& p) { return _input_str(p); });
}

/* ============================================================
 *  GPIOIN 用 driver/gpio（头部已引）
 * ============================================================ */

/* ============================================================
 *  事件环
 * ============================================================ */
void BasicHost::_ring_push(uint8_t code) {
    /* volatile 量禁 ++（GCC 弃用告警，-Werror 挂编译），经局部变量回写 */
    portENTER_CRITICAL(&_ring_mux);
    int t = _rt;
    if (t - _rh < RING_N) { _ring[t % RING_N] = code; _rt = t + 1; }
    portEXIT_CRITICAL(&_ring_mux);
}

uint8_t BasicHost::_ring_pop() {
    portENTER_CRITICAL(&_ring_mux);
    int h = _rh;
    uint8_t c = 0;
    if (h != _rt) { c = _ring[h % RING_N]; _rh = h + 1; }
    portEXIT_CRITICAL(&_ring_mux);
    return c;
}

void BasicHost::feed_key(uint16_t key, bool pressed) {
    if (!pressed) return;
    if (key == input::K_ENTER)     { _ring_push(13); _mark_btn_press(); return; }
    if (key == input::K_BACKSPACE) { _ring_push(8);  return; }
    if (key == input::K_ESC)       { _ring_push(27); return; }
    if (key >= 0x20 && key <= 0x7E) _ring_push((uint8_t)key);
}

void BasicHost::feed_nav(int dir) {
    if (dir >= 1 && dir <= 5) {
        _ring_push((uint8_t)dir);
        if (dir == 5) _mark_btn_press();   /* 摇杆按下 = 确认聚焦按钮 */
    }
}

void BasicHost::_mark_btn_press() {
    std::lock_guard<std::mutex> lk(_mtx);
    if (_btn_focus >= 0 && _btn_focus < 8 && _btn_focus < (int)_btns.size())
        _btn_press[_btn_focus] = 1;
}

void BasicHost::feed_text(const char* utf8, size_t len) {
    if (!utf8 || !len) return;
    std::lock_guard<std::mutex> lk(_mtx);
    if (!_in_input.load()) return;
    if (_input_buf.size() + len <= 96) _input_buf.append(utf8, len);
}

void BasicHost::input_key(uint16_t key) {
    std::lock_guard<std::mutex> lk(_mtx);
    if (!_in_input.load()) return;
    if (key == input::K_ENTER || key == input::K_ESC) { _input_enter = true; return; }
    if (key == input::K_BACKSPACE) {
        /* UTF-8 安全删一字符：先删续字节再删首字节 */
        while (!_input_buf.empty() && (_input_buf.back() & 0xC0) == 0x80) _input_buf.pop_back();
        if (!_input_buf.empty()) _input_buf.pop_back();
        return;
    }
    if (key >= 0x20 && key <= 0x7E && _input_buf.size() < 96) _input_buf += (char)key;
}

/* ============================================================
 *  input a$ 会话（BASIC 任务阻塞等，主循环喂字）
 * ============================================================ */
std::string BasicHost::_input_str(const std::string& prompt) {
    {
        std::lock_guard<std::mutex> lk(_mtx);
        _input_prompt = prompt;
        _input_buf.clear();
        _input_enter = false;
    }
    _in_input = true;
    while (true) {
        if (_abort_flag.load()) break;
        bool enter = false;
        { std::lock_guard<std::mutex> lk(_mtx); enter = _input_enter; }
        if (enter) break;
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    std::string out;
    {
        std::lock_guard<std::mutex> lk(_mtx);
        out = _input_buf;
        _in_input = false;
        _input_enter = false;
    }
    return out;
}

/* ============================================================
 *  GUI 命令
 * ============================================================ */
void BasicHost::_cmd_win(const std::vector<basic::BasicArg>& a) {
    if (a.size() < 4) return;
    std::lock_guard<std::mutex> lk(_mtx);
    if (_panels.size() >= 8) return;
    Panel p;
    p.r = {(int16_t)(int)a[0].num, (int16_t)(int)a[1].num,
           (int16_t)(int)a[2].num, (int16_t)(int)a[3].num};
    p.title = a.size() >= 5 ? a[4].str : "";
    _panels.push_back(std::move(p));
}

void BasicHost::_cmd_list(const std::vector<basic::BasicArg>& a) {
    if (a.size() < 4) return;
    std::lock_guard<std::mutex> lk(_mtx);
    _list_r = {(int16_t)(int)a[0].num, (int16_t)(int)a[1].num,
               (int16_t)(int)a[2].num, (int16_t)(int)a[3].num};
    _has_list = true;
    _list_lines.clear();
    _list_scroll = 0;
}

void BasicHost::_cmd_ladd(const std::vector<basic::BasicArg>& a) {
    if (a.empty()) return;
    std::lock_guard<std::mutex> lk(_mtx);
    if (!_has_list) return;
    _list_add(a[0].str);
}

void BasicHost::_cmd_btn(const std::vector<basic::BasicArg>& a) {
    if (a.size() < 4) return;
    std::lock_guard<std::mutex> lk(_mtx);
    if (_btns.size() >= 8) return;
    Button b;
    b.r = {(int16_t)(int)a[0].num, (int16_t)(int)a[1].num,
           (int16_t)(int)a[2].num, (int16_t)(int)a[3].num};
    b.label = a.size() >= 5 ? a[4].str : "";
    _btns.push_back(std::move(b));
}

void BasicHost::_cmd_bsel(const std::vector<basic::BasicArg>& a) {
    if (a.empty()) return;
    std::lock_guard<std::mutex> lk(_mtx);
    int i = (int)a[0].num;
    if (i >= -1 && i < (int)_btns.size()) _btn_focus = i;
}

/* ---- v1.3 GUI 缺口：bkey / lbl / img / after ---- */
double BasicHost::_func_bkey(const std::vector<basic::BasicArg>& a) {
    if (a.empty() || a[0].is_string) return 0;
    int i = (int)a[0].num;
    std::lock_guard<std::mutex> lk(_mtx);
    if (i < 0 || i >= 8) return 0;
    uint8_t v = _btn_press[i];   /* 读后清零：轮询式事件，与 key() 同语义 */
    _btn_press[i] = 0;
    return v ? 1.0 : 0.0;
}

void BasicHost::_cmd_lbl(const std::vector<basic::BasicArg>& a) {
    if (a.size() < 4 || a[0].is_string) return;
    std::lock_guard<std::mutex> lk(_mtx);
    Label l;
    l.id  = (int16_t)(int)a[0].num;
    l.x   = (int16_t)(int)a[1].num;
    l.y   = (int16_t)(int)a[2].num;
    l.text = a[3].str;
    l.color = _cur_color;
    if (a.size() >= 5 && !a[4].is_string)
        l.style = (uint8_t)std::max(0, std::min(2, (int)a[4].num));
    for (auto& e : _labels) { if (e.id == l.id) { e = l; return; } }   /* 同 id 覆盖 */
    if (_labels.size() < 16) _labels.push_back(l);
}

void BasicHost::_cmd_llbl(const std::vector<basic::BasicArg>& a) {
    if (a.size() < 2 || a[0].is_string) return;
    std::lock_guard<std::mutex> lk(_mtx);
    int id = (int)a[0].num;
    for (auto& e : _labels) {
        if (e.id == id) {
            e.text = a[1].str;
            if (a.size() >= 3 && !a[2].is_string)
                e.style = (uint8_t)std::max(0, std::min(2, (int)a[2].num));
            return;
        }
    }
}

void BasicHost::_cmd_lbld(const std::vector<basic::BasicArg>& a) {
    if (a.empty() || a[0].is_string) return;
    std::lock_guard<std::mutex> lk(_mtx);
    int id = (int)a[0].num;
    for (size_t i = 0; i < _labels.size(); i++) {
        if (_labels[i].id == id) { _labels.erase(_labels.begin() + (long)i); return; }
    }
}

void BasicHost::_cmd_img(const std::vector<basic::BasicArg>& a) {
    if (a.size() < 3) return;
    std::lock_guard<std::mutex> lk(_mtx);
    DrawOp d;
    d.op = Op::Icon;
    d.color = _cur_color;
    d.w  = (int16_t)(int)a[0].num;            /* 图标 id 0..7 */
    d.x  = (int16_t)(int)a[1].num;
    d.y  = (int16_t)(int)a[2].num;
    if (d.w < 0 || d.w >= 8) return;
    if (_ops.size() >= 64) _ops.erase(_ops.begin());
    _ops.push_back(std::move(d));
}

void BasicHost::_cmd_after(const std::vector<basic::BasicArg>& a) {
    if (a.size() < 2) return;
    uint32_t ms = (uint32_t)std::max(1.0, std::min(3600000.0, a[0].num));  /* 1ms~1h */
    int line = (int)a[1].num;
    if (line < 0) return;
    std::lock_guard<std::mutex> lk(_mtx);
    if (_after.size() >= 8) _after.erase(_after.begin());   /* 满则丢最旧 */
    _after.push_back({(uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) + ms, line});
}

int BasicHost::_after_timer_check() {
    uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    std::lock_guard<std::mutex> lk(_mtx);
    for (size_t i = 0; i < _after.size(); i++) {
        if ((int32_t)(now - _after[i].due_ms) >= 0) {   /* 无符号回绕安全比较 */
            int line = _after[i].line;
            _after.erase(_after.begin() + (long)i);
            return line;
        }
    }
    return 0;
}

void BasicHost::_cmd_draw_op(Op op, const std::vector<basic::BasicArg>& a) {
    std::lock_guard<std::mutex> lk(_mtx);
    DrawOp d;
    d.op = op;
    d.color = _cur_color;
    switch (op) {
        case Op::Icon: return;   /* IMG 走 _cmd_img，不属直绘语句 */
        case Op::Text:
            if (a.size() < 3) return;
            d.x = (int16_t)(int)a[0].num; d.y = (int16_t)(int)a[1].num;
            d.text = a[2].str;
            /* v1.3：可选第 4 参文本样式（0 正常 / 1 反白 / 2 粗体） */
            if (a.size() >= 4 && !a[3].is_string)
                d.style = (uint8_t)std::max(0, std::min(2, (int)a[3].num));
            break;
        case Op::Fill: case Op::Rect: case Op::Line:
            if (a.size() < (op == Op::Line ? 4 : 4)) return;
            d.x = (int16_t)(int)a[0].num; d.y = (int16_t)(int)a[1].num;
            if (op == Op::Line) { d.x1 = (int16_t)(int)a[2].num; d.y1 = (int16_t)(int)a[3].num; }
            else { d.w = (int16_t)(int)a[2].num; d.h = (int16_t)(int)a[3].num; }
            break;
        case Op::Circle:
            if (a.size() < 3) return;
            d.x = (int16_t)(int)a[0].num; d.y = (int16_t)(int)a[1].num;
            d.w = (int16_t)(int)a[2].num;             /* 半径 */
            if (d.w > 80) d.w = 80;
            break;
    }
    if (op == Op::Fill && a.size() >= 4 && a[0].is_string) return; /* 防御：FILL 首参误传串 */
    if (_ops.size() >= 64) _ops.erase(_ops.begin());
    _ops.push_back(std::move(d));
    (void)op;
}

void BasicHost::_state_reset() {
    std::lock_guard<std::mutex> lk(_mtx);
    _panels.clear();
    _btns.clear();
    _btn_focus = -1;
    std::memset(_btn_press, 0, sizeof(_btn_press));
    _has_list = false;
    _list_lines.clear();
    _list_scroll = 0;
    _has_input_r = false;
    _ops.clear();
    _labels.clear();
    _after.clear();
    _cur_color = 0xFFFF;
}

/* ============================================================
 *  列表追加（按宽换行 + 跟随滚动；调用方持锁）
 * ============================================================ */
static std::string clip_utf8(const std::string& s, int maxw) {
    int w = 0; size_t i = 0;
    while (i < s.size()) {
        size_t n = 1;
        if ((unsigned char)s[i] >= 0xF0) n = 4;
        else if ((unsigned char)s[i] & 0x80) n = ((unsigned char)s[i] & 0xE0) == 0xC0 ? 2 : 3;
        w += (n > 1) ? window::UIRenderer::CJK_W : window::UIRenderer::FONT_W;
        if (w > maxw) break;
        i += n;
    }
    return s.substr(0, i);
}

void BasicHost::_list_add(const std::string& text) {
    /* 调用方持锁；_has_list 校验在 _cmd_ladd */
    const int maxw = std::max(40, _list_r.w - 6);
    int visible = std::max(1, (_list_r.h - 4) / 16);
    int lines_before = (int)_list_lines.size();
    bool was_bottom = (_list_scroll + visible >= lines_before);

    std::string rest = text;
    if (rest.empty()) _list_lines.push_back("");
    while (!rest.empty() && _list_lines.size() < 400) {
        std::string piece = clip_utf8(rest, maxw);
        if (piece.empty()) {   /* 首字符超宽：整字硬入 */
            size_t n = ((unsigned char)rest[0] & 0x80) ? 3 : 1;
            if (n > rest.size()) n = rest.size();
            piece = rest.substr(0, n);
        }
        _list_lines.push_back(piece);
        rest = rest.substr(piece.size());
    }
    /* 容量 300：丢最旧，滚动游标同步回退 */
    while (_list_lines.size() > 300) {
        _list_lines.erase(_list_lines.begin());
        if (_list_scroll > 0) _list_scroll--;
        lines_before--;
    }
    if (was_bottom) {
        _list_scroll = std::max(0, (int)_list_lines.size() - visible);
    }
}

/* ============================================================
 *  渲染重放（主循环任务）
 * ============================================================ */
/* 内置图标表：img id,x,y（16×16 点阵，'#' 亮，可替换/扩展）
 *   0 电池  1 WiFi  2 音符  3 文件夹  4 齿轮  5 星  6 心  7 右箭头 */
static const char* const ICON_BITMAPS[8][16] = {
    {   /* 0 电池 */
        "................",
        "..############..",
        ".##############.",
        "###############.",
        "#..............#",
        "#..............#",
        "#..............#",
        "#..............#",
        "#..............#",
        "#..............#",
        "#..............#",
        "#..............#",
        "###############.",
        ".##############.",
        "................",
        "................",
    },
    {   /* 1 WiFi */
        "................",
        "................",
        "....#......#....",
        "....##....##....",
        ".....######.....",
        ".....######.....",
        "......####......",
        "......####......",
        ".......##.......",
        ".......##.......",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
    },
    {   /* 2 音符 */
        "................",
        ".....######.....",
        "....#####.......",
        "...###..........",
        "................",
        ".......##.......",
        ".......##.......",
        ".......##.......",
        ".......##.......",
        ".......##.......",
        ".......##.......",
        ".......##.......",
        "......#####.....",
        "......#####.....",
        "................",
        "................",
    },
    {   /* 3 文件夹 */
        "................",
        "..####..........",
        "..#####.........",
        ".######.........",
        "############....",
        "############....",
        "############....",
        "############....",
        "############....",
        "############....",
        "############....",
        "############....",
        "############....",
        "############....",
        "................",
        "................",
    },
    {   /* 4 齿轮 */
        "....########....",
        "..############..",
        ".###..####..###.",
        ".####......####.",
        "####........####",
        "###..........###",
        "###..........###",
        "###....##....###",
        "###..........###",
        "###..........###",
        "####........####",
        ".####......####.",
        ".###..####..###.",
        "..############..",
        "....########....",
        "................",
    },
    {   /* 5 星 */
        ".......##.......",
        "......####......",
        ".....######.....",
        "....########....",
        "...##########...",
        "..############..",
        ".##############.",
        "###############.",
        ".##############.",
        "..############..",
        "...##########...",
        "....########....",
        ".....######.....",
        "......####......",
        ".......##.......",
        "................",
    },
    {   /* 6 心 */
        "................",
        ".###......###...",
        "#####....#####..",
        "######..######..",
        "###############.",
        "##############..",
        ".############...",
        "..##########....",
        "...########.....",
        "....######......",
        ".....####.......",
        "......##........",
        "................",
        "................",
        "................",
        "................",
    },
    {   /* 7 右箭头 */
        "................",
        ".......#........",
        "......##........",
        ".....###........",
        "....####........",
        "...#####........",
        "..######........",
        ".#######........",
        "..######........",
        "...#####........",
        "....####........",
        ".....###........",
        "......##........",
        ".......#........",
        "................",
        "................",
    },
};

void BasicHost::render(window::UIRenderer* ui) {
    std::lock_guard<std::mutex> lk(_mtx);
    const int16_t OY = 16;   /* app 区 y=0 在状态栏下 */

    for (auto& p : _panels) {
        if (p.r.w < 2 || p.r.h < 2) continue;
        window::Rect r{p.r.x, (int16_t)(p.r.y + OY), p.r.w, p.r.h};
        ui->fill_rect(r, rgb565(18, 22, 30));
        ui->draw_rect(r, COLOR_LIGHT_GRAY, 1);
        if (p.r.h >= 16 && !p.title.empty()) {
            ui->fill_rect({r.x, r.y, p.r.w, 14}, COLOR_DEEP_BLUE);
            ui->draw_text_utf8((int16_t)(r.x + 3), (int16_t)(r.y + 1),
                               clip_utf8(p.title, p.r.w - 8), COLOR_WHITE);
        }
    }

    if (_has_list) {
        window::Rect r{_list_r.x, (int16_t)(_list_r.y + OY), _list_r.w, _list_r.h};
        ui->fill_rect(r, COLOR_BLACK);
        ui->draw_rect(r, COLOR_LIGHT_GRAY, 1);
        int visible = std::max(0, (r.h - 4) / 16);
        int y = r.y + 2;
        for (int i = 0; i < visible && (_list_scroll + i) < (int)_list_lines.size(); i++, y += 16) {
            ui->draw_text_utf8((int16_t)(r.x + 3), (int16_t)y,
                               _list_lines[_list_scroll + i], COLOR_WHITE);
        }
    }

    for (int i = 0; i < (int)_btns.size(); i++) {
        auto& b = _btns[i];
        window::Rect r{b.r.x, (int16_t)(b.r.y + OY), b.r.w, b.r.h};
        bool foc = (i == _btn_focus);
        ui->fill_rect(r, foc ? rgb565(70, 90, 160) : rgb565(36, 44, 74));
        ui->draw_rect(r, foc ? COLOR_YELLOW : COLOR_LIGHT_GRAY, foc ? 2 : 1);
        if (!b.label.empty()) {
            int tw = window::UIRenderer::text_width_utf8(b.label);
            ui->draw_text_utf8((int16_t)(r.x + std::max(2, (r.w - tw) / 2)),
                               (int16_t)(r.y + std::max(1, (r.h - 12) / 2)), b.label, COLOR_WHITE);
        }
    }

    /* lbl 文本控件（控件层：面板之后、直绘之前；反白/粗体样式） */
    for (auto& l : _labels) {
        if (l.text.empty()) continue;
        int tw = window::UIRenderer::text_width_utf8(l.text);
        int x = l.x, y = (int16_t)(l.y + OY);
        if (l.style == 1) {   /* 反白：当前色底 + 黑字 */
            ui->fill_rect({(int16_t)x, (int16_t)y, (int16_t)tw, 16}, (uint16_t)l.color);
            ui->draw_text_utf8(x, y, l.text, COLOR_BLACK);
        } else {
            ui->draw_text_utf8(x, y, l.text, (uint16_t)l.color);
            if (l.style == 2) ui->draw_text_utf8((int16_t)(x + 1), y, l.text, (uint16_t)l.color);
        }
    }

    for (auto& d : _ops) {
        switch (d.op) {
            case Op::Text: {
                if (d.style == 1) {   /* 反白：当前色底 + 黑字 */
                    int tw = window::UIRenderer::text_width_utf8(d.text);
                    ui->fill_rect({d.x, (int16_t)(d.y + OY), (int16_t)tw, 16}, (uint16_t)d.color);
                    ui->draw_text_utf8(d.x, (int16_t)(d.y + OY), d.text, COLOR_BLACK);
                } else {
                    ui->draw_text_utf8(d.x, (int16_t)(d.y + OY), d.text, (uint16_t)d.color);
                    if (d.style == 2)   /* 粗体：x+1 重绘 */
                        ui->draw_text_utf8((int16_t)(d.x + 1), (int16_t)(d.y + OY), d.text, (uint16_t)d.color);
                }
                break;
            }
            case Op::Icon: {   /* 内置图标点阵（16×16，前景=当前色） */
                int id = d.w;
                if (id >= 0 && id < 8) {
                    for (int yy = 0; yy < 16; yy++) {
                        const char* row = ICON_BITMAPS[id][yy];
                        for (int xx = 0; xx < 16; xx++) {
                            if (row[xx] == '#')
                                ui->fill_rect({(int16_t)(d.x + xx), (int16_t)(d.y + OY + yy), 1, 1},
                                              (uint16_t)d.color);
                        }
                    }
                }
                break;
            }
            case Op::Fill:
                ui->fill_rect({d.x, (int16_t)(d.y + OY), d.w, d.h}, (uint16_t)d.color);
                break;
            case Op::Rect:
                ui->draw_rect({d.x, (int16_t)(d.y + OY), d.w, d.h}, (uint16_t)d.color, 1);
                break;
            case Op::Line: {
                int x0 = d.x, y0 = d.y + OY, x1 = d.x1, y1 = d.y1 + OY;
                int dx = x1 > x0 ? x1 - x0 : x0 - x1;
                int dy = y1 > y0 ? y1 - y0 : y0 - y1;
                int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
                int err = dx - dy;
                while (true) {
                    ui->fill_rect({(int16_t)x0, (int16_t)y0, 1, 1}, (uint16_t)d.color);
                    if (x0 == x1 && y0 == y1) break;
                    int e2 = 2 * err;
                    if (e2 > -dy) { err -= dy; x0 += sx; }
                    if (e2 < dx)  { err += dx; y0 += sy; }
                }
                break;
            }
            case Op::Circle: {
                int cx = d.x, cy = d.y + OY, rad = d.w;
                int x = 0, y = rad, dq = 1 - rad;
                auto plot = [&](int px, int py) {
                    ui->fill_rect({(int16_t)(cx + px), (int16_t)(cy + py), 1, 1}, (uint16_t)d.color);
                    ui->fill_rect({(int16_t)(cx - px), (int16_t)(cy + py), 1, 1}, (uint16_t)d.color);
                    ui->fill_rect({(int16_t)(cx + px), (int16_t)(cy - py), 1, 1}, (uint16_t)d.color);
                    ui->fill_rect({(int16_t)(cx - px), (int16_t)(cy - py), 1, 1}, (uint16_t)d.color);
                    ui->fill_rect({(int16_t)(cx + py), (int16_t)(cy + px), 1, 1}, (uint16_t)d.color);
                    ui->fill_rect({(int16_t)(cx - py), (int16_t)(cy + px), 1, 1}, (uint16_t)d.color);
                    ui->fill_rect({(int16_t)(cx + py), (int16_t)(cy - px), 1, 1}, (uint16_t)d.color);
                    ui->fill_rect({(int16_t)(cx - py), (int16_t)(cy - px), 1, 1}, (uint16_t)d.color);
                };
                while (x <= y) {
                    plot(x, y);
                    x++;
                    if (dq < 0) dq += 2 * x + 1;
                    else { y--; dq += 2 * (x - y) + 1; }
                }
                break;
            }
        }
    }

    if (_in_input.load()) {
        window::Rect r = _has_input_r
            ? window::Rect{_input_r.x, (int16_t)(_input_r.y + OY), _input_r.w, _input_r.h}
            : window::Rect{4, (int16_t)(96 + OY), (int16_t)(SCREEN_W - 8), 40};
        ui->fill_rect(r, COLOR_DARK_GRAY);
        ui->draw_rect(r, COLOR_YELLOW, 1);
        ui->draw_text_utf8((int16_t)(r.x + 6), (int16_t)(r.y + 4),
                           clip_utf8(_input_prompt + _input_buf + "_", r.w - 12), COLOR_WHITE);
        if (ime::ime_mode() == ime::Mode::CN)
            ime::ime_draw_bar(ui, 2, (int)(r.y + r.h + 4));
    }
}

/* ============================================================
 *  任务运行器
 *  tramp 参数 = 堆上 shared_ptr holder：窗口先亡也不悬垂，
 *  任务收尾 delete holder 后自删任务。
 * ============================================================ */
void BasicHost::task_tramp(void* arg) {
    auto* holder = static_cast<std::shared_ptr<BasicHost>*>(arg);
    holder->get()->task_body();
    delete holder;
    vTaskDelete(nullptr);
}

void BasicHost::task_body() {
    basic::BasicInterpreter interp;
    _interp = &interp;
    install(interp);
    interp.set_step_hook([] { vTaskDelay(1); });   /* 每 4096 步让出，防饿死 idle/WDT */
    interp.set_timer_check([this]() { return _after_timer_check(); });   /* AFTER 定时跳转 */

    interp.clear_program();
    if (!interp.load_file(_path)) {
        _rc = 2; _running = false; _finished = true; _interp = nullptr;
        return;
    }
    _inject_env(interp);
    _state_reset();

    _running = true;
    int rc = interp.run(0x7FFFFFFF);   /* 步数上限放开：强停靠 abort + 返回键 */
    _rc = rc;
    _running = false;
    _finished = true;
    _interp = nullptr;
}

void BasicHost::_interp_abort() {
    /* 行级检查在解释器 while 循环；阻塞语句由各 cmd 自查 _abort_flag */
    _abort_flag = true;
}

void BasicHost::_inject_env(basic::BasicInterpreter& interp) {
#if HOST_HAS_NET
    interp.set_svar("wifi_ssid$", net::sta_connected() ? net::sta_ssid() : "");
    interp.set_svar("wifi_pass$", net::sta_pass());
    interp.set_svar("ap_ssid$",   net::ap_ssid());
    interp.set_svar("ap_pass$",   net::ap_pass());
    interp.set_svar("ble_name$",  net::ble_name());
#else
    interp.set_svar("wifi_ssid$", "");
    interp.set_svar("ap_ssid$",   "");
    interp.set_svar("ble_name$",  "");
#endif
    interp.set_nvar("ime_cn", ime::ime_cn() ? 1.0 : 0.0);
}

std::string BasicHost::name_stem() const {
    std::string n = _path;
    size_t slash = n.find_last_of('/');
    if (slash != std::string::npos) n = n.substr(slash + 1);
    if (n.size() > 4 && n.substr(n.size() - 4) == ".bas") n = n.substr(0, n.size() - 4);
    return n;
}

/* ============================================================
 *  HTTP（照搬 PRGM 实现，错误回写消息列表可见）
 * ============================================================ */
std::vector<std::string> BasicHost::split_top(const std::string& s) {
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

void BasicHost::_http_exec(const std::string& raw, bool post) {
    s_http_status.store(0);
    _http_resp.clear();
    size_t need = post ? 3 : 2;
    auto parts = split_top(raw);
    if (!_interp) return;
    if (parts.size() < need) { std::lock_guard<std::mutex> lk(_mtx); _list_add("HTTP: 参数不足"); return; }

    std::string var = parts.back();
    bool var_ok = var.size() >= 2 && var.back() == '$';
    for (size_t i = 0; var_ok && i + 1 < var.size(); i++)
        if (!(std::isalnum((unsigned char)var[i]) || var[i] == '_')) var_ok = false;
    if (!var_ok) { std::lock_guard<std::mutex> lk(_mtx); _list_add("HTTP: 末参数须为字符串变量名"); return; }

    std::string head_args;
    for (size_t i = 0; i + 1 < parts.size(); i++) {
        if (i) head_args += ",";
        head_args += parts[i];
    }
    auto num_l = [this](const std::string& s) { return _interp->eval_num_expr(s); };
    auto str_l = [this](const std::string& s) { return _interp->eval_str_expr(s); };
    auto vals = basic::parse_args(head_args, num_l, str_l);
    if (vals.empty() || !vals[0].is_string) {
        std::lock_guard<std::mutex> lk(_mtx); _list_add("HTTP: URL 需为字符串"); return;
    }
    std::string body;
    if (post) {
        if (vals.size() < 2 || !vals[1].is_string) {
            std::lock_guard<std::mutex> lk(_mtx); _list_add("HTTP: body 需为字符串"); return;
        }
        body = vals[1].str;
    }

    _http_request(vals[0].str, body, post);
    if (!_interp->set_svar(var, _http_resp)) {
        std::lock_guard<std::mutex> lk(_mtx); _list_add("HTTP: 变量写入失败");
    }
}

void BasicHost::_http_file(const std::string& raw, bool up) {
    s_http_status.store(0);
    if (!_interp) return;
    auto parts = split_top(raw);
    if (parts.size() < 2) { std::lock_guard<std::mutex> lk(_mtx); _list_add("HTTP: 参数不足"); return; }
    auto num_l = [this](const std::string& s) { return _interp->eval_num_expr(s); };
    auto str_l = [this](const std::string& s) { return _interp->eval_str_expr(s); };
    auto vals = basic::parse_args(parts[0] + "," + parts[1], num_l, str_l);
    if (vals.size() < 2 || !vals[0].is_string || !vals[1].is_string) {
        std::lock_guard<std::mutex> lk(_mtx); _list_add("HTTP: url/path 需为字符串"); return;
    }
    const std::string& url = vals[0].str;
    const std::string& path = vals[1].str;

    if (up) {
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) { std::lock_guard<std::mutex> lk(_mtx); _list_add("HTTP: 文件打开失败"); return; }
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (sz < 0 || sz > 2 * 1024 * 1024) {
            std::fclose(f);
            std::lock_guard<std::mutex> lk(_mtx); _list_add("HTTP: 文件超 2MB"); return;
        }
        std::string body((size_t)sz, '\0');
        size_t rd = sz ? std::fread(&body[0], 1, (size_t)sz, f) : 0;
        std::fclose(f);
        if ((long)rd != sz) { std::lock_guard<std::mutex> lk(_mtx); _list_add("HTTP: 文件读取失败"); return; }
        _http_request(url, body, true);
    } else {
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) { std::lock_guard<std::mutex> lk(_mtx); _list_add("HTTP: 文件创建失败"); return; }
        _http_request(url, "", false, f);
        std::fclose(f);
    }

    if (parts.size() >= 3) {
        std::string var = parts[2];
        bool var_ok = var.size() >= 2 && var.back() == '$';
        for (size_t i = 0; var_ok && i + 1 < var.size(); i++)
            if (!(std::isalnum((unsigned char)var[i]) || var[i] == '_')) var_ok = false;
        if (var_ok && _interp) _interp->set_svar(var, _http_resp);
    }
}

void BasicHost::_http_request(const std::string& url, const std::string& body,
                              bool post, FILE* sink) {
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 10000;
    cfg.buffer_size = 4096;
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) { std::lock_guard<std::mutex> lk(_mtx); _list_add("HTTP: 初始化失败"); return; }

    esp_http_client_set_method(c, post ? HTTP_METHOD_POST : HTTP_METHOD_GET);
    if (post) esp_http_client_set_header(c, "Content-Type", "application/octet-stream");
    int send_len = post ? (int)body.size() : 0;

    if (esp_http_client_open(c, send_len) == ESP_OK) {
        if (send_len) esp_http_client_write(c, body.data(), (size_t)send_len);
        esp_http_client_fetch_headers(c);
        s_http_status.store(esp_http_client_get_status_code(c));
        char buf[512];
        const size_t CAP = 1 * 1024 * 1024;
        while (true) {
            if (_abort_flag.load()) break;
            int n = esp_http_client_read(c, buf, sizeof(buf));
            if (n <= 0) break;
            if (sink) {
                std::fwrite(buf, 1, (size_t)n, sink);
            } else {
                if (_http_resp.size() + (size_t)n > CAP) {
                    std::lock_guard<std::mutex> lk(_mtx); _list_add("HTTP: 响应超 1MB 截断");
                    break;
                }
                _http_resp.append(buf, (size_t)n);
            }
        }
    } else {
        std::lock_guard<std::mutex> lk(_mtx); _list_add("HTTP: 连接失败");
    }
    esp_http_client_cleanup(c);
}

} // namespace modes
} // namespace memoria

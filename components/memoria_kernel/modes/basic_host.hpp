/**
 * @file basic_host.hpp
 * @brief BASIC APP 宿主层：GUI 控件状态 + 事件环 + 任务运行器
 *
 * 与 PRGM 运行页（主任务同步 run）不同，APP 模式由 AppRunWindow 开独立
 * FreeRTOS 任务跑解释器，主循环照常分发键盘/摇杆事件，程序经 key() 取用。
 *
 * 渲染模型：BASIC 任务只改控件状态（win/list/btn/绘制语句写入状态表），
 * render() 由主循环 render_all 每帧重放（本系统 framebuffer + 每帧全量
 * flush 架构，无撕裂无双任务抢 SPI）。"局部刷新" = 程序只改状态、宿主
 * 统一重绘、framebuffer 直推无闪。
 *
 * 坐标系：app 区 y=0 在状态栏下（渲染时统一 +16 偏移），可用区 480×304。
 *
 * key() 键码表（与模拟器同步点，勿单方改）：
 *   0 无 / 1上 2下 3左 4右 5摇杆按下 / 32~126 ASCII / 13 回车 / 8 退格 / 27 Esc
 *
 * GUI 语句（小写书写，解释器大小写不敏感）：
 *   win x,y,w,h,"标题"          面板：底色+边框+标题条
 *   list x,y,w,h                定义消息列表（单实例，带边框）
 *   ladd "文本"                 追加一行（按宽换行、容量 300 行、自动跟随滚动）
 *   lclr / lscr n               清空列表 / 滚动（+向下 -向上）
 *   btn x,y,w,h,"文字"          按钮（创建顺序编号 0..7）
 *   bsel i                      按钮焦点高亮
 *   bkey(i)                     查询按钮 i 是否被"按下"（回车/摇杆确认，读后清零）
 *   lbl id,x,y,"文本"[,style]   文本控件：id 化创建/更新（0 正常 1 反白 2 粗体）
 *   llbl id,"新文本"[,style]    只改文本控件内容
 *   lbld id                     删除文本控件
 *   img id,x,y                  内置图标（0 电池 1 WiFi 2 音符 3 文件夹 4 齿轮 5 星 6 心 7 右箭头）
 *   text x,y,"文本"[,style]     直绘文本（style 同上，颜色走 color）
 *   after ms,line               非阻塞定时：ms 后跳转 line（一次性，阻塞语句期间不触发）
 *   inputat x,y,w,h             设定下一次 input a$ 的输入框位置（IME 条画框下）
 *   delay ms                    让出 CPU
 *   key()                       非阻塞取键事件
 */
#pragma once

#include "basic_interpreter.hpp"
#include "window.hpp"

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace memoria {
namespace modes {

class BasicHost {
public:
    BasicHost() = default;
    ~BasicHost() = default;

    /* 注册全部宿主命令（显示重放/GUI/HTTP/语音/连接/系统/环境注入） */
    void install(basic::BasicInterpreter& interp);

    /* ---- 事件输入（主循环任务调用） ---- */
    void feed_key(uint16_t key, bool pressed);
    void feed_nav(int dir);                 /* 1上 2下 3左 4右 5按下 */
    void feed_text(const char* utf8, size_t len);   /* IME 上屏 → 输入缓冲 */
    void input_key(uint16_t key);           /* input a$ 会话期按键（退格/回车/字符） */

    /* ---- 渲染重放（主循环任务调用） ---- */
    void render(window::UIRenderer* ui);

    /* ---- 运行（APP 任务调用；task_tramp 自删） ---- */
    static void task_tramp(void* arg);
    void task_body();
    void set_path(const std::string& p) { _path = p; }
    const std::string& path() const { return _path; }
    std::string name_stem() const;          /* 文件名去 .bas */
    void abort() { _abort_flag = true; _interp_abort(); }
    bool running() const { return _running.load(); }
    bool finished() const { return _finished.load(); }
    int  result() const { return _rc.load(); }
    bool input_active() const { return _in_input.load(); }

private:
    /* ---- 控件状态（BASIC 任务写 / 主循环读，_mtx 保护） ---- */
    struct Panel  { window::Rect r{}; std::string title; };
    struct Button { window::Rect r{}; std::string label; };
    struct Label  { int16_t id = 0, x = 0, y = 0; std::string text; uint32_t color = 0xFFFF; uint8_t style = 0; };
    struct AfterTimer { uint32_t due_ms = 0; int line = 0; };   /* 一次性定时跳转 */
    enum class Op : uint8_t { Text, Fill, Rect, Line, Circle, Icon };
    struct DrawOp {
        Op op = Op::Text;
        int16_t x = 0, y = 0, w = 0, h = 0, x1 = 0, y1 = 0;
        uint32_t color = 0;
        uint8_t style = 0;        /* 文本样式：0 正常 1 反白 2 粗体 */
        std::string text;
    };

    void _interp_abort();                   /* _interp 存在时 request_abort */
    void _state_reset();                    /* 清控件状态（run 前 / CLEAR） */
    void _list_add(const std::string& text);
    void _mark_btn_press();                 /* 确认键/摇杆按下 → 记入聚焦按钮 */

    /* 命令实现（BASIC 任务上下文） */
    void _cmd_win(const std::vector<basic::BasicArg>& a);
    void _cmd_list(const std::vector<basic::BasicArg>& a);
    void _cmd_ladd(const std::vector<basic::BasicArg>& a);
    void _cmd_btn(const std::vector<basic::BasicArg>& a);
    void _cmd_bsel(const std::vector<basic::BasicArg>& a);
    void _cmd_draw_op(Op op, const std::vector<basic::BasicArg>& a);
    void _cmd_lbl(const std::vector<basic::BasicArg>& a);
    void _cmd_llbl(const std::vector<basic::BasicArg>& a);
    void _cmd_lbld(const std::vector<basic::BasicArg>& a);
    void _cmd_img(const std::vector<basic::BasicArg>& a);
    void _cmd_after(const std::vector<basic::BasicArg>& a);
    double _func_bkey(const std::vector<basic::BasicArg>& a);
    int  _after_timer_check();              /* 解释器每行回调：到期返回跳转行号 */
    std::string _input_str(const std::string& prompt);
    void _http_exec(const std::string& raw, bool post);
    void _http_file(const std::string& raw, bool up);
    void _http_request(const std::string& url, const std::string& body,
                       bool post, FILE* sink = nullptr);
    void _inject_env(basic::BasicInterpreter& interp);

    /* 键事件环（主循环写 / BASIC 任务读，portMUX 保护） */
    static const int RING_N = 32;
    uint8_t _ring[RING_N] = {};
    volatile int _rh = 0, _rt = 0;
    portMUX_TYPE _ring_mux = portMUX_INITIALIZER_UNLOCKED;
    void _ring_push(uint8_t code);
    uint8_t _ring_pop();

    /* ---- 状态 ---- */
    std::mutex _mtx;
    basic::BasicInterpreter* _interp = nullptr;   /* 任务体持有，弱引用 */
    std::string _path;

    std::vector<Panel>   _panels;
    std::vector<Button>  _btns;
    int      _btn_focus = -1;
    uint8_t  _btn_press[8] = {};            /* 按钮按下标记（bkey(i) 读后清零） */
    window::Rect _list_r{};
    bool     _has_list = false;
    std::vector<std::string> _list_lines;   /* 已按宽换行的展示行 */
    int      _list_scroll = 0;              /* 顶部展示行号 */
    window::Rect _input_r{};
    bool     _has_input_r = false;
    std::vector<DrawOp> _ops;               /* 直接绘制语句重放表 */
    std::vector<Label>  _labels;            /* lbl 文本控件（上限 16，按 id 更新） */
    std::vector<AfterTimer> _after;         /* after 定时跳转（上限 8，BASIC 任务线程独用） */
    uint32_t _cur_color = 0xFFFF;           /* RGB565 白 */

    /* input a$ 会话（BASIC 任务等 / 主循环写） */
    std::atomic<bool> _in_input{false};
    std::string _input_prompt, _input_buf;
    bool _input_enter = false;

    /* HTTP（照搬 PRGM 实现；错误回写列表可见） */
    std::string _http_resp;
    static std::atomic<int> s_http_status;

    /* 运行状态 */
    std::atomic<bool> _running{false};
    std::atomic<bool> _finished{false};
    std::atomic<bool> _abort_flag{false};
    std::atomic<int>  _rc{-1};

    static std::vector<std::string> split_top(const std::string& s);
};

} // namespace modes
} // namespace memoria

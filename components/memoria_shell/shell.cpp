/**
 * @file shell.cpp
 * @brief 真实 Shell 实现
 *
 * 所有命令均有实际实现（读 WifiManager/BatteryAdc/Joystick/RtcClock 单例、
 * 调用 script runtime、package_manager、ota_updater）。
 * REPL 主循环通过 stdin 阻塞读。
 */

#include "shell.hpp"
#include "joystick.hpp"
#include "battery_adc.hpp"
#include "rtc_clock.hpp"
#include "wifi_manager.hpp"
#include "ble_manager.hpp"
#include "sdcard.hpp"
#include "power.hpp"
#include "i2s_audio.hpp"
#include "kernel.hpp"   /* reboot */
#include "evaluator.hpp"
#include "package_manager.hpp"
#include "ota_updater.hpp"
#include "math_solver.hpp"

extern "C" {
#include <esp_log.h>
#include <esp_system.h>
#include <esp_app_desc.h>
#include "rom/ets_sys.h"   /* ets_get_cpu_frequency（v6 无 esp_clk.h） */
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <driver/temperature_sensor.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <dirent.h>
}

#include <cstdio>
#include <cstring>

#include <sstream>
#include <fstream>
#include <vector>
#include <string>
#include <cctype>
#include <cstdarg>

namespace memoria { namespace script { int run_file(const std::string& path); } }

namespace memoria {
namespace shell {

static void _print_help(bool safe) {
    if (safe) {
        std::fprintf(stdout,
            "help                 show this help\n"
            "info                 system info (version, uptime, heap)\n"
            "sys                  full system status (cpu/temp/load/mem/rf/battery)\n"
            "top                  per-task CPU usage table\n"
            "battery              battery level + voltage\n"
            "backlight <0-255>    set LCD backlight (no arg = show)\n"
            "wifi                 wifi status (ssid, rssi)\n"
            "ble                  BLE adv status, connected count\n"
            "sd                   SD card mount status\n"
            "sd mount             retry TF card mount\n"
            "sd format            ERASE ALL DATA on TF card and re-mount\n"
            "reboot               restart ESP32\n"
            "quit                 exit shell\n"
            "(no TF card: desktop runs normally, SD features show 'insert card')\n");
        return;
    }
    std::fprintf(stdout,
        "memoria> help                 show this help\n"
        "memoria> info                 system info (memoria version, uptime, build)\n"
        "memoria> sys                  full system status (cpu/temp/load/mem/rf/battery)\n"
        "memoria> top                  per-task CPU usage table\n"
        "memoria> battery              battery level + voltage\n"
        "memoria> wifi                 wifi status (ssid, rssi)\n"
        "memoria> ble                  BLE adv status, connected count\n"
        "memoria> sd                   SD card mount status\n"
        "memoria> sd mount             retry TF card mount\n"
        "memoria> sd format            ERASE ALL DATA on TF card and re-mount\n"
        "memoria> scripts              list all scripts in /mem_fat/scripts\n"
        "memoria> script <name>        run a .msp script from /mem_fat/scripts\n"
        "memoria> solve <expr>         solve equations (3x+5=20 | x^2-5x+6=0 | 2x+y=5,x-y=1)\n"
        "memoria> pkg_update           check + download packages now\n"
        "memoria> pkg_mirror <url>     set package manifest URL (persistent)\n"
        "memoria> pkg_cfg              show current package config\n"
        "memoria> ota_url <url>        set firmware OTA URL (persistent)\n"
        "memoria> ota_firmware         start firmware OTA now (reboot on success)\n"
        "memoria> rec start            start INMP441 recording -> /mem_fat/rec.wav\n"
        "memoria> rec stop             stop recording, finalize WAV header\n"
        "memoria> reboot               restart ESP32\n"
        "memoria> quit                 exit shell\n");
}

static std::vector<std::string> split_args(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool in_q = false;
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (c == '"') { in_q = !in_q; continue; }
        if (std::isspace((unsigned char)c) && !in_q) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

Shell::Shell() {}

int Shell::execute(const std::string& line, bool safe) {
    auto args = split_args(line);
    if (args.empty() || args[0].empty()) return 0;
    const std::string& cmd = args[0];

    if (cmd == "help") { _print_help(safe); return 0; }

    if (cmd == "info") {
        std::fprintf(stdout, "memoria os v1.3.0 (ESP32-S3)\n");
        std::fprintf(stdout, "uptime_ms = %lu\n", (unsigned long)(esp_timer_get_time() / 1000));
        std::fprintf(stdout, "free_heap = %lu B\n", (unsigned long)esp_get_free_heap_size());
        std::fprintf(stdout, "psram     = %lu B free\n",
                     (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        return 0;
    }

    if (cmd == "sys") {
        /* 全系统状态一览：芯片/温度/双核负载/内存/射频/存储/电池（一次看全） */
        static temperature_sensor_handle_t s_ts = nullptr;   /* lazy 安装：首次 sys 调用 */
        float celsius = -273.15f;
        if (!s_ts) {
            temperature_sensor_config_t tcfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 80);
            if (temperature_sensor_install(&tcfg, &s_ts) == ESP_OK)
                temperature_sensor_enable(s_ts);
        }
        if (s_ts) temperature_sensor_get_celsius(s_ts, &celsius);

        /* 双核负载：run time stats 反推（开机以来平均，近似值——idle 时间占总时间的补） */
        UBaseType_t n = uxTaskGetNumberOfTasks();
        TaskStatus_t* st = (TaskStatus_t*)std::malloc((size_t)n * sizeof(TaskStatus_t));
        uint32_t total_rt = 0, idle0 = 0, idle1 = 0;
        double ld_total = -1, ld_c0 = -1, ld_c1 = -1;
        if (st) {
            UBaseType_t got = uxTaskGetSystemState(st, n, &total_rt);
            if (got > 0 && total_rt > 0) {
                for (UBaseType_t i = 0; i < got; i++) {
                    if (!std::strcmp(st[i].pcTaskName, "IDLE0")) idle0 = st[i].ulRunTimeCounter;
                    else if (!std::strcmp(st[i].pcTaskName, "IDLE1")) idle1 = st[i].ulRunTimeCounter;
                }
                ld_total = 100.0 * (1.0 - (double)(idle0 + idle1) / (double)total_rt);
                ld_c0 = 100.0 - 200.0 * (double)idle0 / (double)total_rt;   /* 假设两核均摊总时基 */
                ld_c1 = 100.0 - 200.0 * (double)idle1 / (double)total_rt;
            }
            std::free(st);
        }

        const esp_app_desc_t* ad = esp_app_get_description();
        std::fprintf(stdout, "==== Memoria OS System ====\n");
        std::fprintf(stdout, "version  : %s  uptime %lu s\n", ad->version,
                     (unsigned long)(esp_timer_get_time() / 1000000ULL));
        std::fprintf(stdout, "cpu      : %lu MHz  temp %.1f C\n",
                     (unsigned long)ets_get_cpu_frequency(), celsius);
        if (ld_total >= 0)
            std::fprintf(stdout, "load     : total %.0f%%  core0 ~%.0f%%  core1 ~%.0f%% (boot avg)\n",
                         ld_total, ld_c0, ld_c1);
        std::fprintf(stdout, "tasks    : %u\n", (unsigned)uxTaskGetNumberOfTasks());
        std::fprintf(stdout, "heap int : %lu KB free (largest %lu, min-ever %lu)\n",
                     (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024,
                     (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                     (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
        std::fprintf(stdout, "psram    : %lu KB free\n",
                     (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
        {
            auto* w = drivers::WifiManager::instance();
            std::fprintf(stdout, "wifi     : rf=%s conn=%s\n",
                         w->rf_started() ? "on" : "off", w->connected() ? "yes" : "no");
        }
        {
            auto* b = drivers::BleManager::instance();
            std::fprintf(stdout, "ble      : conn=%s\n", b->connected() ? "yes" : "no");
        }
        std::fprintf(stdout, "sd       : %s\n",
                     drivers::SdCard::instance()->is_mounted() ? "mounted" : "not mounted");
        {
            auto* bt = drivers::BatteryAdc::instance();
            std::fprintf(stdout, "battery  : %d%%  %.2fV\n", bt->percent(), bt->voltage());
        }
        std::fprintf(stdout, "backlight: %u/255\n",
                     (unsigned)drivers::Power::instance()->get_backlight());
        return 0;
    }

    if (cmd == "top") {
        /* 任务 CPU 占用表（开机以来平均）：名/核内占比/栈余量 */
        UBaseType_t n = uxTaskGetNumberOfTasks();
        TaskStatus_t* st = (TaskStatus_t*)std::malloc((size_t)n * sizeof(TaskStatus_t));
        if (!st) { std::fprintf(stdout, "top: no memory\n"); return 0; }
        uint32_t total_rt = 0;
        UBaseType_t got = uxTaskGetSystemState(st, n, &total_rt);
        if (got == 0 || total_rt == 0) {
            std::fprintf(stdout, "top: no stats yet\n");
            std::free(st);
            return 0;
        }
        std::fprintf(stdout, "%-16s %6s %8s %s\n", "task", "cpu%", "stk/B", "state");
        for (UBaseType_t i = 0; i < got; i++) {
            if (st[i].ulRunTimeCounter == 0) continue;   /* 只列跑过的时间 */
            std::fprintf(stdout, "%-16s %5.1f%% %8lu %s\n",
                         st[i].pcTaskName,
                         100.0 * (double)st[i].ulRunTimeCounter / (double)total_rt,
                         (unsigned long)st[i].usStackHighWaterMark,
                         st[i].eCurrentState == eRunning ? "run" :
                         st[i].eCurrentState == eReady   ? "rdy" :
                         st[i].eCurrentState == eBlocked ? "blk" : "sus");
        }
        std::free(st);
        return 0;
    }

    if (cmd == "solve") {
        /* 解方程（memoria_math / SOLVER-SPEC）：串口直验口。
         * 解析器忽略空格，token 直接拼接即可；方程组逗号写在式子里 */
        if (args.size() < 2) {
            std::fprintf(stdout, "usage: solve 3x+5=20 | solve x^2-5x+6=0 | solve 2x+y=5,x-y=1\n");
            return 0;
        }
        std::string expr;
        for (size_t i = 1; i < args.size(); i++) expr += args[i];
        auto r = math::solve_equations(expr);
        std::fprintf(stdout, "%s\n", r.text().c_str());
        return 0;
    }

    if (cmd == "battery") {
        auto* b = drivers::BatteryAdc::instance();
        int pct = b->percent();
        float v = b->voltage();
        bool chg = false;  /* BatteryAdc has no charging state */
        (void)chg;
        std::fprintf(stdout, "battery: %d%%  %.2fV  %s\n",
                     pct, v, chg ? "charging" : "discharging");
        return 0;
    }

    if (cmd == "backlight") {
        /* 串口直接调背光：BOD 观测 + 应急点亮（屏幕被误关时救场） */
        auto* p = drivers::Power::instance();
        if (args.size() >= 2) {
            int v = std::atoi(args[1].c_str());
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            p->set_backlight(static_cast<uint8_t>(v));
            p->touch_event();   /* 视为活动：重置 idle 计时防立刻又被压暗 */
        }
        std::fprintf(stdout, "backlight: %u / 255\n",
                     (unsigned)p->get_backlight());
        return 0;
    }

    if (cmd == "wifi") {
        auto* w = drivers::WifiManager::instance();
        if (!w->connected()) { std::fprintf(stdout, "wifi: disconnected\n"); return 0; }
        std::fprintf(stdout, "wifi: ssid=\"%s\" rssi=%ld dBm\n",
                     w->saved_ssid().c_str(), (long)w->rssi());
        return 0;
    }

    if (cmd == "ble") {
        auto* b = drivers::BleManager::instance();
        std::fprintf(stdout, "ble: conn=%s\n", b->connected() ? "on" : "off");
        return 0;
    }

    if (cmd == "sd") {
        auto* s = drivers::SdCard::instance();
        if (args.size() >= 2 && args[1] == "mount") {
            esp_err_t e = s->init(false);   /* 手动挂载：非 quiet，出完整错误 */
            if (e == ESP_OK) std::fprintf(stdout, "sd mount: OK, card mounted at %s\n", s->mount_point().c_str());
            else             std::fprintf(stdout, "sd mount: failed (%s) - insert card or try 'sd format'\n", esp_err_to_name(e));
            return 0;
        }
        if (args.size() >= 2 && args[1] == "format") {
            std::fprintf(stdout, "sd format: erasing ALL data on TF card...\n");
            esp_err_t e = s->reformat();
            if (e == ESP_OK) std::fprintf(stdout, "sd format: OK, card re-mounted at %s\n", s->mount_point().c_str());
            else             std::fprintf(stdout, "sd format: failed (%s)\n", esp_err_to_name(e));
            return 0;
        }
        std::fprintf(stdout, "sd: mounted=%s\n", s->is_mounted() ? "yes" : "no");
        return 0;
    }

    if (cmd == "scripts") {
        /* readdir 列出 /mem_fat/scripts 目录 */
        std::string dir = "/mem_fat/scripts";
        DIR* d = opendir(dir.c_str());
        if (!d) { std::fprintf(stdout, "scripts: cannot open %s\n", dir.c_str()); return 0; }
        std::fprintf(stdout, "scripts:\n");
        struct dirent* e;
        while ((e = readdir(d)) != nullptr) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
            std::fprintf(stdout, "  %s\n", e->d_name);
        }
        closedir(d);
        return 0;
    }

    if (cmd == "script" && args.size() >= 2) {
        /* 文件名白名单 [A-Za-z0-9_.-]，拒绝路径穿越（../ 可读任意路径） */
        const std::string& name = args[1];
        bool ok = !name.empty() && name != "." && name != "..";
        if (ok) for (char ch : name) {
            if (!(std::isalnum((unsigned char)ch) || ch == '_' || ch == '.' || ch == '-')) { ok = false; break; }
        }
        if (!ok) { std::fprintf(stdout, "script: invalid name\n"); return 0; }
        std::string path = "/mem_fat/scripts/" + name;
        std::fprintf(stdout, "running %s ...\n", path.c_str());
        /* #86：失败码只打印不冒泡——execute 返回 1 会被主循环当作退出 shell */
        int r = script::run_file(path);
        if (r != 0) std::fprintf(stdout, "script: exited with code %d\n", r);
        return 0;
    }

    if (cmd == "pkg_update") {
        int a = 0, r = 0;
        package::check_updates(&a, &r);
        std::fprintf(stdout, "pkg_update: +%d -%d\n", a, r);
        return 0;
    }

    if (cmd == "pkg_mirror" && args.size() >= 2) {
        package::set_mirror_url(args[1]);
        std::fprintf(stdout, "pkg_mirror: set to %s\n", args[1].c_str());
        return 0;
    }

    if (cmd == "pkg_cfg") {
        std::fprintf(stdout, "pkg: mirror=%s\n", package::get_mirror_url().c_str());
        std::fprintf(stdout, "pkg: auto_update=%s at %02u:%02u\n",
                     package::get_auto_update() ? "on" : "off",
                     package::get_auto_hour(), package::get_auto_min());
        return 0;
    }

    if (cmd == "ota_url" && args.size() >= 2) {
        package::set_ota_url(args[1]);
        std::fprintf(stdout, "ota_url: set to %s\n", args[1].c_str());
        return 0;
    }

    if (cmd == "ota_firmware") {
        std::fprintf(stdout, "starting firmware OTA...\n");
        /* #86：OTA 失败码（1/2/3）只打印不冒泡；成功路径在 ota_firmware 内部直接 esp_restart */
        int r = package::ota_firmware();
        if (r != 0) std::fprintf(stdout, "ota: failed (code %d)\n", r);
        return 0;
    }

    if (cmd == "rec") {
        if (args.size() >= 2 && args[1] == "start") {
            esp_err_t e = drivers::I2sAudio::instance()->rec_start("/mem_fat/rec.wav");
            std::fprintf(stdout, e == ESP_OK
                ? "rec: recording -> /mem_fat/rec.wav (rec stop to finalize)\n"
                : "rec: start failed (%d)\n", (int)e);
            return 0;
        }
        if (args.size() >= 2 && args[1] == "stop") {
            esp_err_t e = drivers::I2sAudio::instance()->rec_stop();
            if (e == ESP_OK) {
                uint32_t secs = drivers::I2sAudio::instance()->_rec_seconds.load();
                std::fprintf(stdout, "rec: stopped, %lu s -> /mem_fat/rec.wav\n", (unsigned long)secs);
            } else {
                std::fprintf(stdout, "rec: not recording (%d)\n", (int)e);
            }
            return 0;
        }
        std::fprintf(stdout, "rec: usage: rec start | rec stop\n");
        return 0;
    }

    if (cmd == "reboot") {
        std::fprintf(stdout, "rebooting...\n");
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
        return 0;
    }

    if (cmd == "quit" || cmd == "exit") {
        std::fprintf(stdout, "bye.\n");
        return 1;   /* 退出 REPL */
    }

    std::fprintf(stdout, "shell: unknown command '%s' — try 'help'\n", cmd.c_str());
    return 0;
}

void Shell::menu() {
    /* SAFE 模式：TF 卡未挂载时进应急态（提示符 safe>，sd mount 成功自动退出） */
    bool safe = !drivers::SdCard::instance()->is_mounted();
    if (safe)
        std::fprintf(stdout, "\nSAFE MODE (no TF card). 'help' for commands.\n");
    else
        std::fprintf(stdout, "\nMemoria Shell (type 'help')\n");
    while (true) {
        std::fprintf(stdout, safe ? "safe> " : "memoria> ");
        std::fflush(stdout);

        std::string line;
        for (;;) {
            int c = std::getchar();
            if (c == '\n' || c == '\r') {
                /* 终端发 CRLF 时吞掉配对符，否则残留的 \r/\n 会空转一轮
                 * 再打一个 prompt（真机 "safe> safe> " 双提示符实录） */
                int n = std::getchar();
                if (n == '\n' || n == '\r') { /* 配对符已吞 */ }
                else if (n != EOF) line += (char)n;
                break;
            }
            /* 修复忙等：stdin 非阻塞时 getchar 立即返回 EOF，旧代码空转刷屏
             * 并饿死 IDLE0（task_wdt 告警）——无数据时让出 CPU 喂狗 */
            if (c == EOF) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
            line += (char)c;
        }
        /* 处理退格 */
        size_t bk;
        while ((bk = line.find('\b')) != std::string::npos)
            line = line.substr(0, bk) + line.substr(bk + 1);

        if (execute(line, safe) == 1) break;
        /* 关键：stdout 全缓冲时 execute 的输出不出 UART，敲 help 全被吞
         *（真机实录）。每轮命令后强制刷出。 */
        std::fflush(stdout);
        safe = !drivers::SdCard::instance()->is_mounted();  /* sd mount 成功自动出 SAFE */
    }
}

} }
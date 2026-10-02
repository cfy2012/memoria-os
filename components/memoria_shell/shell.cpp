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
#include "i2s_audio.hpp"
#include "kernel.hpp"   /* reboot */
#include "evaluator.hpp"
#include "package_manager.hpp"
#include "ota_updater.hpp"

extern "C" {
#include <esp_log.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
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

static void _print_help() {
    std::fprintf(stdout,
        "memoria> help                 show this help\n"
        "memoria> info                 system info (memoria version, uptime, build)\n"
        "memoria> battery              battery level + voltage\n"
        "memoria> wifi                 wifi status (ssid, rssi)\n"
        "memoria> ble                  BLE adv status, connected count\n"
        "memoria> sd                   SD card total/free bytes\n"
        "memoria> scripts              list all scripts in /mem_fat/scripts\n"
        "memoria> script <name>        run a .msp script from /mem_fat/scripts\n"
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

int Shell::execute(const std::string& line) {
    auto args = split_args(line);
    if (args.empty() || args[0].empty()) return 0;
    const std::string& cmd = args[0];

    if (cmd == "help") { _print_help(); return 0; }

    if (cmd == "info") {
        std::fprintf(stdout, "memoria os v1.0.0 (ESP32-S3)\n");
        std::fprintf(stdout, "uptime_ms = %lu\n", (unsigned long)(esp_timer_get_time() / 1000));
        std::fprintf(stdout, "free_heap = %lu B\n", (unsigned long)esp_get_free_heap_size());
        std::fprintf(stdout, "psram     = %lu B free\n",
                     (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
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
        bool m = drivers::SdCard::instance()->present();
        std::fprintf(stdout, "sd: mounted=%s\n", m ? "yes" : "no");
        return 0;
    }

    if (cmd == "scripts") {
        /* readdir 列出 /mem_fat/scripts 目录 */
        std::string dir = "/mem_fat/scripts";
        DIR* d = opendir(dir.c_str());
        if (!d) { std::fprintf(stdout, "scripts: cannot open %s\n", dir.c_str()); return 1; }
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
        if (!ok) { std::fprintf(stdout, "script: invalid name\n"); return 1; }
        std::string path = "/mem_fat/scripts/" + name;
        std::fprintf(stdout, "running %s ...\n", path.c_str());
        return script::run_file(path);
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
        return package::ota_firmware();
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
    std::fprintf(stdout, "\nMemoria Shell (type 'help')\n");
    while (true) {
        std::fprintf(stdout, "memoria> ");
        std::fflush(stdout);

        std::string line;
        for (;;) {
            int c = std::getchar();
            if (c == EOF || c == '\n') break;
            if (c == '\r') continue;
            line += (char)c;
        }
        /* 处理退格 */
        size_t bk;
        while ((bk = line.find('\b')) != std::string::npos)
            line = line.substr(0, bk) + line.substr(bk + 1);

        if (execute(line) == 1) break;
    }
}

} }
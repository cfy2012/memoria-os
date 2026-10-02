/**
 * @file store_mode.cpp
 * @brief STORE 软件商店模式
 *
 * 从软件源拉取 manifest 列表 → 选中安装（下载 + CRC32 校验）→ 已安装管理（卸载）。
 *
 * 换源规则（从高到低）：
 *   1. TF 卡 /mem_fat/store_url.txt 首行内容（插卡改文本即可换源，最方便）
 *   2. 设置里保存的源（NVS "package" 的 mirror）
 *   3. 默认官方源
 *
 * 操作：上下选择 · Enter 确认 · 左右切页 · F 键行快捷操作。
 */

#include "mode_manager.hpp"
#include "json_fetcher.hpp"
#include "package_manager.hpp"
#include "wifi_manager.hpp"

extern "C" {
#include <dirent.h>
#include <sys/stat.h>
}

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>

#include <string>
#include <vector>
#include <algorithm>

namespace memoria {
using namespace drivers;
namespace modes {

struct RemoteItem {
    std::string name;
    std::string version;
    uint32_t    size = 0;
    uint32_t    crc  = 0;
    std::string url;
    bool        installed = false;
};

class StoreMode : public ModeWindow {
public:
    StoreMode() {
        _scan_installed();
        _reload_remote();
    }

    /* ================= 渲染 ================= */
    void mode_render(window::UIRenderer* ui, bool focused) override {
        char right[24];
        snprintf(right, sizeof(right), "%d/3 %s", _page + 1, _online ? "WiFi" : "离线");
        draw_title(ui, "STORE 商店", right);

        if (!_hint.empty()) draw_hint(ui, _hint.c_str());

        switch (_page) {
            case 0: _render_remote(ui, focused); break;
            case 1: _render_installed(ui, focused); break;
            case 2: _render_source(ui); break;
        }
    }

    /* ================= 导航 ================= */
    bool mode_nav(const window::NavInput& ni) override {
        switch (ni.dir) {
            case window::NavEvent::NavUp:    _move(-1); break;
            case window::NavEvent::NavDown:  _move(+1); break;
            case window::NavEvent::NavLeft:  _page = (_page + 2) % 3; _cur = 0; break;
            case window::NavEvent::NavRight: _page = (_page + 1) % 3; _cur = 0; break;
            case window::NavEvent::NavEnter: _enter(); break;
            default: return false;
        }
        return true;
    }

    const char* const* fn_labels() override {
        static const char* f0[6] = {"安装", "刷新", "已装", "源", "-", "菜单"};
        static const char* f1[6] = {"卸载", "刷新", "在线", "源", "信息", "菜单"};
        static const char* f2[6] = {"重读", "官方源", "在线", "已装", "-", "菜单"};
        return _page == 0 ? f0 : (_page == 1 ? f1 : f2);
    }

    void on_fn(int idx) override {
        switch (_page) {
            case 0:
                switch (idx) {
                    case 0: _install_sel(); break;
                    case 1: _reload_remote(); break;
                    case 2: _page = 1; _cur = 0; break;
                    case 3: _page = 2; _cur = 0; break;
                    default: break;
                }
                break;
            case 1:
                switch (idx) {
                    case 0: _uninstall_sel(); break;
                    case 1: _scan_installed(); break;
                    case 2: _page = 0; _cur = 0; break;
                    case 3: _page = 2; _cur = 0; break;
                    case 4: _show_info(); break;
                    default: break;
                }
                break;
            case 2:
                switch (idx) {
                    case 0: _hint = _load_source(); break;
                    case 1:
                        if (package::set_mirror_url(_default_source())) {
                            _hint = "已切回官方源（可 TF 卡自定义）";
                        } else { _hint = "写源失败"; }
                        break;
                    case 2: _page = 0; _cur = 0; break;
                    case 3: _page = 1; _cur = 0; break;
                    default: break;
                }
                break;
        }
    }

    void on_confirm_ok() override {
        if (_pending >= 0 && _pending < (int)_installed.size()) {
            std::string nm = _installed[_pending];
            /* 仅商店包（.msp 约定，见 shell "script <name>"）可卸载，防误删用户自拷的 .bas/.ms */
            bool is_pkg = nm.size() > 4 && nm.substr(nm.size() - 4) == ".msp";
            if (is_pkg) {
                remove((std::string("/mem_fat/scripts/") + nm).c_str());
                _hint = "已卸载 " + nm;
            } else {
                _hint = "非商店包不可卸载（" + nm + "）";
            }
        }
        _pending = -1;
        _scan_installed();
        _reload_remote();
    }

private:
    /* ================= 源 ================= */
    std::string _default_source() {
        return "https://raw.githubusercontent.com/memoria-os/packages/main/manifest.json";
    }
    /* 生效源：TF 卡文件优先，其次 NVS，最后默认 */
    std::string _load_source() {
        FILE* f = std::fopen("/mem_fat/store_url.txt", "r");
        if (f) {
            char buf[256] = {};
            if (std::fgets(buf, sizeof(buf), f)) {
                std::fclose(f);
                std::string s = buf;
                while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
                if (!s.empty()) return s;
                return "TF 卡文件为空";
            }
            std::fclose(f);
        }
        std::string u = package::get_mirror_url();
        if (u.empty() || u.find("http") == std::string::npos) return _default_source();
        return u;
    }

    /* ================= 数据 ================= */
    void _reload_remote() {
        _remote.clear();
        _online = drivers::WifiManager::instance()->connected();
        if (!_online) { _hint = "WiFi 未连接 · 先连网再刷新"; return; }
        _hint = "正在获取软件列表...";

        std::string src = _load_source();
        std::string json = package::json_fetcher::fetch(src);
        if (json.empty()) { _hint = "获取列表失败 · 检查源地址"; return; }

        /* 极简 manifest 解析：[{"name":"..","version":"..","size":..,"crc":0x..,"url":".."}] */
        size_t pos = 0;
        while (pos < json.size()) {
            size_t ob = json.find('{', pos);
            if (ob == std::string::npos) break;
            size_t cb = json.find('}', ob);
            if (cb == std::string::npos) break;

            RemoteItem it;
            auto pick = [&](const char* key) -> std::string {
                std::string pat = std::string("\"") + key + "\":\"";
                size_t kp = json.find(pat, ob);
                if (kp == std::string::npos || kp > cb) return "";
                size_t sp = kp + pat.size();
                size_t ep = json.find('"', sp);
                if (ep == std::string::npos || ep > cb) return "";
                return json.substr(sp, ep - sp);
            };
            auto pick_u32 = [&](const char* key) -> uint32_t {
                std::string pat = std::string("\"") + key + "\":";
                size_t kp = json.find(pat, ob);
                if (kp == std::string::npos || kp > cb) return 0;
                size_t sp = kp + pat.size();
                while (sp < json.size() && (json[sp] == ' ' || json[sp] == '\t')) sp++;
                size_t ep = sp;
                while (ep < json.size() && (std::isdigit(json[ep]) || json[ep] == 'x' || std::isxdigit(json[ep]))) ep++;
                if (ep == sp) return 0;
                return (uint32_t)strtoul(json.substr(sp, ep - sp).c_str(), nullptr, 0);
            };

            it.name    = pick("name");
            it.version = pick("version");
            it.url     = pick("url");
            it.size    = pick_u32("size");
            it.crc     = pick_u32("crc");
            if (!it.name.empty()) {
                it.installed = _file_exists(std::string("/mem_fat/scripts/") + it.name);
                _remote.push_back(it);
            }
            pos = cb + 1;
        }

        _cur = std::min(_cur, (int)_remote.size() - 1);
        char buf[40];
        snprintf(buf, sizeof(buf), "列表 %d 项 · Enter 安装", (int)_remote.size());
        _hint = buf;
    }

    void _scan_installed() {
        _installed.clear();
        DIR* d = opendir("/mem_fat/scripts");
        if (!d) return;
        struct dirent* e;
        while ((e = readdir(d)) != nullptr) {
            if (e->d_name[0] == '.') continue;
            _installed.push_back(e->d_name);
        }
        closedir(d);
        std::sort(_installed.begin(), _installed.end());
        _cur = std::min(_cur, (int)_installed.size() - 1);
    }

    bool _file_exists(const std::string& p) {
        struct stat st{};
        return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
    }

    /* ================= 动作 ================= */
    void _enter() {
        if (_page == 0) _install_sel();
        else if (_page == 1) _show_info();
        else _hint = _load_source();
    }

    void _install_sel() {
        if (_remote.empty()) return;
        if (!_online) { _hint = "WiFi 未连接"; return; }
        RemoteItem& it = _remote[_cur];
        if (it.installed) { _hint = it.name + " 已安装 · 可到已装页卸载"; return; }
        /* 包名白名单：仅 [A-Za-z0-9_.-]，防 manifest 恶意/错误 name 路径穿越 */
        bool name_ok = !it.name.empty() && it.name != "." && it.name != "..";
        for (char ch : it.name) {
            if (!(std::isalnum((unsigned char)ch) || ch == '_' || ch == '.' || ch == '-')) { name_ok = false; break; }
        }
        if (!name_ok) { _hint = "包名非法，已拒绝安装"; return; }
        _hint = "下载中 " + it.name + " ...";

        std::string data = package::json_fetcher::fetch(it.url);
        if (data.empty()) { _hint = "下载失败"; return; }
        /* 安装包显式上限 1MB（脚本包合理上限；json_fetcher 侧另有 4MB 上限校验，防止恶意服务器） */
        const size_t MAX_INSTALL = 1024 * 1024;
        if (data.size() > MAX_INSTALL) { _hint = "包过大（>1MB）· 已拒绝"; return; }
        if (it.crc != 0) {
            uint32_t actual = package::crc32_compute(
                reinterpret_cast<const uint8_t*>(data.data()), data.size());
            if (actual != it.crc) { _hint = "CRC 校验失败 · 已丢弃"; return; }
        }

        std::string path = std::string("/mem_fat/scripts/") + it.name;
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) { _hint = "写入失败 · 检查 TF 卡"; return; }
        std::fwrite(data.data(), 1, data.size(), f);
        std::fclose(f);

        it.installed = true;
        _scan_installed();
        _hint = "安装完成 " + it.name;
    }

    void _uninstall_sel() {
        if (_installed.empty()) { _hint = "没有已安装的应用"; return; }
        _pending = _cur;
        _confirm_msg = "卸载 " + _installed[_cur] + " ?";
        request_confirm(_confirm_msg.c_str());
    }

    void _show_info() {
        if (_installed.empty()) { _hint = "没有已安装的应用"; return; }
        std::string p = std::string("/mem_fat/scripts/") + _installed[_cur];
        struct stat st{};
        if (stat(p.c_str(), &st) == 0) {
            char buf[48];
            snprintf(buf, sizeof(buf), "%s · %u B", _installed[_cur].c_str(), (unsigned)st.st_size);
            _hint = buf;
        }
    }

    void _move(int d) {
        int n = (_page == 0) ? (int)_remote.size() : (int)_installed.size();
        if (n <= 0) return;
        _cur = (_cur + d + n) % n;
    }

    /* ================= 渲染 ================= */
    void _render_remote(window::UIRenderer* ui, bool focused) {
        if (!_online) { draw_hint(ui, focused ? "WiFi 未连接 · 请先联网" : ""); return; }
        if (_remote.empty()) { draw_hint(ui, focused ? "列表为空 · F2 刷新" : ""); return; }
        int y = 34;
        int start = std::max(0, _cur - 7);
        for (int i = start; i < (int)_remote.size() && y < SCREEN_H - 34; i++, y += 12) {
            std::string line = (i == _cur ? ">" : " ") + std::string(" ") + _remote[i].name +
                               (_remote[i].installed ? " *" : "") + " v" + _remote[i].version;
            ui->draw_text(2, y, line, i == _cur ? COLOR_YELLOW : COLOR_WHITE);
        }
    }

    void _render_installed(window::UIRenderer* ui, bool focused) {
        if (_installed.empty()) { draw_hint(ui, focused ? "没有已安装应用 · 到在线页安装" : ""); return; }
        int y = 34;
        int start = std::max(0, _cur - 8);
        for (int i = start; i < (int)_installed.size() && y < SCREEN_H - 34; i++, y += 12) {
            std::string line = (i == _cur ? ">" : " ") + std::string(" ") + _installed[i];
            ui->draw_text(2, y, line, i == _cur ? COLOR_YELLOW : COLOR_WHITE);
        }
    }

    void _render_source(window::UIRenderer* ui) {
        std::string src = _load_source();
        ui->draw_text(6, 40, "当前软件源", COLOR_LIGHT_GRAY);
        /* 逐行折行显示 URL */
        int y = 56;
        std::string rest = src;
        while (!rest.empty() && y < 160) {
            size_t cut = rest.size();
            if (rest.size() > 26) {
                cut = rest.find('/', 26);
                if (cut == std::string::npos) cut = rest.size();
            }
            std::string seg = rest.substr(0, cut);
            ui->draw_text(6, y, seg.c_str(), COLOR_WHITE);
            y += 12;
            rest = (cut < rest.size()) ? rest.substr(cut) : "";
        }
        ui->draw_text(6, 176, "换源：TF 卡 store_url.txt", COLOR_YELLOW);
        ui->draw_text(6, 190, "或 设置 → 软件源", COLOR_LIGHT_GRAY);
    }

    int _page = 0;
    int _cur = 0;
    std::string _confirm_msg;
    bool _online = false;
    std::string _hint;
    std::vector<RemoteItem> _remote;
    std::vector<std::string> _installed;
    int _pending = -1;
};

static std::shared_ptr<window::Window> store_create() {
    return std::make_shared<StoreMode>();
}

void store_mode_register() {
    ModeDesc d{};
    d.id = "store"; d.name = "STORE"; d.cn = "商店";
    d.color = rgb565(34, 211, 238);
    d.create = store_create;
    ModeManager::instance()->register_mode(d);
}

} // namespace modes
} // namespace memoria
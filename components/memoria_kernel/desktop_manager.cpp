/**
 * @file desktop_manager.cpp
 * @brief 桌面布局管理器实现
 */

#include "desktop_manager.hpp"
#include "mode_manager.hpp"

extern "C" {
#include "nvs_flash.h"
#include "nvs.h"
}

#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace memoria {
namespace modes {

static const char* kNs     = "desktop";
static const size_t kCells = 12;

DesktopManager* DesktopManager::instance() {
    static DesktopManager inst;
    return &inst;
}

/* ============================================================
 *  当前层
 * ============================================================ */
std::vector<Cell>* DesktopManager::_layer() {
    if (_stack.empty()) return &_root;
    auto it = _folders.find(_stack.back());
    return it != _folders.end() ? &it->second : &_root;
}

const std::vector<Cell>& DesktopManager::cells() const {
    if (_stack.empty()) return _root;
    auto it = _folders.find(_stack.back());
    return it != _folders.end() ? it->second : _root;
}

const std::string& DesktopManager::folder_name(int fid) const {
    static const std::string kUnknown = "文件夹";
    auto it = _folder_names.find(fid);
    return it != _folder_names.end() ? it->second : kUnknown;
}

/* ============================================================
 *  编码 / 解码（NVS 字符串）
 *   每格一个条目，逗号分隔：
 *     _         空
 *     A:appid   应用
 *     D:2       文件夹 id 2
 * ============================================================ */
std::string DesktopManager::_encode(const std::vector<Cell>& c) const {
    std::string out;
    for (size_t i = 0; i < c.size(); i++) {
        if (i) out += ",";
        const Cell& cell = c[i];
        if (cell.type == CellType::App) { out += "A:"; out += cell.app_id; }
        else if (cell.type == CellType::Folder) {
            char buf[24];
            snprintf(buf, sizeof(buf), "D:%d", cell.folder_id);
            out += buf;
        } else out += "_";
    }
    return out;
}

bool DesktopManager::_decode(const std::string& s, std::vector<Cell>& out) {
    out.clear();
    out.resize(kCells);
    if (s.empty()) return false;
    std::string cur;
    size_t idx = 0;
    for (size_t i = 0; i <= s.size(); i++) {
        if (i == s.size() || s[i] == ',') {
            if (idx < kCells) {
                Cell& cell = out[idx];
                if (cur == "_") { /* 空 */ }
                else if (cur.size() >= 2 && cur[0] == 'A') {
                    cell.type = CellType::App;
                    cell.app_id = cur.substr(2);
                } else if (cur.size() >= 2 && cur[0] == 'D') {
                    cell.type = CellType::Folder;
                    cell.folder_id = atoi(cur.substr(2).c_str());
                    if (_folder_names.count(cell.folder_id) == 0)
                        _folder_names[cell.folder_id] = "文件夹" + std::to_string(cell.folder_id);
                }
            }
            cur.clear();
            idx++;
        } else cur += s[i];
    }
    return true;
}

/* ============================================================
 *  加载 / 保存
 * ============================================================ */
void DesktopManager::load() {
    if (_loaded) return;
    _loaded = true;

    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return;

    size_t len = 0;
    char buf[160];
    if (nvs_get_str(h, "root", nullptr, &len) == ESP_OK && len > 1 && len <= sizeof(buf)) {
        if (nvs_get_str(h, "root", buf, &len) == ESP_OK)
            _decode(std::string(buf, len - 1), _root);
    }
    if (nvs_get_u16(h, "seq", (uint16_t*)&_seq) == ESP_OK) { if (_seq < 1) _seq = 1; }

    /* 读文件夹：遍历 seq 范围内可能存在的 key */
    for (int i = 1; i <= _seq; i++) {
        char key[16];
        snprintf(key, sizeof(key), "f%d", i);
        len = 0;
        if (nvs_get_str(h, key, nullptr, &len) == ESP_OK && len > 1 && len <= sizeof(buf)) {
            if (nvs_get_str(h, key, buf, &len) == ESP_OK) {
                std::vector<Cell> fc;
                _decode(std::string(buf, len - 1), fc);
                _folders[i] = fc;
                if (_folder_names.count(i) == 0)
                    _folder_names[i] = "文件夹" + std::to_string(i);
            }
        }
    }
    nvs_close(h);

    if (_root.empty()) {
        /* 首次：按注册顺序铺满应用 */
        auto* mm = ModeManager::instance();
        for (size_t i = 0; i < kCells && i < (size_t)mm->count(); i++) {
            const ModeDesc* d = mm->get((int)i);
            if (!d) break;
            _root[i].type = CellType::App;
            _root[i].app_id = d->id;
        }
    }
}

void DesktopManager::save() {
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return;

    std::string root = _encode(_root);
    nvs_set_str(h, "root", root.c_str());
    nvs_set_u16(h, "seq", (uint16_t)_seq);
    for (auto& kv : _folders) {
        char key[16];
        snprintf(key, sizeof(key), "f%d", kv.first);
        std::string enc = _encode(kv.second);
        nvs_set_str(h, key, enc.c_str());
    }
    nvs_commit(h);
    nvs_close(h);
}

/* ============================================================
 *  文件夹导航
 * ============================================================ */
bool DesktopManager::enter_folder(int idx) {
    auto* layer = _layer();
    if (idx < 0 || idx >= (int)layer->size()) return false;
    if ((*layer)[idx].type != CellType::Folder) return false;
    _stack.push_back((*layer)[idx].folder_id);
    return true;
}

void DesktopManager::go_up() {
    if (!_stack.empty()) _stack.pop_back();
}

int DesktopManager::_next_folder_id() {
    while (_folders.count(_seq)) _seq++;
    return _seq;
}

bool DesktopManager::create_folder(int idx) {
    auto* layer = _layer();
    if (idx < 0 || idx >= (int)layer->size()) return false;
    if ((*layer)[idx].type != CellType::Empty) return false;
    int fid = _next_folder_id();
    Cell c;
    c.type = CellType::Folder;
    c.folder_id = fid;
    c.folder_name = "文件夹" + std::to_string(fid);
    (*layer)[idx] = c;
    _folder_names[fid] = c.folder_name;
    _folders[fid].resize(kCells);
    _seq = fid + 1;
    save();
    return true;
}

/* ============================================================
 *  拿起 / 放下
 * ============================================================ */
void DesktopManager::move_pick(int idx) {
    auto* layer = _layer();
    if (idx < 0 || idx >= (int)layer->size()) return;
    if ((*layer)[idx].type == CellType::App) _picked = idx;
}

void DesktopManager::move_drop(int idx) {
    auto* layer = _layer();
    if (_picked < 0 || idx < 0 || idx >= (int)layer->size()) return;
    if (_picked == idx) { _picked = -1; return; }
    Cell moving = (*layer)[_picked];
    if (moving.type != CellType::App) { _picked = -1; return; }

    Cell& target = (*layer)[idx];
    if (target.type == CellType::Empty) {
        /* 移到空位：原格变空 */
        (*layer)[_picked] = Cell();
        target = moving;
    } else if (target.type == CellType::App) {
        /* 交换 */
        std::swap((*layer)[_picked], target);
    } else if (target.type == CellType::Folder) {
        /* 图标放入文件夹：该文件夹第一个空位 */
        auto fit = _folders.find(target.folder_id);
        if (fit == _folders.end()) { _picked = -1; return; }
        for (auto& fc : fit->second) {
            if (fc.type == CellType::Empty) {
                fc = moving;
                (*layer)[_picked] = Cell();
                break;
            }
        }
    }
    _picked = -1;
    save();
}

} // namespace modes
} // namespace memoria
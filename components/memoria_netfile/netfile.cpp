/**
 * @file netfile.cpp
 * @brief 浏览器拖文件写 TF 卡：GET / POST /upload|/rm|/format GET /ls
 *
 * 上传走 httpd_req_recv 分块落盘，不在内存里攒整包。
 */

#include "netfile.hpp"
#include "sdcard.hpp"
#include "wifi_manager.hpp"

extern "C" {
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi_default.h>
#include <dirent.h>
#include <sys/stat.h>
}

#include <cstdio>
#include <cstring>
#include <cctype>
#include <string>

namespace memoria {
namespace netfile {

static const char* TAG = "NETFILE";
static httpd_handle_t s_server = nullptr;

static const char kHtml[] = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Memoria 文件传输</title>
<style>
body{font:14px/1.5 system-ui,sans-serif;max-width:680px;margin:32px auto;padding:0 16px;color:#222}
h3{margin:0 0 16px}label{display:block;margin:12px 0 4px;color:#555}
input[type=text]{width:100%;padding:7px;border:1px solid #bbb;border-radius:4px;box-sizing:border-box}
input[type=file]{margin:6px 0}
button{padding:7px 18px;margin:8px 8px 0 0;border:1px solid #888;border-radius:4px;background:#f7f7f7;cursor:pointer}
pre{background:#f4f4f4;padding:10px;border-radius:4px;min-height:60px;white-space:pre-wrap;word-break:break-all;font-size:12px}
</style></head><body>
<h3>Memoria 文件传输</h3>
<label>目标路径（可带子目录，文件路径则直接用；目录以 / 结尾）</label>
<input type="text" id="path" value="/mem_fat/">
<label>选择文件</label>
<input type="file" id="file">
<div>
<button onclick="up()">上传</button>
<button onclick="ls()">列出目录</button>
<button onclick="del()">删除文件</button>
<button onclick="fmt()">格式化 TF 卡</button>
</div>
<pre id="log">就绪</pre>
<script>
const el=id=>document.getElementById(id);
const log=t=>el('log').textContent=t;
function target(){
  let p=el('path').value.trim()||'/mem_fat/';
  const f=el('file').files[0];
  if(p.endsWith('/')){ if(!f){log('请先选择文件');return null;} p+=f.name; }
  return p;
}
function dirOf(){
  let p=el('path').value.trim()||'/mem_fat/';
  const i=p.lastIndexOf('/');
  return i<=0?'/mem_fat':p.slice(0,i);
}
async function up(){
  const f=el('file').files[0];
  if(!f){log('请先选择文件');return;}
  const p=target(); if(!p)return;
  log('上传中 '+f.name+' ('+f.size+' 字节) ...');
  try{
    const r=await fetch('/upload?path='+encodeURIComponent(p),{method:'POST',body:f});
    log(r.status+' '+await r.text());
  }catch(e){log('请求失败: '+e);}
}
async function ls(){
  try{
    const r=await fetch('/ls?dir='+encodeURIComponent(dirOf()));
    const t=await r.text();
    if(!r.ok){log(r.status+' '+t);return;}
    const a=JSON.parse(t);
    log(a.length+' 个文件\n'+a.map(x=>x.name+'  '+x.size+' B').join('\n'));
  }catch(e){log('请求失败: '+e);}
}
async function del(){
  const p=target(); if(!p)return;
  if(!confirm('删除 '+p+' ?'))return;
  try{
    const r=await fetch('/rm?path='+encodeURIComponent(p),{method:'POST'});
    log(r.status+' '+await r.text());
  }catch(e){log('请求失败: '+e);}
}
async function fmt(){
  if(!confirm('格式化会清空 TF 卡全部数据，且不可恢复。继续？'))return;
  if(!confirm('再次确认：确定要格式化吗？'))return;
  try{
    const r=await fetch('/format',{method:'POST'});
    log(r.status+' '+await r.text());
  }catch(e){log('请求失败: '+e);}
}
</script></body></html>
)HTML";

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static std::string url_decode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); i++) {
        char c = in[i];
        if (c == '+') { out += ' '; continue; }
        if (c == '%') {
            if (i + 2 >= in.size()) return {};
            int hi = hex_val(in[i + 1]), lo = hex_val(in[i + 2]);
            if (hi < 0 || lo < 0) return {};
            out += static_cast<char>((hi << 4) | lo);
            i += 2;
            continue;
        }
        out += c;
    }
    return out;
}

static bool get_query(httpd_req_t* req, const char* key, std::string& out) {
    size_t len = httpd_req_get_url_query_len(req) + 1;
    if (len <= 1 || len > 640) return false;
    char q[640];
    if (httpd_req_get_url_query_str(req, q, len) != ESP_OK) return false;
    char val[512];
    if (httpd_query_key_value(q, key, val, sizeof(val)) != ESP_OK) return false;
    out = val;
    return true;
}

static bool under_root(const std::string& p, bool allow_root) {
    static const char* roots[] = { "/mem_fat", "/sd_ext" };
    for (const char* r : roots) {
        size_t n = std::strlen(r);
        if (p.size() == n && p.compare(0, n, r) == 0) return allow_root;
        if (p.size() > n + 1 && p.compare(0, n, r) == 0 && p[n] == '/') return true;
    }
    return false;
}

static bool bad_segments(const std::string& p) {
    size_t i = 0;
    while (i <= p.size()) {
        size_t j = p.find('/', i);
        if (j == std::string::npos) j = p.size();
        if (j - i == 2 && p.compare(i, 2, "..") == 0) return true;
        if (j == i && i > 0) return true;
        if (j == p.size()) break;
        i = j + 1;
    }
    return false;
}

/* 先解码再校验；拼成内核绝对路径后再解码+校验一次 */
static bool resolve(const std::string& raw, bool allow_dir, std::string& out) {
    if (raw.empty()) return false;
    std::string p = url_decode(raw);
    if (p.empty()) return false;
    if (p.find('\0') != std::string::npos) return false;
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    if (p[0] == '\\') return false;
    if (p.size() > 1 && p[1] == ':') return false;
    if (p[0] == '/') { if (!under_root(p, allow_dir)) return false; }
    if (bad_segments(p)) return false;
    if (p[0] != '/') p = "/mem_fat/" + p;
    std::string p2 = url_decode(p);
    if (p2 != p) return false;   /* 解码后仍有编码残留，视为绕过 */
    if (!under_root(p2, allow_dir)) return false;
    if (bad_segments(p2)) return false;
    out = p2;
    return true;
}

static std::string json_escape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += static_cast<char>(c); }
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
        else o += static_cast<char>(c);
    }
    return o;
}

static esp_err_t fail(httpd_req_t* req, const char* status, const char* msg) {
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    std::string b = "{\"ok\":false,\"error\":\"";
    b += json_escape(msg);
    b += "\"}";
    return httpd_resp_send(req, b.c_str(), b.size());
}

static esp_err_t need_mount(httpd_req_t* req) {
    if (!drivers::SdCard::instance()->is_mounted())
        return fail(req, "503 Service Unavailable", "SD 卡未挂载，请先在串口执行 sd mount");
    return ESP_OK;
}

static esp_err_t h_index(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, kHtml, sizeof(kHtml) - 1);
}

static esp_err_t h_upload(httpd_req_t* req) {
    esp_err_t m = need_mount(req);
    if (m != ESP_OK) return m;
    std::string raw, path;
    if (!get_query(req, "path", raw) || !resolve(raw, false, path))
        return fail(req, "400 Bad Request", "路径非法，只允许 /mem_fat 或 /sd_ext 下的文件");

    std::string cur;
    for (size_t i = 1; i < path.size(); i++) {
        if (path[i] != '/') continue;
        cur = path.substr(0, i);
        mkdir(cur.c_str(), 0777);   /* 已存在返回 EEXIST，忽略 */
    }

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return fail(req, "500 Internal Server Error", "无法创建文件（目录不存在？）");

    static char buf[4096];
    size_t total = 0;
    int idle = 0;
    for (;;) {
        int r = httpd_req_recv(req, buf, sizeof(buf));
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            if (++idle < 6) continue;
            r = -1;   /* 连续 60 秒无数据视为断流 */
        }
        if (r < 0) {
            std::fclose(f);
            remove(path.c_str());
            return fail(req, "500 Internal Server Error", "接收中断，已删除半截文件");
        }
        if (r == 0) break;
        idle = 0;
        if (std::fwrite(buf, 1, static_cast<size_t>(r), f) != static_cast<size_t>(r)) {
            std::fclose(f);
            remove(path.c_str());
            return fail(req, "500 Internal Server Error", "写入失败，已删除半截文件");
        }
        total += static_cast<size_t>(r);
    }
    if (req->content_len > 0 && total != req->content_len) {
        std::fclose(f);
        remove(path.c_str());
        return fail(req, "500 Internal Server Error", "长度不符，已删除半截文件");
    }
    if (std::fclose(f) != 0) {
        remove(path.c_str());
        return fail(req, "500 Internal Server Error", "关闭文件失败，已删除");
    }

    ESP_LOGI(TAG, "upload %s %lu B", path.c_str(), (unsigned long)total);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    std::string b = "{\"ok\":true,\"path\":\"";
    b += json_escape(path);
    b += "\",\"size\":" + std::to_string(static_cast<unsigned long>(total)) + "}";
    return httpd_resp_send(req, b.c_str(), b.size());
}

static esp_err_t h_ls(httpd_req_t* req) {
    esp_err_t m = need_mount(req);
    if (m != ESP_OK) return m;
    std::string raw = "/mem_fat", dir;
    if (get_query(req, "dir", raw) && raw.empty()) raw = "/mem_fat";
    if (!resolve(raw, true, dir))
        return fail(req, "400 Bad Request", "目录非法，只允许 /mem_fat 或 /sd_ext 之下");

    DIR* d = opendir(dir.c_str());
    if (!d) return fail(req, "404 Not Found", "目录不存在");

    std::string out = "[";
    size_t n = 0;
    while (struct dirent* e = readdir(d)) {
        if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, "..")) continue;
        std::string full = dir + "/" + e->d_name;
        struct stat st = {};
        if (stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
        if (n++) out += ',';
        out += "{\"name\":\"";
        out += json_escape(e->d_name);
        out += "\",\"size\":" + std::to_string(static_cast<unsigned long>(st.st_size)) + "}";
        if (out.size() > 12000) break;   /* 防止响应撑爆内存 */
    }
    closedir(d);
    out += "]";

    ESP_LOGI(TAG, "ls %s %u entries", dir.c_str(), (unsigned)n);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    return httpd_resp_send(req, out.c_str(), out.size());
}

static esp_err_t h_rm(httpd_req_t* req) {
    esp_err_t m = need_mount(req);
    if (m != ESP_OK) return m;
    std::string raw, path;
    if (!get_query(req, "path", raw) || !resolve(raw, false, path))
        return fail(req, "400 Bad Request", "路径非法");

    struct stat st = {};
    if (stat(path.c_str(), &st) != 0)
        return fail(req, "404 Not Found", "文件不存在");
    if (S_ISDIR(st.st_mode))
        return fail(req, "400 Bad Request", "只能删除文件");
    if (remove(path.c_str()) != 0)
        return fail(req, "500 Internal Server Error", "删除失败");

    ESP_LOGI(TAG, "rm %s", path.c_str());
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    std::string b = "{\"ok\":true,\"path\":\"" + json_escape(path) + "\"}";
    return httpd_resp_send(req, b.c_str(), b.size());
}

static esp_err_t h_format(httpd_req_t* req) {
    esp_err_t m = need_mount(req);
    if (m != ESP_OK) return m;
    ESP_LOGW(TAG, "format requested via http");
    esp_err_t e = drivers::SdCard::instance()->reformat();
    if (e != ESP_OK)
        return fail(req, "500 Internal Server Error", "格式化失败");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    std::string b = "{\"ok\":true,\"mounted\":true}";
    return httpd_resp_send(req, b.c_str(), b.size());
}

bool running() { return s_server != nullptr; }

esp_err_t start(uint16_t port) {
    if (s_server) return ESP_OK;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = port;
    cfg.max_uri_handlers = 8;
    cfg.stack_size = 8192;
    cfg.recv_wait_timeout = 10;
    cfg.send_wait_timeout = 10;
    cfg.lru_purge_enable = true;

    esp_err_t e = httpd_start(&s_server, &cfg);
    if (e != ESP_OK) { s_server = nullptr; return e; }

    httpd_uri_t uris[] = {
        { "/",       HTTP_GET,  h_index,  nullptr },
        { "/upload", HTTP_POST, h_upload, nullptr },
        { "/ls",     HTTP_GET,  h_ls,     nullptr },
        { "/rm",     HTTP_POST, h_rm,     nullptr },
        { "/format", HTTP_POST, h_format, nullptr },
    };
    for (auto& u : uris) {
        e = httpd_register_uri_handler(s_server, &u);
        if (e != ESP_OK) {
            httpd_stop(s_server);
            s_server = nullptr;
            return e;
        }
    }
    ESP_LOGI(TAG, "http server on port %u", (unsigned)port);
    return ESP_OK;
}

esp_err_t start_ap() {
    auto* w = drivers::WifiManager::instance();
    /* 只上电不开 STA：开热点时不需要同时连路由器，少一次 TX 峰值 */
    if (!w->rf_started()) {
        esp_err_t e = w->start_rf(false);
        if (e != ESP_OK) return e;
    }
    if (!esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"))
        esp_netif_create_default_wifi_ap();
    esp_err_t e = w->start_ap("MemoriaOS", "memoria123");
    if (e != ESP_OK) return e;
    return start(80);
}

esp_err_t start_sta() {
    auto* w = drivers::WifiManager::instance();
    esp_err_t e = w->start_rf();
    if (e != ESP_OK) return e;
    if (!w->connected()) {
        e = w->reconnect();
        if (e != ESP_OK && e != ESP_ERR_NOT_FOUND) return e;
    }
    return start(80);
}

void stop() {
    if (s_server) {
        httpd_stop(s_server);
        s_server = nullptr;
        ESP_LOGI(TAG, "http server stopped");
    }
    auto* w = drivers::WifiManager::instance();
    if (w->ap_active()) w->stop_ap();
}

} // namespace netfile
} // namespace memoria

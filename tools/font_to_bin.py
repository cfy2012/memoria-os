# -*- coding: utf-8 -*-
"""
font_to_bin.py — 把编译进固件的 GB2312 点阵数组抽出为 TF 卡裸文件。

输入:  components/memoria_window/gb2312_font.cpp  (点阵数组 + unicode 映射表)
输出:  sdcard/font/gb2312_16.bin                  (7445 字 x 32B 点阵，拷到 TF 卡根目录)
       components/memoria_window/gb2312_font.cpp  (重写：去掉点阵数组，加载逻辑入内)

用法:  python tools/font_to_bin.py
换字体时重新生成 cpp 数组后重跑本脚本即可。
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "components" / "memoria_window" / "gb2312_font.cpp"
BIN = ROOT / "sdcard" / "font" / "gb2312_16.bin"

BYTES_PER = 32


def main() -> int:
    text = SRC.read_text(encoding="utf-8")

    # --- 槽位数从数组声明动态抓取（8178：含空洞，见 gen_gb2312_font.py） ---
    m_decl = re.search(r"FONT_GB2312_16\[(\d+) \* 32\]", text)
    if not m_decl:
        print("FAIL: 数组声明未匹配"); return 1
    n_slots = int(m_decl.group(1))

    # --- 提取点阵数组段 ---
    m_arr = re.search(
        r"static const uint8_t FONT_GB2312_16\[\d+ \* 32\] = \{(.*?)\n\};",
        text, re.S)
    if not m_arr:
        print("FAIL: 点阵数组段未匹配"); return 1
    hexes = re.findall(r"0x([0-9A-Fa-f]{2})", m_arr.group(1))
    data = bytes(int(h, 16) for h in hexes)
    if len(data) != n_slots * BYTES_PER:
        print(f"FAIL: 点阵字节数 {len(data)} != {n_slots * BYTES_PER}"); return 1

    # --- 原文段落切分 ---
    m_map = re.search(r"(static const struct \{ uint32_t unicode; uint16_t gb_index; \} GB2312_MAP\[\d+\] = \{.*?\n\};)", text, re.S)
    m_fn = re.search(r"(int gb2312_index\(uint32_t unicode\) \{.*)", text, re.S)
    if not (m_map and m_fn):
        print("FAIL: 映射表或函数段未匹配"); return 1

    # --- 写 bin ---
    BIN.parent.mkdir(parents=True, exist_ok=True)
    BIN.write_bytes(data)
    print(f"OK  bin : {BIN}  ({len(data)} B)")

    # --- 重写 cpp ---
    new_cpp = f"""#include <cstdint>
#include <cstdio>
#include "gb2312_font.hpp"
#include "esp_heap_caps.h"

/* Auto-generated GB2312 16x16 font interface, {n_slots} slots (含空洞), from simhei.ttf
 * 点阵数据在 TF 卡 /font/gb2312_16.bin（由 tools/font_to_bin.py 生成），
 * 启动时经 gb2312_font_load() 读入 PSRAM；unicode 映射表仍编译在固件内。 */

static uint8_t* s_font_ram = nullptr;   /* PSRAM: {n_slots} x {BYTES_PER} B */

bool gb2312_font_load(const char* path) {{
    if (s_font_ram) return true;        /* 已加载，幂等 */
    if (!path) return false;
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    uint8_t* ram = static_cast<uint8_t*>(
        heap_caps_malloc({n_slots} * {BYTES_PER}, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!ram) {{ fclose(f); return false; }}
    size_t n = fread(ram, 1, {n_slots} * {BYTES_PER}, f);
    fclose(f);
    if (n != {n_slots} * {BYTES_PER}) {{ heap_caps_free(ram); return false; }}
    s_font_ram = ram;
    return true;
}}

bool gb2312_font_ready(void) {{
    return s_font_ram != nullptr;
}}

{m_map.group(1)}

{m_fn.group(1)}"""

    # 原函数段里 gb2312_index 原样保留；gb2312_glyph 改为 PSRAM 取数
    new_cpp = re.sub(
        r"const uint8_t\* gb2312_glyph\(int gb_index\) \{[^}]*\}",
        f"""const uint8_t* gb2312_glyph(int gb_index) {{
  if (gb_index < 0 || gb_index >= {n_slots} || !s_font_ram) return nullptr;
  return s_font_ram + gb_index * {BYTES_PER};
}}""",
        new_cpp)
    if "s_font_ram + gb_index" not in new_cpp:
        print("FAIL: gb2312_glyph 重写失败"); return 1

    SRC.write_text(new_cpp, encoding="utf-8")
    print(f"OK  cpp : {SRC}  (rewritten, glyph -> PSRAM)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

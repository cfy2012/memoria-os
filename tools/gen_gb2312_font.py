# -*- coding: utf-8 -*-
"""
gen_gb2312_font.py — 用 simhei.ttf 重造 GB2312 16x16 点阵（每字 32B，修复历史半量数据）。

背景:  原 gb2312_font.cpp 声明 7445*32B 但只初始化了 16B/字（下半补零，
       固件汉字渲染下半空白——模拟器用浏览器字体所以从未暴露）。
做法:  unicode 集合与 gb_index 编号完全沿用现有 GB2312_MAP（映射表不动，
       gb2312_index 不受影响），PIL 逐字渲染 16x16 二值位图，输出完整 cpp 数组。
       生成后请再跑 tools/font_to_bin.py 完成 TF 卡 bin 导出与固件 TF 化改写。

用法:  python tools/gen_gb2312_font.py
"""
import re
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "components" / "memoria_window" / "gb2312_font.cpp"
FONT_PATH = Path(r"C:\Windows\Fonts\simhei.ttf")

N_CHARS = 7445
CELL = 16


def load_map(text: str):
    """解析 GB2312_MAP：返回 [(unicode, gb_index), ...]"""
    m = re.search(
        r"GB2312_MAP\[\d+\] = \{(.*?)\n\};", text, re.S)
    if not m:
        print("FAIL: GB2312_MAP 段未匹配"); sys.exit(1)
    entries = re.findall(r"\{0x([0-9A-Fa-f]+),\s*(\d+)\}", m.group(1))
    if len(entries) != N_CHARS:
        print(f"FAIL: MAP 条目 {len(entries)} != {N_CHARS}"); sys.exit(1)
    return [(int(u, 16), int(i)) for u, i in entries]


def render_char(font, ch: str) -> bytes:
    """渲染单字为 16x16 位图，每行 2B（高位在左），共 32B。

    ImageDraw.text + anchor 居中直绘（getmask 的 ImagingCore 无法导出像素），
    超出画布部分自动裁剪；灰度阈值 100 二值化，保住黑体细笔画。
    """
    canvas = Image.new("L", (CELL, CELL), 0)
    d = ImageDraw.Draw(canvas)
    d.text((CELL // 2, CELL // 2), ch, font=font, fill=255, anchor="mm")
    px = canvas.load()
    out = bytearray(CELL * 2)
    for row in range(CELL):
        hi = lo = 0
        for col in range(CELL):
            if px[col, row] >= 100:
                if col < 8:
                    hi |= 0x80 >> col
                else:
                    lo |= 0x80 >> (col - 8)
        out[row * 2] = hi
        out[row * 2 + 1] = lo
    return bytes(out)


def main() -> int:
    text = SRC.read_text(encoding="utf-8")
    entries = load_map(text)
    font = ImageFont.truetype(str(FONT_PATH), CELL)

    # gb_index 空间: 0..max（含空洞，洞=无字全零槽）。历史上 idx 最大 8177，
    # 而旧数组只声明 7445 槽 -> 越界读；本脚本按真实 idx 上限重建。
    max_idx = max(i for _, i in entries)
    size = max_idx + 1
    u_by_idx = {i: u for u, i in entries}

    glyphs = {}
    empty = 0
    for unicode_cp, gb_index in entries:
        data = render_char(font, chr(unicode_cp))
        if not any(data):
            empty += 1
        glyphs[gb_index] = data

    lines = []
    for idx in range(size):
        u = u_by_idx.get(idx)
        d = glyphs.get(idx, b"\x00" * 32)
        tag = f"U+{u:04X} idx={idx}" if u is not None else f"UNUSED idx={idx}"
        lines.append(
            f"  /* {tag} */ "
            + ", ".join(f"0x{b:02X}" for b in d) + ",")
    body = "\n".join(lines)

    print(f"OK  rendered {len(entries)} chars into {size} slots, blank glyphs: {empty}")

    # 抽查“永”(U+6C38) 的 ASCII art，肉眼校验
    yong = glyphs[next(i for u, i in entries if chr(u) == "永")]
    for r in range(CELL):
        hi, lo = yong[r * 2], yong[r * 2 + 1]
        print("".join("#" if (hi if c < 8 else lo) & (0x80 >> (c % 8)) else "."
                      for c in range(CELL)))

    # 组装新 cpp（完整 32B/字版本），MAP 段与函数段原样保留
    m_map = re.search(r"(static const struct \{ uint32_t unicode; uint16_t gb_index; \} GB2312_MAP\[\d+\] = \{.*?\n\};)", text, re.S)
    m_fn = re.search(r"(int gb2312_index\(uint32_t unicode\) \{.*)", text, re.S)
    fn = re.sub(r"gb_index >= \d+", f"gb_index >= {size}", m_fn.group(1))
    new_cpp = (
        "#include <cstdint>\n"
        "/* Auto-generated GB2312 16x16 font, from simhei.ttf\n"
        f" * 由 tools/gen_gb2312_font.py 重造：{size} 槽（含空洞）x 32B 完整点阵。\n"
        " * 修复历史三重缺陷：16B 半量数据 / MAP 顺序错位 / idx 越界读 */\n"
        f"#define FONT_GB2312_CHARS {size}\n"
        f"static const uint8_t FONT_GB2312_16[{size} * 32] = {{\n"
        + body + "\n};\n\n" + m_map.group(1) + "\n\n" + fn)
    SRC.write_text(new_cpp, encoding="utf-8")
    print(f"OK  cpp : {SRC}  ({size} slots x 32B full data)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

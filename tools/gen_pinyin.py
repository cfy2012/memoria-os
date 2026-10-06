# -*- coding: utf-8 -*-
"""
@file gen_pinyin.py
@brief 生成掌机输入法拼音码表 sdcard/ime/pinyin.bin
@note  数据源：GB2312 一二级全部汉字（6763 字），pypinyin 取全部读音（无声调）
      排序：一级字库（常用，区 16-55）在前，二级在后，各按区位码升序——固件顺序扫描时常用字优先出候选
格式 PYIME1：
  头:   "PYIME1\\0"(8B) + 条目数 u32LE(4B)
  条目: 汉字UTF-8(3B) + 读音数n(1B) + n × [读音长度(1B) + 拼音字母]
"""
import io, os, sys
from pypinyin import pinyin, Style

DST = r"I:\Memoria OS\memoria-os\sdcard\ime\pinyin.bin"

def gb2312_chars():
    """遍历 GB2312 区位 16-87 全部汉字，返回 [(区, 位, 字)]"""
    out = []
    for qu in range(16, 88):
        for wei in range(1, 95):
            code = bytes([qu + 0xA0, wei + 0xA0])
            try:
                ch = code.decode("gb2312")
                out.append((qu, wei, ch))
            except UnicodeDecodeError:
                continue
    return out

rows = gb2312_chars()
# 一级在前（qu 16-55），二级在后（qu 56-87），区内已按区位升序
rows.sort(key=lambda r: (0 if r[0] <= 55 else 1, r[0], r[1]))

body = io.BytesIO()
cnt = 0
for qu, wei, ch in rows:
    u8 = ch.encode("utf-8")
    assert len(u8) == 3, ch
    pys = pinyin(ch, style=Style.NORMAL, heteronym=True, errors="ignore")[0]
    pys = sorted(set(p for p in pys if p and p.isascii()))
    if not pys:
        continue
    body.write(u8)
    body.write(bytes([len(pys)]))
    for p in pys:
        pb = p.encode("ascii")
        body.write(bytes([len(pb)]))
        body.write(pb)
    cnt += 1

data = b"PYIME1\x00" + cnt.to_bytes(4, "little") + body.getvalue()
os.makedirs(os.path.dirname(DST), exist_ok=True)
io.open(DST, "wb").write(data)

print("条目:", cnt, "文件:", len(data), "bytes")
# 抽查
for probe in ["的", "中", "行", "长", "阿", "鼾"]:
    pys = pinyin(probe, style=Style.NORMAL, heteronym=True, errors="ignore")[0]
    print(probe, "->", sorted(set(pys)))

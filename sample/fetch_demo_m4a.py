# -*- coding: utf-8 -*-
# 下载公开测试 m4a（本机无 ffmpeg，抓稳定源的 AAC-in-MP4 样例供真机实测）
import urllib.request, os, sys

DEST = os.path.join(os.path.dirname(os.path.abspath(__file__)), "sample_test.m4a")
CANDIDATES = [
    ("https://samplelib.com/lib/preview/m4a/sample-3s.m4a", "samplelib 3s"),
    ("https://file-examples.com/wp-content/uploads/2017/11/file_example_M4A_128KB.mp4", "file-examples 128KB"),
]

def fetch(url, name):
    print("trying %s -> %s" % (name, url))
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=30) as r:
        data = r.read()
    if len(data) < 4096:
        raise ValueError("too small: %d bytes" % len(data))
    return data

last = None
for url, name in CANDIDATES:
    try:
        data = fetch(url, name)
    except Exception as e:
        last = e
        print("  fail: %r" % e)
        continue
    with open(DEST, "wb") as f:
        f.write(data)
    print("OK -> %s (%d bytes)" % (DEST, len(data)))
    # 校验前 12 字节是 ftyp box（MP4/M4A 容器特征）
    if data[:4] == b"\x00\x00\x00\x18" or data[4:8] == b"ftyp":
        print("ftyp check: PASS (MP4 container)")
    else:
        print("ftyp check: head=%s (may still be valid)" % data[:16].hex())
    sys.exit(0)

print("ALL CANDIDATES FAILED: %r" % (last,))
sys.exit(1)

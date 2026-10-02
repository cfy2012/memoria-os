# -*- coding: utf-8 -*-
"""尝试下载公共 MP3/M4A 测试样本（jsdelivr 镜像），失败则构造静音 MP3 兜底"""
import io, os, sys, urllib.request, struct

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "examples", "audio-samples")

CANDIDATES = [
    # jsdelivr GitHub 镜像（GLM 实测可达）
    ("test.mp3", "https://cdn.jsdelivr.net/gh/samirkumardas/mp3@master/audio/test.mp3"),
    ("test.mp3", "https://cdn.jsdelivr.net/gh/agalwood/Motrix@master/test/test.mp3"),
    ("test.m4a", "https://cdn.jsdelivr.net/gh/matvp91/audio@master/test.m4a"),
]


def try_download(fname, url):
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=10) as r:
            data = r.read()
        if len(data) < 1024:
            return False
        with open(os.path.join(OUT, fname), "wb") as f:
            f.write(data)
        print("DL OK:", fname, len(data), "B <-", url)
        return True
    except Exception as e:
        print("DL FAIL:", fname, "-", type(e).__name__)
        return False


def make_silent_mp3(path, secs=5, rate=44100, kbps=128):
    """构造合法静音 MP3（MPEG1 Layer3, 128kbps 44100Hz），用于播放链路验证
    帧头位布局（32bit MSB）：sync[31:21]=0xFFE | version[20:19]=11(MPEG1) |
    layer[18:17]=01(Layer3) | prot[16]=0 | bitrate[15:12]=9(128k) |
    sr[11:10]=0(44.1k) | pad[9]=0 | priv[8]=0 | mode[7:6]=3(mono) | rest=0"""
    hdr = 0xFFE00000 | (3 << 19) | (1 << 17) | (0 << 16) | (9 << 12) | (0 << 10) | (0 << 9) | (0 << 8) | (3 << 6)
    frame_size = 144 * kbps * 1000 // rate  # MPEG1 Layer3 无 padding
    frame = struct.pack(">I", hdr) + bytes(frame_size - 4)
    frames = frame * (secs * rate // frame_size)
    with open(path, "wb") as f:
        f.write(frames)
    print("SILENT MP3:", path, len(frames), "B (%.1fs @%dkbps)" % (secs, kbps))


if __name__ == "__main__":
    got_mp3 = any(try_download(*c) for c in CANDIDATES[:2])
    got_m4a = try_download(*CANDIDATES[2])
    if not got_mp3:
        make_silent_mp3(os.path.join(OUT, "test.mp3"))
    if not got_m4a:
        print("M4A: 无编码器且下载失败——上板时请自备 AAC-LC 的 test.m4a（TEST-PLAN 步骤 0 已注明）")

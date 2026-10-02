# -*- coding: utf-8 -*-
"""
gen_audio_samples.py — 音频陪测线测试资源生成（豆包-2）
产出（examples/audio-samples/）：
  1. test.wav          16bit PCM 44100Hz mono 正弦波 3s（可辨听：440Hz A + 880Hz 双音）
  2. audio-test.msp    按 .msp 格式（MSPACK + ver + name[32] + size + crc32）打包 test.wav，
                       即 GLM §1.1 定稿的 type:"media" 资源包载体
  3. manifest.json     商店清单示例（核心集 {name,version,size,crc,url} + type:"media" + schema:1）

用法：python gen_audio_samples.py
依赖：python 标准库（wave/struct/zlib/math）
"""
import io, math, os, struct, wave, zlib

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "examples", "audio-samples")
os.makedirs(OUT, exist_ok=True)

SR = 44100
DUR = 3.0
N = int(SR * DUR)


def gen_wav(path, rate=SR, dur=DUR, vol=0.5):
    """双音正弦（440Hz + 880Hz），首尾 20ms 渐入渐出防爆音"""
    n = int(rate * dur)
    fade = int(rate * 0.02)
    frames = bytearray()
    for i in range(n):
        t = i / rate
        env = 1.0
        if i < fade:
            env = i / fade
        elif i > n - fade:
            env = (n - i) / fade
        s = vol * env * (math.sin(2 * math.pi * 440 * t) * 0.5 +
                         math.sin(2 * math.pi * 880 * t) * 0.5)
        frames += struct.pack("<h", int(s * 32767))
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(bytes(frames))
    print("WAV:", path, "%.1fs" % dur)


def make_msp(name, version, payload, out_path):
    """与固件 package_format.cpp 完全一致：50B header + payload"""
    if len(name) >= 32:
        raise ValueError("name too long")
    h = struct.pack("<6sI32sII", b"MSPACK", version,
                    name.encode("utf-8").ljust(32, b"\x00"),
                    len(payload), zlib.crc32(payload) & 0xFFFFFFFF)
    with open(out_path, "wb") as f:
        f.write(h)
        f.write(payload)
    print("MSP:", out_path, "name=%s ver=%d payload=%dB crc=0x%08X" %
          (name, version, len(payload), zlib.crc32(payload) & 0xFFFFFFFF))


def main():
    wav_path = os.path.join(OUT, "test.wav")
    gen_wav(wav_path)
    payload = open(wav_path, "rb").read()
    msp_path = os.path.join(OUT, "audio-test.msp")
    make_msp("audio-test", 1, payload, msp_path)

    # manifest 示例：GLM §1.1 核心集 + type:"media" + schema:1
    # name 带扩展名（audio-test.wav）：media 落盘名=name，MUSIC 枚举只认 .wav/.mp3/.m4a，无扩展名会被安装分支拒绝
    # url 为占位：上架时替换为实际部署源地址（jsdelivr 镜像：https://cdn.jsdelivr.net/gh/cfy2012/memoria-ota@main/...）
    manifest = [{
        "name": "audio-test.wav",
        "version": 1,
        "size": len(payload),
        "crc": zlib.crc32(payload) & 0xFFFFFFFF,
        "url": "https://cdn.jsdelivr.net/gh/cfy2012/memoria-ota@main/packages/audio-test.msp",
        "type": "media",
        "schema": 1,
    }]
    mpath = os.path.join(OUT, "manifest.json")
    with io.open(mpath, "w", encoding="utf-8") as f:
        f.write("[\n" + ",\n".join(
            "  {\n" + ",\n".join('    "%s": %s' % (k, json_repr(v)) for k, v in item.items()) +
            "\n  }" for item in manifest) + "\n]\n")
    print("MANIFEST:", mpath)


def json_repr(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, int):
        return str(v)
    return '"%s"' % v


if __name__ == "__main__":
    main()

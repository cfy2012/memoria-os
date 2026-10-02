# -*- coding: utf-8 -*-
"""联测：media 资源包全链路（Python 镜像，模拟固件 package_manager 行为）
链路：manifest 解析(type=media/name/ver/crc) -> 下载 .msp -> verify_package(MSPACK+size+crc)
     -> 双重 crc 比对 -> payload 落盘 /mem_fat/audio/<name> -> MUSIC 扩展名白名单检查
"""
import io, os, struct, zlib, json

BASE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "examples", "audio-samples")
EXT_WHITELIST = (".wav", ".mp3", ".m4a")
PAYLOAD_LIMIT = 1024 * 1024
INSTALL_DIR = "/mem_fat/audio"

# --- 1. manifest 解析（模拟 parse_manifest）---
mf = json.load(io.open(os.path.join(BASE, "manifest.json"), encoding="utf-8"))
e = mf[0]
name = e["name"]; version = e["version"]; m_size = e["size"]; m_crc = e["crc"]
mtype = e.get("type", "app"); schema = e.get("schema")
print("[1] manifest parse: name=%s type=%s ver=%s schema=%s" % (name, mtype, version, schema))
assert mtype == "media", "FAIL: type not media"
assert isinstance(version, int) or str(version).isdigit(), "FAIL: version not int/digit"

# --- 2. 下载（本地模拟）并 verify_package ---
msp = io.open(os.path.join(BASE, "audio-test.msp"), "rb").read()
magic, p_ver, p_name, p_size, p_crc = struct.unpack("<6sI32sII", msp[:50])
p_name = p_name.split(b"\x00")[0].decode()
payload = msp[50:]
print("[2] verify_package: magic=%s name=%s ver=%d payload=%dB crc=0x%08X" %
      (magic.decode(), p_name, p_ver, p_size, p_crc))
assert magic == b"MSPACK", "FAIL: bad magic"
assert p_size == len(payload) and p_size <= PAYLOAD_LIMIT, "FAIL: size check"
assert (zlib.crc32(payload) & 0xFFFFFFFF) == p_crc, "FAIL: package crc"
print("    package crc OK, payload <=1MB OK")

# --- 3. 双重 crc 比对（包内 crc vs manifest crc）---
print("[3] double crc: package=0x%08X manifest=0x%08X %s" %
      (p_crc, m_crc, "MATCH" if p_crc == m_crc else "MISMATCH"))
assert p_crc == m_crc and m_size == len(payload), "FAIL: manifest/package mismatch"

# --- 4. 落盘名 + MUSIC 扩展名白名单 ---
target = os.path.join(INSTALL_DIR, name)
ext = os.path.splitext(name)[1].lower()
print("[4] install target=%s ext=%s whitelist=%s" % (target, ext, ext in EXT_WHITELIST))
assert ext in EXT_WHITELIST, "FAIL: ext not in whitelist"

# --- 5. payload 内容确认（WAV RIFF 头）---
assert payload[:4] == b"RIFF" and payload[8:12] == b"WAVE", "FAIL: not WAV payload"
print("[5] payload is WAV (RIFF/WAVE) OK, %dB -> %s" % (len(payload), target))

print("\n=== MEDIA LINK TEST: ALL PASS ===")
print("安装后 MUSIC 应枚举到: %s（440+880Hz 双音正弦，播放出声即全链路 OK）" % name)

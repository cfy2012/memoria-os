# -*- coding: utf-8 -*-
"""联测：商店 app 脚本包全链路（Python 镜像，模拟固件 package_manager 行为）
链路：manifest 解析(type=app/name/ver/crc) -> 下载 .msp -> verify_package(MSPACK+size+crc)
     -> 双重 crc 比对 -> payload 落盘 /mem_fat/scripts/<name> -> 包名白名单 -> BASIC 可运行性
"""
import io, os, posixpath, struct, zlib, json

BASE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "examples", "app-samples")
NAME_WHITELIST_RE = None  # 简化：直接字符检查（模拟固件 isalnum/_/-/.）
PAYLOAD_LIMIT = 1024 * 1024
INSTALL_DIR = "/mem_fat/scripts"

os.makedirs(BASE, exist_ok=True)

# --- 0. 造一个 demo 脚本包 .msp（payload = BASIC 程序文本）---
SCRIPT = b'10 PRINT "MEMORIA OS"\r\n20 PRINT "STORE LINK OK"\r\n30 END\r\n'
demo_path = os.path.join(BASE, "demo-script.msp")
hdr = struct.pack("<6sI32sII", b"MSPACK", 1, b"demo-script.msp\x00".ljust(32, b"\x00"),
                  len(SCRIPT), zlib.crc32(SCRIPT) & 0xFFFFFFFF)
io.open(demo_path, "wb").write(hdr + SCRIPT)
print("[0] demo .msp created: %s (%dB payload)" % (demo_path, len(SCRIPT)))

# manifest（app 类型，无 type 字段时缺省 app；url 指向本地相对路径）
manifest = [{
    "name": "demo-script.msp",
    "version": 1,
    "size": len(SCRIPT),
    "crc": zlib.crc32(SCRIPT) & 0xFFFFFFFF,
    "url": "./demo-script.msp",
}]
mf_path = os.path.join(BASE, "manifest.json")
io.open(mf_path, "w", encoding="utf-8").write(json.dumps(manifest, indent=2, ensure_ascii=False))
print("[0] app manifest written: %s" % mf_path)

# --- 1. manifest 解析（模拟 parse_manifest：type 缺省 app / version 数字兜底）---
e = manifest[0]
name = e["name"]; version = e["version"]; m_size = e["size"]; m_crc = e["crc"]
mtype = e.get("type", "app")
print("[1] manifest parse: name=%s type=%s ver=%s" % (name, mtype, version))
assert mtype == "app", "FAIL: type should default app"
assert isinstance(version, int) or str(version).isdigit(), "FAIL: version not int/digit"

# --- 2. 包名白名单（[A-Za-z0-9_.-]，防路径穿越）---
name_ok = name and name not in (".", "..")
for ch in name:
    if not (ch.isalnum() or ch in "_ .-"):
        name_ok = False
        break
print("[2] name whitelist: %s ok=%s" % (name, name_ok))
assert name_ok, "FAIL: illegal package name"

# --- 3. 下载（本地模拟）并 verify_package ---
msp = io.open(demo_path, "rb").read()
magic, p_ver, p_name, p_size, p_crc = struct.unpack("<6sI32sII", msp[:50])
p_name = p_name.split(b"\x00")[0].decode()
payload = msp[50:]
print("[3] verify_package: magic=%s name=%s ver=%d payload=%dB crc=0x%08X" %
      (magic.decode(), p_name, p_ver, p_size, p_crc))
assert magic == b"MSPACK", "FAIL: bad magic"
assert p_size == len(payload) and p_size <= PAYLOAD_LIMIT, "FAIL: size check"
assert (zlib.crc32(payload) & 0xFFFFFFFF) == p_crc, "FAIL: package crc"
print("    package crc OK, payload <=1MB OK")

# --- 4. 双重 crc 比对（包内 crc vs manifest crc）---
print("[4] double crc: package=0x%08X manifest=0x%08X %s" %
      (p_crc, m_crc, "MATCH" if p_crc == m_crc else "MISMATCH"))
assert p_crc == m_crc and m_size == len(payload), "FAIL: manifest/package mismatch"

# --- 5. 落盘路径（/mem_fat/scripts/<name>；固件侧为 POSIX，镜像用 posixpath 避免 Windows 反斜杠）---
target = posixpath.join(INSTALL_DIR, name)
print("[5] install target=%s payload=%dB" % (target, len(payload)))
assert target.startswith(INSTALL_DIR + "/"), "FAIL: install path escape"

# --- 6. payload 为 BASIC 程序（可被解释器解析的文本）---
text = payload.decode("ascii", errors="replace")
has_print = "PRINT" in text.upper()
print("[6] payload as BASIC: %r printable=%s has PRINT=%s" % (text[:40], all(c < 128 for c in payload), has_print))
assert has_print, "FAIL: payload not a BASIC program"

print("\n=== STORE LINK TEST: ALL PASS ===")
print("安装后 PRGM/BASIC 应能列出: %s，运行输出 MEMORIA OS / STORE LINK OK 即全链路 OK" % name)

import json
import os
import re


def _cmake_ver(root):
    p = os.path.join(root, "CMakeLists.txt")
    t = open(p, encoding="utf-8").read()
    m = re.search(r'set\s*\(\s*PROJECT_VER\s+"([^"]+)"\s*\)', t)
    return m.group(1) if m else None


def _header_ver(root):
    p = os.path.join(root, "components", "memoria_drivers", "include", "system_info.h")
    t = open(p, encoding="utf-8").read()
    m = re.search(r'#define\s+MEMORIA_OS_VERSION\s+"([^"]+)"', t)
    return m.group(1) if m else None


def _json_ver(root):
    p = os.path.join(root, "firmware", "version.json")
    return json.load(open(p, encoding="utf-8")).get("version")


def run(root):
    errs = []
    a, b, c = _cmake_ver(root), _header_ver(root), _json_ver(root)
    if not a:
        errs.append("CMakeLists.txt 里找不到 PROJECT_VER")
    if not b:
        errs.append("system_info.h 里找不到 MEMORIA_OS_VERSION")
    if not c:
        errs.append("version.json 缺少 version 字段")
    if len({a, b, c} - {None}) > 1:
        errs.append("版本三处不一致：CMake=%s header=%s json=%s" % (a, b, c))

    if not c:
        return errs

    m = re.match(r"^(\d+)\.(\d+)\.(\d+)$", c)
    if not m:
        errs.append("version 格式非 X.Y.Z：%s" % c)
        return errs

    minor = int(m.group(2))
    want = "stable" if minor % 2 == 0 else "beta"

    p = os.path.join(root, "firmware", "version.json")
    d = json.load(open(p, encoding="utf-8"))
    ch = d.get("channel")
    if ch is None:
        errs.append("version.json 缺 channel 字段（按奇偶规则应为 %s）" % want)
    elif ch != want:
        errs.append("channel=%s 与版本号奇偶不符（%s 位应为 %s）" % (ch, c, want))

    return errs

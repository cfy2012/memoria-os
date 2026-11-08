import json
import os
import posixpath


def run(root):
    errs = []
    p = os.path.join(root, "firmware", "version.json")
    if not os.path.exists(p):
        return ["firmware/version.json 不存在"]
    d = json.load(open(p, encoding="utf-8"))

    for k in ("version", "url", "changelog", "major", "channel"):
        if k not in d:
            errs.append("version.json 缺字段 %s" % k)

    ver = d.get("version")
    url = d.get("url", "")
    if ver and url:
        base = posixpath.basename(url.split("?")[0])
        if base != "memoria_os_%s.bin" % ver:
            errs.append("url 指向 %s，与 version %s 不符" % (base, ver))

    major = d.get("major")
    if not isinstance(major, str):
        errs.append("major 类型为 %s，必须是字符串 \"true\"/\"false\"" % type(major).__name__)
    elif major not in ("true", "false"):
        errs.append("major 值 %r 非法，只接受 \"true\" / \"false\"" % major)

    if not str(d.get("changelog", "")).strip():
        errs.append("changelog 为空")

    if ver:
        b = os.path.join(root, "firmware", "memoria_os_%s.bin" % ver)
        if not os.path.exists(b):
            errs.append("firmware/memoria_os_%s.bin 不存在" % ver)
        elif os.path.getsize(b) < 0x10000:
            errs.append("固件只有 %d B，异常" % os.path.getsize(b))
    return errs

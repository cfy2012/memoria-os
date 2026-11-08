import hashlib
import json
import os
import posixpath


def _sha256(path, size):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            blk = f.read(size)
            if not blk:
                break
            h.update(blk)
    return h.hexdigest()


def run(root):
    errs = []
    p = os.path.join(root, "firmware", "version.json")
    if not os.path.exists(p):
        return ["firmware/version.json 不存在"]
    d = json.load(open(p, encoding="utf-8"))

    for k in ("version", "url", "sha256", "changelog", "major", "channel"):
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

    sha = d.get("sha256")
    if sha is not None:
        if not isinstance(sha, str) or len(sha) != 64 or any(c not in "0123456789abcdefABCDEF" for c in sha):
            errs.append("sha256 必须是 64 位十六进制，当前 %r" % (sha,))

    if ver:
        b = os.path.join(root, "firmware", "memoria_os_%s.bin" % ver)
        if not os.path.exists(b):
            errs.append("firmware/memoria_os_%s.bin 不存在" % ver)
        else:
            if os.path.getsize(b) < 0x10000:
                errs.append("固件只有 %d B，异常" % os.path.getsize(b))
            if isinstance(sha, str) and len(sha) == 64:
                real = _sha256(b, 1 << 20)
                if real.lower() != sha.lower():
                    errs.append("sha256 与 bin 不符：清单 %s…，实际 %s…" % (sha[:16], real[:16]))
    return errs

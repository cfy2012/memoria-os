import os
import re

HEADER_OWNER = {
    "esp_ota_ops.h": "app_update",
    "esp_http_client.h": "esp_http_client",
    "nvs_flash.h": "nvs_flash",
}

INC_RE = re.compile(r'#\s*include\s*<([^>]+)>')


def _sources(comp_dir):
    out = []
    for cur, _, files in os.walk(comp_dir):
        for f in files:
            if f.endswith((".c", ".cpp", ".h", ".hpp")):
                out.append(os.path.join(cur, f))
    return out


def run(root):
    errs = []
    base = os.path.join(root, "components")
    for name in sorted(os.listdir(base)):
        cdir = os.path.join(base, name)
        cm = os.path.join(cdir, "CMakeLists.txt")
        if not os.path.isdir(cdir) or not os.path.exists(cm):
            continue
        decl = open(cm, encoding="utf-8").read().lower()
        used = {}
        for src in _sources(cdir):
            try:
                t = open(src, encoding="utf-8", errors="ignore").read()
            except OSError:
                continue
            for h in INC_RE.findall(t):
                owner = HEADER_OWNER.get(h)
                if owner and owner not in decl:
                    used.setdefault(owner, []).append(os.path.relpath(src, root))
        for owner, files in used.items():
            errs.append("%s 用了 %s 但 CMakeLists 没声明依赖（%s）"
                        % (name, owner, ", ".join(files[:3])))
    return errs

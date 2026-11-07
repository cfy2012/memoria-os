import json, os, io, sys

PATCH_DIR = r"G:\Memoria OS\docs\recovery\patches"
ROOT = r"G:\Memoria OS"
MAP = {
    "window_hpp":      r"components\memoria_window\include\window.hpp",
    "window_cpp":      r"components\memoria_window\window.cpp",
    "basic_host_hpp":  r"components\memoria_kernel\modes\basic_host.hpp",
    "basic_host_cpp":  r"components\memoria_kernel\modes\basic_host.cpp",
    "basic_cpp":       r"tools\pc\basic.cpp",
}
ORDER = ["window_hpp", "window_cpp", "basic_host_hpp", "basic_host_cpp", "basic_cpp"]

def parse_edits(path):
    txt = io.open(path, encoding="utf-8").read()
    chunks = txt.split("--- ARGS_JSON ---")
    out = []
    for c in chunks[1:]:
        cut = c.find("====")
        body = c if cut < 0 else c[:cut]
        s = body.find("{")
        if s < 0:
            continue
        obj, _ = json.JSONDecoder().raw_decode(body[s:])
        out.append(obj)
    return out

def norm(s):
    return s.replace("\r\n", "\n")

summary = []
dry = "--apply" not in sys.argv
for key in ORDER:
    pf = os.path.join(PATCH_DIR, key + ".edits.txt")
    tf = os.path.join(ROOT, MAP[key])
    if not os.path.exists(tf):
        summary.append("%-18s 目标文件不存在，跳过（%d 处无法应用）" % (key, len(parse_edits(pf))))
        continue
    edits = parse_edits(pf)
    content = norm(io.open(tf, encoding="utf-8", errors="strict").read())
    ok = 0
    fail = None
    for i, e in enumerate(edits, 1):
        old = norm(e.get("old_string", ""))
        new = norm(e.get("new_string", ""))
        cnt = content.count(old)
        if cnt == 1:
            content = content.replace(old, new, 1)
            ok += 1
        elif cnt == 0 and content.count(new) >= 1:
            ok += 1
        else:
            fail = "第 %d/%d 处 old_string 匹配 %d 次" % (i, len(edits), cnt)
            break
    summary.append("%-18s %2d 处，可应用 %2d，%s" % (key, len(edits), ok, fail or "全部匹配"))
    if not dry and fail is None:
        with io.open(tf, "w", encoding="utf-8", newline="\r\n") as f:
            f.write(content)
        summary.append("                已写回 %s" % MAP[key])

print("\n".join(summary))

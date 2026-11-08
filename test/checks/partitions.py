import os
import re

FLASH_DEFAULT = 0x1000000
ALIGN = 0x1000
APP_START = 0x10000
PT_OFFSET = 0x8000


def _flash_size(root):
    p = os.path.join(root, "sdkconfig.defaults")
    t = open(p, encoding="utf-8").read()
    for m in re.finditer(r"CONFIG_ESPTOOLPY_FLASHSIZE_(\d+)MB=y", t):
        return int(m.group(1)) * 0x100000
    return FLASH_DEFAULT


def _rows(root):
    p = os.path.join(root, "partitions.csv")
    out = []
    for ln, line in enumerate(open(p, encoding="utf-8"), 1):
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        f = [x.strip() for x in s.split(",")]
        out.append((ln, f))
    return out


def run(root):
    errs = []
    rows = _rows(root)
    if not rows:
        return ["partitions.csv 没有有效行"]

    parts = []
    for ln, f in rows:
        if len(f) != 5:
            errs.append("第 %d 行字段数 %d，应为 5" % (ln, len(f)))
            continue
        name, ptype, subtype, off_s, size_s = f
        try:
            off = int(off_s, 16)
            size = int(size_s, 16)
        except ValueError:
            errs.append("第 %d 行偏移/大小不是十六进制: %s %s" % (ln, off_s, size_s))
            continue
        parts.append(dict(line=ln, name=name, type=ptype, subtype=subtype,
                          off=off, size=size, end=off + size))

    if any(p["off"] % ALIGN for p in parts):
        errs.append("存在未按 0x1000 对齐的分区")

    prev_end = PT_OFFSET + ALIGN
    for p in parts:
        if p["off"] < prev_end:
            errs.append("%s 起点 0x%X 与前一分区结尾 0x%X 重叠" % (p["name"], p["off"], prev_end))
        prev_end = max(prev_end, p["end"])

    flash = _flash_size(root)
    for p in parts:
        if p["end"] > flash:
            errs.append("%s 结束 0x%X 超出 flash 0x%X" % (p["name"], p["end"], flash))

    names = {p["name"]: p for p in parts}
    ota_data = [p for p in parts if p["type"] == "data" and p["subtype"] == "ota"]
    if not ota_data:
        errs.append("缺少 otadata（type=data subtype=ota），A/B 切换无从记录")
    elif ota_data[0]["size"] < 0x2000:
        errs.append("otadata 大小 0x%X 小于 0x2000" % ota_data[0]["size"])

    apps = [p for p in parts if p["type"] == "app"]
    has_factory = any(p["subtype"] == "factory" for p in apps)
    ota_slots = [p for p in apps if p["subtype"].startswith("ota_")]
    ota0 = [p for p in apps if p["subtype"] == "ota_0"]

    if not has_factory and len(ota_slots) != 2:
        errs.append("无 factory 时必须 ota_0 + ota_1 两槽，实际 %d 槽" % len(ota_slots))
    if has_factory and not ota0:
        errs.append("有 factory 但没有 ota_0")
    if len(ota_slots) == 2 and ota_slots[0]["size"] != ota_slots[1]["size"]:
        errs.append("ota_0/ota_1 大小不等：0x%X vs 0x%X" % (ota_slots[0]["size"], ota_slots[1]["size"]))

    boot_app = ota0[0] if ota0 else None
    if boot_app is None:
        errs.append("没有可启动的 app 槽（ota_0）")
    elif boot_app["off"] < APP_START:
        errs.append("ota_0 偏移 0x%X 落在分区表/引导区，最低应为 0x%X" % (boot_app["off"], APP_START))

    if not any(p["type"] == "data" and p["subtype"] == "nvs" for p in parts):
        errs.append("缺少 nvs 分区")

    bin_size = _bin_size(root)
    if bin_size and boot_app:
        if bin_size > boot_app["size"]:
            errs.append("固件 0x%X 超出 ota_0 槽 0x%X" % (bin_size, boot_app["size"]))
        used = bin_size * 100.0 / boot_app["size"]
        if used > 85.0:
            errs.append("槽占用 %.1f%%，超过 85%% 预警线" % used)
    return errs


def _bin_size(root):
    import json
    vj = os.path.join(root, "firmware", "version.json")
    if not os.path.exists(vj):
        return 0
    ver = json.load(open(vj, encoding="utf-8")).get("version", "")
    b = os.path.join(root, "firmware", "memoria_os_%s.bin" % ver)
    return os.path.getsize(b) if os.path.exists(b) else 0

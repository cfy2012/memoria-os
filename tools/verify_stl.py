# -*- coding: utf-8 -*-
"""STL 几何验证 v2：包围盒 + xy 投影点测试（点被任一三角覆盖 = 板内）"""
import struct, os

OUT = r"I:\Memoria OS\memoria-os\design\3D模型\外壳STL"

def read_stl(path):
    with open(path, "rb") as f:
        f.read(80)
        n = struct.unpack("<I", f.read(4))[0]
        tris = []
        for _ in range(n):
            data = struct.unpack("<12f", f.read(48))
            f.read(2)
            tris.append((data[3:6], data[6:9], data[9:12]))
        return tris

def bbox(tris):
    xs = [v[0] for t in tris for v in t]
    ys = [v[1] for t in tris for v in t]
    zs = [v[2] for t in tris for v in t]
    return (min(xs), max(xs), min(ys), max(ys), min(zs), max(zs))

def in_tri2d(px, py, a, b, c):
    d1 = (px - b[0]) * (a[1] - b[1]) - (a[0] - b[0]) * (py - b[1])
    d2 = (px - c[0]) * (b[1] - c[1]) - (b[0] - c[0]) * (py - c[1])
    d3 = (px - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (py - a[1])
    neg = d1 < 0 or d2 < 0 or d3 < 0
    pos = d1 > 0 or d2 > 0 or d3 > 0
    return not (neg and pos)

def point_in_mesh(tris, x, y, z):
    """z 射线法：从 (x,y,z) 向下发射，交叉数奇数 = 网格内部（STL 为封闭实体）。"""
    cnt = 0
    for (a, b, c) in tris:
        ax, ay = a[0], a[1]; bx, by = b[0], b[1]; cx, cy = c[0], c[1]
        denom = (bx-ax)*(cy-ay) - (by-ay)*(cx-ax)
        if abs(denom) < 1e-12:
            continue
        u = ((x-ax)*(cy-ay) - (y-ay)*(cx-ax)) / denom
        v = ((bx-ax)*(y-ay) - (by-ay)*(x-ax)) / denom
        if u < -1e-9 or v < -1e-9 or u + v > 1 + 1e-9:
            continue
        zp = a[2] + u*(b[2]-a[2]) + v*(c[2]-a[2])
        if zp < z - 1e-9:
            cnt += 1
    return (cnt % 2) == 1

checks = {
    "顶盖.stl": (1.25, [
        ((5, 5), True),          # 角上板内
        ((53.5, 32.5), False),   # 屏幕窗内
        ((48.5, 96.5), False),   # 键孔内（第5列第1行）
        ((16.5, 77), False),     # 摇杆孔内
        ((74.5, 139.5), True),   # 键间隙板内
        ((53.5, 140), True),     # 键间隙板内
        ((35, 65), False),       # 麦克孔内
        ((8, 8), False),         # 螺丝孔内
    ]),
    "壳体.stl": (None, [
        (1.0,  (5, 5), True),        # 实底
        (3.0,  (0.75, 78.5), True),  # 底部仓左壁内（避开剖分对角线）
        (3.0,  (5, 78.5), False),    # 底部仓内腔空心
        (10.0, (6, 78.5), True),     # 主板托台凸缘（宽6）
        (10.0, (50, 78.5), False),   # 主板托台中央空
        (10.0, (78, 77.5), False),   # 穿件孔 47×30 内
        (10.0, (8, 8), False),       # 螺丝孔内
        (13.0, (6, 78.5), True),     # 屏幕托台凸缘
        (13.0, (50, 78.5), False),   # 屏幕托台中央空
        (16.0, (0.75, 78.5), True),  # 顶部腔左壁内（避开剖分对角线）
        (16.0, (53.5, 78.5), False), # 顶部腔空心（屏幕区）
    ]),
}

all_ok = True
for fname, spec in checks.items():
    tris = read_stl(os.path.join(OUT, fname))
    b = bbox(tris)
    print(f"\n{fname}: bbox X {b[0]:.1f}~{b[1]:.1f}  Y {b[2]:.1f}~{b[3]:.1f}  Z {b[4]:.1f}~{b[5]:.1f}  ({len(tris)} tris)")
    if isinstance(spec, list):
        spec = [(s[0], (s[1][0], s[1][1]), s[2]) for s in spec]
        for z, (x, y), expect in spec:
            got = point_in_mesh(tris, x, y, z)
            tag = "OK " if got == expect else "FAIL"
            if got != expect:
                all_ok = False
            print(f"  {tag} ({x},{y}) z={z}: in_mesh={got} expect={expect}")
    else:
        if spec[0] is None:   # 带 z 的分层点格式 [(z,(x,y),expect)...]
            for z, (x, y), expect in spec[1]:
                got = point_in_mesh(tris, x, y, z)
                tag = "OK " if got == expect else "FAIL"
                if got != expect:
                    all_ok = False
                print(f"  {tag} ({x},{y}) z={z}: in_mesh={got} expect={expect}")
        else:                 # 单 z 格式 [((x,y),expect)...]
            z = spec[0]
            for (x, y), expect in spec[1]:
                got = point_in_mesh(tris, x, y, z)
                tag = "OK " if got == expect else "FAIL"
                if got != expect:
                    all_ok = False
                print(f"  {tag} ({x},{y}) z={z}: in_mesh={got} expect={expect}")

print("\nALL", "PASS" if all_ok else "FAIL")

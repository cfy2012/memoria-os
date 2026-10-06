# -*- coding: utf-8 -*-
"""Memoria OS 外壳 STL 生成器（纯标准库，无第三方依赖）
按 HARDWARE-DIMENSIONS.md 定版尺寸生成 6 个可 3D 打印件：
  1 前面板  107×157×2.5  屏幕窗71×49 + 键孔60×7.4 + 摇杆Ø16 + 麦克Ø1.5×3 + 四角Ø2.5
  2 隔板③   107×157×2    屏幕窗通孔 + 四角
  3 隔板②   107×157×2    嵌屏孔91×56 + 四角
  4 隔板①   107×157×2    穿件孔47×30 + 四角
  5 底仓壁   外107×157内102×152 高10 四角
  6 底壳    107×157×2    透音孔Ø2×7 + 四角
坐标：面板坐标（原点左上，X 0~107 Y 0~157），Z 从 0 起（打印件，层叠时按层序自对齐）。
"""
import struct, math, os

OUT = r"I:\Memoria OS\memoria-os\design\3D模型\外壳STL"
os.makedirs(OUT, exist_ok=True)

# ---------- 2D 多边形工具 ----------
def rect_cw(cx, cy, w, h):
    """矩形洞（顺时针）"""
    return [(cx-w/2, cy-h/2), (cx-w/2, cy+h/2), (cx+w/2, cy+h/2), (cx+w/2, cy-h/2)]

def circle_cw(cx, cy, r, n=24):
    """圆形洞（顺时针）"""
    return [(cx + r*math.cos(2*math.pi*i/n), cy - r*math.sin(2*math.pi*i/n)) for i in range(n)]

def cross(o, a, b):
    return (a[0]-o[0])*(b[1]-o[1]) - (a[1]-o[1])*(b[0]-o[0])

def seg_inter(a, b, c, d):
    """线段 ab 与 cd 是否相交（不含端点共享）"""
    def ccw(p, q, r): return cross(p, q, r)
    o1, o2, o3, o4 = ccw(a,b,c), ccw(a,b,d), ccw(c,d,a), ccw(c,d,b)
    if o1==0 and min(a[0],b[0])<=c[0]<=max(a[0],b[0]) and min(a[1],b[1])<=c[1]<=max(a[1],b[1]): return True
    if o2==0 and min(a[0],b[0])<=d[0]<=max(a[0],b[0]) and min(a[1],b[1])<=d[1]<=max(a[1],b[1]): return True
    if o3==0 and min(c[0],d[0])<=a[0]<=max(c[0],d[0]) and min(c[1],d[1])<=a[1]<=max(c[1],d[1]): return True
    if o4==0 and min(c[0],d[0])<=b[0]<=max(c[0],d[0]) and min(c[1],d[1])<=b[1]<=max(c[1],d[1]): return True
    return (o1>0)!=(o2>0) and (o3>0)!=(o4>0)

def seg_inter_strict(a, b, c, d):
    """严格内部相交（端点不算、共线不算），用于对角线可见性检查"""
    def ccw(p, q, r): return cross(p, q, r)
    o1, o2 = ccw(a,b,c), ccw(a,b,d)
    o3, o4 = ccw(c,d,a), ccw(c,d,b)
    if o1==0 or o2==0 or o3==0 or o4==0: return False
    return (o1>0)!=(o2>0) and (o3>0)!=(o4>0)

def poly_x_at(poly, y):
    """多边形与水平线 y 的交点 x 列表（成对：进/出）"""
    xs = []
    n = len(poly)
    for i in range(n):
        p1, p2 = poly[i], poly[(i+1) % n]
        if (p1[1] <= y < p2[1]) or (p2[1] <= y < p1[1]):
            t = (y - p1[1]) / (p2[1] - p1[1])
            xs.append(p1[0] + t * (p2[0] - p1[0]))
    xs.sort()
    return xs

def triangulate(outer, holes, W, H):
    """扫描线三角剖分：外框 W×H（原点 0,0 起），holes 为多边形列表。
    按洞顶点 y 切成水平条带，每条带内求板内 x 区间 → 矩形 → 2 三角。
    返回三角形列表（逆时针，y 向下不影响拓扑）。"""
    ys = {0.0, float(H)}
    for h in holes:
        for p in h:
            ys.add(p[1])
    ys = sorted(ys)
    tris = []
    for i in range(len(ys) - 1):
        y0, y1 = ys[i], ys[i+1]
        if y1 - y0 < 1e-9:
            continue
        ym = (y0 + y1) / 2.0
        segs = []
        for h in holes:
            xs = poly_x_at(h, ym)
            for j in range(0, len(xs) - 1, 2):
                segs.append((xs[j], xs[j+1]))
        segs.sort()
        cur = 0.0
        for l, r in segs:
            if r <= cur:
                continue
            if l > cur + 1e-6:
                tris.append(((cur, y0), (l, y0), (l, y1)))
                tris.append(((cur, y0), (l, y1), (cur, y1)))
            cur = max(cur, r)
        if cur < W - 1e-6:
            tris.append(((cur, y0), (W, y0), (W, y1)))
            tris.append(((cur, y0), (W, y1), (cur, y1)))
    return tris

# ---------- STL 输出 ----------
def norm(a, b, c):
    u = (b[0]-a[0], b[1]-a[1], b[2]-a[2]); v = (c[0]-a[0], c[1]-a[1], c[2]-a[2])
    nx = u[1]*v[2]-u[2]*v[1]; ny = u[2]*v[0]-u[0]*v[2]; nz = u[0]*v[1]-u[1]*v[0]
    L = math.sqrt(nx*nx+ny*ny+nz*nz) or 1
    return (nx/L, ny/L, nz/L)

def write_stl(path, tris3d):
    """tris3d: 三角形顶点三元组列表 (a,b,c)，每点 (x,y,z)。"""
    with open(path, "wb") as f:
        f.write(b"Memoria OS shell part".ljust(80, b"\x00"))
        f.write(struct.pack("<I", len(tris3d)))
        for a, b, c in tris3d:
            n = norm(a, b, c)
            f.write(struct.pack("<12f", n[0], n[1], n[2],
                                a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]))
            f.write(struct.pack("<H", 0))

def extrude(tris2d, z0, h):
    """2D 三角形（逆时针）拉伸成三棱柱：顶面 + 底面 + 3 侧面。"""
    out = []
    for a, b, c in tris2d:
        ah, bh, ch = (a[0], a[1], z0+h), (b[0], b[1], z0+h), (c[0], c[1], z0+h)
        a0, b0, c0 = (a[0], a[1], z0), (b[0], b[1], z0), (c[0], c[1], z0)
        out += [(ah, bh, ch), (a0, c0, b0)]          # 顶面(CCW→+Z) 底面(CW→-Z)
        out += [(ah, bh, b0), (ah, b0, a0)]          # 边 a→b
        out += [(bh, ch, c0), (bh, c0, b0)]          # 边 b→c
        out += [(ch, ah, a0), (ch, a0, c0)]          # 边 c→a
    return out

def plate(name, w, h, thick, holes, z0=0.0):
    """矩形板 0..w × 0..h × thick，holes: (cx,cy,w,h) 矩形洞 或 (cx,cy,r) 圆洞"""
    hlist = []
    for hd in holes:
        if len(hd) == 3:
            cx, cy, r = hd; hlist.append(circle_cw(cx, cy, r, 16))
        else:
            cx, cy, ww, hh = hd; hlist.append(rect_cw(cx, cy, ww, hh))
    tris = triangulate(None, hlist, w, h)
    return extrude(tris, z0, thick)

def frame(name, w, h, thick_wall, height, holes=(), z0=0.0):
    """回字形壁框：外 w×h，壁厚 thick_wall，高 height"""
    inner = rect_cw(w/2, h/2, w-2*thick_wall, h-2*thick_wall)
    hlist = [inner] + [circle_cw(cx, cy, r, 16) for (cx, cy, r) in holes]
    tris = triangulate(None, hlist, w, h)
    return extrude(tris, z0, height)

# ---------- 部件定义（面板坐标 mm） ----------
SCREW = [(8, 8, 1.25), (99, 8, 1.25), (8, 149, 1.25), (99, 149, 1.25)]   # 四角 M2.5 通孔
SCREEN_WIN = (53.5, 32.5, 71, 49)                                         # 屏幕窗
KEY_HOLES = [(8.5 + 10*c, 96.5 + 10*r, 7.4, 7.4) for r in range(6) for c in range(10)]
JOY = (16.5, 77, 8.0)                                                     # 摇杆 Ø16
MIC = [(35, 65, 0.75), (45, 62, 0.75), (40, 70, 0.75)]                     # 麦克 Ø1.5×3
THRU = (78, 77.5, 47, 30)                                                 # 主板穿件孔 47×30
CAV = (53.5, 78.5, 102, 152)                                              # 内腔 102×152
CORE90 = (53.5, 78.5, 90, 140)                                            # 托台中央空 90×140

# ---------- 一体壳体：5 层（z 0 底 → 17.5 顶，总厚 17.5，顶盖另 2.5 = 总 20） ----------
BODY_Z = 17.5
def build_body():
    """壳体：实底 → 底部仓 → 主板托台(穿件孔) → 过渡 → 屏幕托台 → 顶部腔"""
    segs = [
        (0.0,  2.0, []),                                        # 实底
        (2.0,  9.0, [CAV] + SCREW),                             # 底部仓（电池/功放/喇叭 + 背焊件伸入）
        (9.0, 11.0, [CORE90] + [THRU] + SCREW),                  # 主板托台（中央空 90×140 + 穿件孔 47×30）
        (11.0, 12.0, [CAV] + SCREW),                             # 过渡壁环
        (12.0, 14.0, [CORE90] + SCREW),                          # 屏幕托台（中央空 90×140）
        (14.0, BODY_Z, [CAV] + SCREW),                           # 顶部腔（屏幕 91×56 横装）
    ]
    tris = []
    for z0, z1, holes in segs:
        hlist = []
        for hd in holes:
            if len(hd) == 3:
                hlist.append(circle_cw(hd[0], hd[1], hd[2], 16))
            else:
                hlist.append(rect_cw(hd[0], hd[1], hd[2], hd[3]))
        tris += extrude(triangulate(None, hlist, 107, 157), z0, z1 - z0)
    return tris

if __name__ == "__main__":
    parts = [
        ("顶盖", plate("顶盖", 107, 157, 2.5,
            [SCREEN_WIN] + KEY_HOLES + [JOY] + MIC + [(8,8,1.25),(99,8,1.25),(8,149,1.25),(99,149,1.25)])),
        ("壳体", build_body()),
    ]

    for name, tris in parts:
        path = os.path.join(OUT, name + ".stl")
        write_stl(path, tris)
        print("wrote", path)

    # ---------- 读回验证 ----------
    print("\n--- verify ---")
    for name, _ in parts:
        path = os.path.join(OUT, name + ".stl")
        with open(path, "rb") as f:
            f.read(80)
            n = struct.unpack("<I", f.read(4))[0]
            f.read(50 * n)
            size = os.path.getsize(path)
        print(f"{name}: {n} tris, {size/1024:.1f} KB")

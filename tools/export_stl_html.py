# -*- coding: utf-8 -*-
"""把 6 个外壳 STL 解析为内嵌数据，生成 Three.js 3D 预览 HTML"""
import struct, os, json, io

OUT_DIR = r"I:\Memoria OS\memoria-os\design\3D模型\外壳STL"
HTML_PATH = r"I:\Memoria OS\memoria-os\design\3D模型\外壳STL预览.html"

# 装配顺序（上→下）与 z 偏移、配色
LAYERS = [
    ("顶盖",  0x3a7bd5, 17.5, True),  # 半透明默认，看到内部层
    ("壳体",  0x17294f, 0.0,  False),
]
GAP = 9.0  # 爆炸间距

def read_stl(path):
    with open(path, "rb") as f:
        f.read(80)
        n = struct.unpack("<I", f.read(4))[0]
        tris = []
        for _ in range(n):
            data = struct.unpack("<12f", f.read(48))
            f.read(2)
            tris.append([data[3:6], data[6:9], data[9:12]])
        return tris

def rnd(v, nd=2):
    return round(v, nd)

def tris_to_js(tris, zoff, exploded):
    zz = zoff + (GAP if exploded else 0.0)
    return [[rnd(v[0]), rnd(v[1]), rnd(v[2] + zz)] for t in tris for v in t]

layers_js = []
for name, color, zoff, trans in LAYERS:
    fname = f"{name}.stl"
    tris = read_stl(os.path.join(OUT_DIR, fname))
    layers_js.append({
        "name": name,
        "color": color,
        "zoff": zoff,
        "trans": trans,
        "norm": tris_to_js(tris, zoff, False),
        "boom": tris_to_js(tris, zoff, True),
        "tris": len(tris),
    })

data = json.dumps(layers_js, ensure_ascii=False)

html = """<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Memoria OS 外壳 · STL 预览</title>
<link rel="icon" type="image/svg+xml" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'%3E%3Crect width='64' height='64' rx='14' fill='%230b1020'/%3E%3Crect x='14' y='14' width='36' height='36' rx='6' fill='none' stroke='%233a7bd5' stroke-width='5'/%3E%3Ccircle cx='32' cy='32' r='6' fill='%233a7bd5'/%3E%3C/svg%3E">
<style>
  * { margin:0; padding:0; box-sizing:border-box; }
  body { background:#0b1020; color:#dfe6f2; font-family: "Microsoft YaHei", system-ui, sans-serif; overflow:hidden; }
  #wrap { display:flex; height:100vh; }
  #side { width:260px; padding:16px 14px; background:rgba(255,255,255,.05); backdrop-filter:blur(14px); border-right:1px solid rgba(255,255,255,.1); overflow-y:auto; flex-shrink:0; }
  #side h1 { font-size:16px; font-weight:700; margin-bottom:4px; letter-spacing:.5px; }
  #side .sub { font-size:11px; color:#8fa3c8; margin-bottom:14px; line-height:1.5; }
  .ctl { display:flex; gap:8px; margin-bottom:14px; flex-wrap:wrap; }
  button { background:rgba(255,255,255,.09); color:#dfe6f2; border:1px solid rgba(255,255,255,.16); border-radius:8px; padding:6px 12px; font-size:12px; cursor:pointer; }
  button:hover { background:rgba(255,255,255,.16); }
  button.on { background:#3a7bd5; border-color:#3a7bd5; }
  .layer { display:flex; align-items:center; gap:9px; padding:9px 10px; border-radius:9px; margin-bottom:6px; background:rgba(255,255,255,.04); border:1px solid rgba(255,255,255,.07); }
  .layer .sw { width:34px; height:14px; border-radius:7px; position:relative; cursor:pointer; background:rgba(255,255,255,.14); transition:.15s; flex-shrink:0; }
  .layer .sw::after { content:""; position:absolute; top:2px; left:2px; width:10px; height:10px; border-radius:50%; background:#9fb2d8; transition:.15s; }
  .layer.on .sw { background:#3a7bd5; }
  .layer.on .sw::after { left:22px; background:#fff; }
  .layer .dot { width:10px; height:10px; border-radius:3px; flex-shrink:0; }
  .layer .nm { font-size:13px; flex:1; }
  .layer .mt { font-size:11px; color:#7d90b5; }
  .layer .tri { font-size:10px; color:#5a6f96; }
  #view { flex:1; position:relative; }
  canvas { display:block; width:100%; height:100%; }
  #hint { position:absolute; bottom:14px; left:50%; transform:translateX(-50%); font-size:11px; color:#7d90b5; background:rgba(10,16,32,.6); padding:6px 14px; border-radius:20px; border:1px solid rgba(255,255,255,.08); }
</style>
</head>
<body>
<div id="wrap">
  <div id="side">
    <h1>Memoria OS 外壳 · STL</h1>
    <div class="sub">2 个打印件 · 顶盖 2.5 + 壳体 17.5 = 总厚 20 · 内部 5 层<br>拖动旋转 · 滚轮缩放 · 右键平移</div>
    <div class="ctl">
      <button id="btnBoom" class="on">爆炸视图</button>
      <button id="btnWire">线框</button>
      <button id="btnReset">重置视角</button>
    </div>
    <div id="layers"></div>
    <div class="sub" style="margin-top:12px">壳体内部 5 层（下→上）：实底 2 · 底部仓 7（电池/功放/喇叭+背焊件）· 主板托台 2（穿件孔47×30）· 过渡 1 · 屏幕托台 2 · 顶部腔 3.5（屏幕91×56 横装）</div>
  </div>
  <div id="view"></div>
</div>
<div id="hint">左键旋转 · 滚轮缩放 · 右键平移</div>

<script src="https://cdn.jsdelivr.net/npm/three@0.128.0/build/three.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/three@0.128.0/examples/js/controls/OrbitControls.js"></script>
<script>
const LAYERS = __LAYER_DATA__;
const GAP = 9.0;

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x0b1020);

const camera = new THREE.PerspectiveCamera(45, 1, 0.1, 2000);
const view = document.getElementById('view');
const renderer = new THREE.WebGLRenderer({ antialias: true });
view.appendChild(renderer.domElement);

const controls = new THREE.OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;
controls.dampingFactor = 0.12;
controls.target.set(53.5, 78.5, 10);

// 灯光
scene.add(new THREE.AmbientLight(0xffffff, 0.55));
const d1 = new THREE.DirectionalLight(0xffffff, 0.7); d1.position.set(120, 180, 200); scene.add(d1);
const d2 = new THREE.DirectionalLight(0x88bbff, 0.35); d2.position.set(-160, -60, 120); scene.add(d2);

function buildGeo(verts) {
  const pos = new Float32Array(verts.length * 3);
  for (let i = 0; i < verts.length; i++) {
    pos[i*3] = verts[i][0]; pos[i*3+1] = verts[i][1]; pos[i*3+2] = verts[i][2];
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
  g.computeVertexNormals();
  return g;
}

const meshes = [];
let boom = true, wire = false;

LAYERS.forEach((L, i) => {
  const mat = new THREE.MeshStandardMaterial({
    color: L.color, roughness: 0.55, metalness: 0.12,
    transparent: L.trans, opacity: L.trans ? 0.5 : 0.96,
    side: THREE.DoubleSide, depthWrite: !L.trans,
  });
  const mesh = new THREE.Mesh(buildGeo(L.boom), mat);
  mesh.userData = L;
  scene.add(mesh);
  meshes.push(mesh);
});

function applyView() {
  meshes.forEach(m => {
    const L = m.userData;
    m.geometry.dispose();
    m.geometry = buildGeo(boom ? L.boom : L.norm);
    m.material.visible = m.userData.show !== false;
  });
}

// 图层面板
const lbox = document.getElementById('layers');
LAYERS.forEach((L, i) => {
  const d = document.createElement('div');
  d.className = 'layer on';
  d.innerHTML = '<span class="sw"></span><span class="dot"></span><span class="nm">' + L.name + '</span><span class="mt">' + (L.name==='顶盖'?'2.5':'17.5') + 'mm</span><span class="tri">' + L.tris + ' tri</span>';
  d.querySelector('.dot').style.background = '#' + L.color.toString(16).padStart(6,'0');
  d.querySelector('.sw').onclick = () => {
    d.classList.toggle('on');
    meshes[i].userData.show = d.classList.contains('on');
    meshes[i].material.visible = meshes[i].userData.show;
  };
  lbox.appendChild(d);
});

document.getElementById('btnBoom').onclick = e => { boom = !boom; e.target.classList.toggle('on', boom); applyView(); };
document.getElementById('btnWire').onclick = e => {
  wire = !wire; e.target.classList.toggle('on', wire);
  meshes.forEach(m => { m.material.wireframe = wire; m.material.transparent = wire ? false : m.userData.trans; m.material.opacity = wire ? 1 : (m.userData.trans ? 0.5 : 0.96); });
};
document.getElementById('btnReset').onclick = () => {
  camera.position.set(120, 200, 260);
  controls.target.set(53.5, 78.5, 10);
  controls.update();
};

function resize() {
  const w = view.clientWidth, h = view.clientHeight;
  renderer.setSize(w, h);
  camera.aspect = w / h;
  camera.updateProjectionMatrix();
}
window.addEventListener('resize', resize);
resize();
camera.position.set(120, 200, 260);
applyView();

(function loop() {
  requestAnimationFrame(loop);
  controls.update();
  renderer.render(scene, camera);
})();
</script>
</body>
</html>
"""

html = html.replace("__LAYER_DATA__", data)
with io.open(HTML_PATH, "w", encoding="utf-8") as f:
    f.write(html)
print("wrote", HTML_PATH, os.path.getsize(HTML_PATH), "bytes")

#!/usr/bin/env python3
"""Archviz soft furnishings made from code (CC0): a pleated linen curtain panel (double sided, gathered folds that
widen toward the hem) and a wool rug texture with a woven look and a border.
Output: Resources/Templates/Shared/Furniture/linen_curtain.glb, Resources/Templates/Projects/archviz/Assets/Textures."""
import math
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from make_track import GLB  # noqa: E402

ROOT = os.path.join(HERE, "..", "..", "..", "Resources", "Templates", "Projects", "archviz", "Assets")
rng = np.random.default_rng(5)

# Keten dokusu (perde): ince atkı / çözgü çizgileri, hafif düzensizlik
s = 512
y, x = np.mgrid[0:s, 0:s].astype(np.float32)
weave = 0.5 + 0.25 * np.sin(x * 2 * np.pi / 3.0) * 0.4 + 0.25 * np.sin(y * 2 * np.pi / 3.0) * 0.4
slub = rng.normal(0.0, 1.0, (s, s)).astype(np.float32)
slub = np.cumsum(slub, axis=1) * 0.004
lin = np.clip(0.9 + (weave - 0.5) * 0.12 + slub * 0.15 + rng.normal(0, 0.02, (s, s)), 0, 1)
col = np.stack([lin * 0.9, lin * 0.86, lin * 0.79], -1)
os.makedirs(os.path.join(ROOT, "Textures"), exist_ok=True)
Image.fromarray((col * 255).astype(np.uint8)).save(os.path.join(ROOT, "Textures", "linen.jpg"), quality=90)

# Yün halı: bej zemin, ilmek dokusu, koyu kenar şeridi
s = 1024
y, x = np.mgrid[0:s, 0:s].astype(np.float32)
pile = rng.normal(0.0, 1.0, (s, s)).astype(np.float32)
pile = (pile + np.roll(pile, 1, 0) + np.roll(pile, 1, 1) + np.roll(pile, -1, 1)) * 0.25
loops = 0.5 + 0.5 * np.sin(x * 2 * np.pi / 6.0) * np.sin(y * 2 * np.pi / 6.0)
base = np.array([0.78, 0.72, 0.62])
v = 1.0 + pile * 0.06 + (loops - 0.5) * 0.05
rgb = base[None, None, :] * v[..., None]
u = x / s
w = y / s
edge = np.minimum(np.minimum(u, 1 - u), np.minimum(w, 1 - w))
border = (edge > 0.035) & (edge < 0.06)
rgb[border] = rgb[border] * np.array([0.42, 0.36, 0.3]) / base
rgb[edge < 0.012] *= 0.85
Image.fromarray((np.clip(rgb, 0, 1) * 255).astype(np.uint8)).save(os.path.join(ROOT, "Textures", "rug_wool.jpg"), quality=90)

# Perde paneli: 0.7 m genişlikte toplanmış, 2.72 m boy; pliler aşağı doğru hafifçe açılır. İki yüzlü (içten ve dıştan).
g = GLB()
mat = g.material("Linen Curtain", color=(1.0, 1.0, 1.0), roughness=0.92, metallic=0.0)
g.textures_src = None
tex = g.tex(open(os.path.join(ROOT, "Textures", "linen.jpg"), "rb").read(), "image/jpeg")
g.materials[mat]["pbrMetallicRoughness"]["baseColorTexture"] = {"index": tex}
g.materials[mat]["doubleSided"] = True
W, H = 0.7, 2.72
nx, ny = 96, 40
pleats = 7
pos, nrm, uv = [], [], []
for j in range(ny + 1):
    t = j / ny                       # 0 = alt etek, 1 = üst
    yy = t * H
    amp = 0.045 + 0.03 * (1.0 - t)   # etekte daha geniş kıvrım
    for i in range(nx + 1):
        u = i / nx
        xx = (u - 0.5) * W
        ph = u * pleats * 2 * math.pi
        zz = amp * math.sin(ph)
        dz = amp * pleats * 2 * math.pi / W * math.cos(ph)
        n = np.array([-dz, 0.0, 1.0])
        n /= np.linalg.norm(n)
        pos.append((xx, yy, zz))
        nrm.append(tuple(n))
        uv.append((u * 1.6, t * 4.0))
idx = []
for j in range(ny):
    for i in range(nx):
        a = j * (nx + 1) + i
        b = a + 1
        c = a + (nx + 1)
        d = c + 1
        idx += [a, b, d, a, d, c]
# Arka yüz: aynı köşeler ters normal, ters sarım
n0 = len(pos)
pos += pos[:]
nrm += [(-a, -b, -c) for (a, b, c) in nrm[:n0]]
uv += uv[:]
idx += [k + n0 for tri in zip(idx[0::3], idx[2::3], idx[1::3]) for k in tri]
g.mesh("Curtain", pos, nrm, uv, idx, mat)
# Mobilyalar ortak pakette (archviz + AR şablonu): Resources/Templates/Shared/Furniture
FURN = os.path.join(HERE, "..", "..", "..", "Resources", "Templates", "Shared", "Furniture")
os.makedirs(FURN, exist_ok=True)
g.save(os.path.join(FURN, "linen_curtain.glb"))

#!/usr/bin/env python3
"""Crowd plaza props made from code (CC0): a stylized street tree (tapered trunk and branches with bark, canopy of
alpha-cut leaf cluster cards with volume-bent normals) and striped canvas awning textures.
Output: Resources/Templates/Shared/Plaza/{Props,Textures} (shared by the Crowd and Short Film templates)."""
import io
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from make_track import GLB  # noqa: E402

ROOT = os.path.join(HERE, "..", "..", "..", "Resources", "Templates", "Shared", "Plaza")
rng = np.random.default_rng(21)


def png_bytes(img):
    b = io.BytesIO()
    img.save(b, "PNG")
    return b.getvalue()


def jpg_bytes(img):
    b = io.BytesIO()
    img.save(b, "JPEG", quality=88)
    return b.getvalue()


# ---------------------------------------------------------------- dokular
def bark_texture():
    w, h = 256, 512
    n = rng.normal(0, 1, (h // 16, w // 4))
    n = np.array(Image.fromarray(((n * 0.25 + 0.5).clip(0, 1) * 255).astype(np.uint8)).resize((w, h), Image.BICUBIC), float) / 255.0
    fiss = np.abs(np.sin(np.linspace(0, 18 * math.pi, w)[None, :] + n * 3.0))
    v = 0.55 + 0.35 * fiss ** 0.6 + rng.normal(0, 0.04, (h, w))
    col = np.stack([v * 0.36, v * 0.31, v * 0.26], -1)
    return Image.fromarray((col.clip(0, 1) * 255).astype(np.uint8))


def leaf_atlas():
    """Yaprak kümesi: yoğun merkez, seyrek kenar; her yaprak mercek biçimli, ton farklı. Alfa = yaprak."""
    s = 512
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for k in range(420):
        r = (rng.random() ** 0.6) * 0.46 * s
        a = rng.random() * 2 * math.pi
        cx, cy = s / 2 + math.cos(a) * r, s / 2 + math.sin(a) * r
        length = rng.uniform(26, 44)
        width = length * rng.uniform(0.38, 0.5)
        rot = rng.random() * 2 * math.pi
        pts = []
        for t in np.linspace(0, 2 * math.pi, 18, endpoint=False):
            x = math.cos(t) * length * 0.5
            y = math.sin(t) * width * 0.5 * (1.0 - 0.35 * math.cos(t))   # sivri uç
            pts.append((cx + x * math.cos(rot) - y * math.sin(rot), cy + x * math.sin(rot) + y * math.cos(rot)))
        g = rng.uniform(0.75, 1.15)
        edge = r / (0.46 * s)
        base = np.array([0.20, 0.36, 0.12]) * g * (1.0 + 0.25 * edge)
        if rng.random() < 0.18:
            base = base * np.array([1.25, 1.1, 0.7])      # sararmış yaprak
        c = tuple(int(v) for v in (np.clip(base, 0, 1) * 255)) + (255,)
        d.polygon(pts, fill=c)
        # orta damar
        d.line([pts[0], pts[9]], fill=tuple(int(v * 0.8) for v in c[:3]) + (255,), width=1)
    return img


def awning(c1, c2):
    w, h = 256, 64
    x = np.arange(w)[None, :].repeat(h, 0)
    stripe = ((x // 32) % 2 == 0)
    a = np.array(c1, float)
    b = np.array(c2, float)
    rgb = np.where(stripe[..., None], a[None, None, :], b[None, None, :])
    weave = rng.normal(0, 0.025, (h, w))[..., None] + 0.02 * np.sin(np.arange(h) * 2.3)[:, None, None]
    rgb = np.clip(rgb * (1.0 + weave), 0, 1)
    return Image.fromarray((rgb * 255).astype(np.uint8))


os.makedirs(os.path.join(ROOT, "Textures"), exist_ok=True)
for name, c1, c2 in [("awning_red", (0.66, 0.1, 0.1), (0.92, 0.9, 0.84)), ("awning_green", (0.12, 0.36, 0.24), (0.9, 0.88, 0.8)),
                     ("awning_navy", (0.12, 0.17, 0.33), (0.9, 0.88, 0.82)), ("awning_terracotta", (0.72, 0.36, 0.2), (0.93, 0.88, 0.76))]:
    awning(c1, c2).save(os.path.join(ROOT, "Textures", name + ".png"))


# ---------------------------------------------------------------- ağaç geometrisi
def tube(p0, p1, r0, r1, sides, v0=0.0, vscale=1.0):
    p0, p1 = np.array(p0, float), np.array(p1, float)
    ax = p1 - p0
    L = np.linalg.norm(ax)
    ax /= L
    ref = np.array([0, 0, 1.0]) if abs(ax[1]) > 0.9 else np.array([0, 1.0, 0])
    u = np.cross(ax, ref)
    u /= np.linalg.norm(u)
    v = np.cross(ax, u)
    pos, nrm, uv, idx = [], [], [], []
    rings = max(2, int(L / 0.35) + 1)
    for j in range(rings):
        t = j / (rings - 1)
        c = p0 + ax * L * t
        r = r0 + (r1 - r0) * t
        for i in range(sides + 1):
            a = 2 * math.pi * i / sides
            n = u * math.cos(a) + v * math.sin(a)
            pos.append(tuple(c + n * r))
            nrm.append(tuple(n))
            uv.append((i / sides * 2.0, v0 + L * t * vscale))
    for j in range(rings - 1):
        for i in range(sides):
            a = j * (sides + 1) + i
            b = a + 1
            cc = a + (sides + 1)
            d = cc + 1
            idx += [a, cc, b, b, cc, d]
    return pos, nrm, uv, idx


def merge(parts):
    pos, nrm, uv, idx = [], [], [], []
    for p, n, u, i in parts:
        base = len(pos)
        pos += p
        nrm += n
        uv += u
        idx += [k + base for k in i]
    return pos, nrm, uv, idx


g = GLB()
bark = g.tex(jpg_bytes(bark_texture()), "image/jpeg")
m_bark = g.material("Bark", tex=bark, roughness=0.92)
leaf_tex = g.tex(png_bytes(leaf_atlas()), "image/png")
m_leaf = g.material("Leaves", tex=leaf_tex, roughness=0.78)
g.materials[m_leaf]["alphaMode"] = "MASK"
g.materials[m_leaf]["alphaCutoff"] = 0.5
g.materials[m_leaf]["doubleSided"] = True

CANOPY_C = np.array([0.0, 4.35, 0.0])
RX, RY = 2.35, 1.75
parts = [tube((0, -0.05, 0), (0.0, 0.35, 0.0), 0.24, 0.16, 12), tube((0, 0.35, 0), (0.04, 2.75, 0.02), 0.16, 0.12, 12, 0.4)]
branch_tips = []
for k in range(6):
    a = k / 6 * 2 * math.pi + rng.uniform(-0.3, 0.3)
    start = np.array([0.04, 2.45 + rng.uniform(0, 0.3), 0.02])
    tip = CANOPY_C + np.array([math.cos(a) * RX * 0.62, rng.uniform(-0.3, 0.7), math.sin(a) * RX * 0.62])
    mid = start + (tip - start) * 0.5 + np.array([0, 0.25, 0])
    parts.append(tube(start, mid, 0.085, 0.06, 8))
    parts.append(tube(mid, tip, 0.06, 0.025, 8))
    branch_tips.append(tip)
parts.append(tube((0.04, 2.75, 0.02), CANOPY_C + np.array([0, 1.0, 0]), 0.1, 0.03, 8))
pos, nrm, uv, idx = merge(parts)
g.mesh("Tree Trunk", pos, nrm, uv, idx, m_bark)

# Taç: elipsoid kabuğa yayılmış yaprak kartları; normaller taç merkezinden dışa (yumuşak hacim gölgelemesi)
pos, nrm, uv, idx = [], [], [], []
cards = 120
for k in range(cards):
    while True:
        d = rng.normal(0, 1, 3)
        d /= np.linalg.norm(d)
        if d[1] > -0.75:
            break
    rad = 0.55 + 0.45 * rng.random() ** 0.5
    c = CANOPY_C + np.array([d[0] * RX, d[1] * RY, d[2] * RX]) * rad * 0.82
    n = d + rng.normal(0, 0.45, 3)
    n /= np.linalg.norm(n)
    up = np.array([0, 1.0, 0]) if abs(n[1]) < 0.95 else np.array([1.0, 0, 0])
    t = np.cross(up, n)
    t /= np.linalg.norm(t)
    b = np.cross(n, t)
    rot = rng.random() * 2 * math.pi
    t, b = t * math.cos(rot) + b * math.sin(rot), b * math.cos(rot) - t * math.sin(rot)
    size = rng.uniform(1.05, 1.55) * 0.5
    base = len(pos)
    for (su, sv) in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
        p = c + t * su * size + b * sv * size
        vn = (p - CANOPY_C) / np.array([RX, RY, RX])
        vn = vn / (np.linalg.norm(vn) + 1e-6)
        pos.append(tuple(p))
        nrm.append(tuple(vn))
        uv.append(((su + 1) * 0.5, (sv + 1) * 0.5))
    idx += [base, base + 1, base + 2, base, base + 2, base + 3]
g.mesh("Tree Canopy", pos, nrm, uv, idx, m_leaf)
os.makedirs(os.path.join(ROOT, "Props"), exist_ok=True)
g.save(os.path.join(ROOT, "Props", "plaza_tree.glb"))
print("plaza props ->", os.path.normpath(ROOT))

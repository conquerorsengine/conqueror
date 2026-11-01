#!/usr/bin/env python3
"""Virtual Production set dressing made from code (CC0): forest-floor texture (soil, leaf litter, twigs), a fern
(arched alpha-cut fronds), a mossy rock and a fallen log. They blend the physical stage into the forest shown on the LED
wall (practical set dressing in front of the LED volume). Output: Resources/Templates/Projects/virtualproduction/Assets."""
import io
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from make_track import GLB  # noqa: E402

ROOT = os.path.join(HERE, "..", "..", "..", "Resources", "Templates", "Projects", "virtualproduction", "Assets")
rng = np.random.default_rng(33)


def img_bytes(img, fmt):
    b = io.BytesIO()
    img.save(b, fmt, **({"quality": 88} if fmt == "JPEG" else {}))
    return b.getvalue()


def smooth_noise(h, w, cell, amp=1.0):
    n = rng.normal(0, 1, (h // cell + 2, w // cell + 2))
    n = np.array(Image.fromarray(((n * 0.25 + 0.5).clip(0, 1) * 255).astype(np.uint8)).resize((w + 2 * cell, h + 2 * cell), Image.BICUBIC), float)
    return (n[cell:cell + h, cell:cell + w] / 255.0 - 0.5) * amp


# ---------------------------------------------------------------- orman tabanı (döşenebilir)
S = 1024
soil = 0.5 + smooth_noise(S, S, 64, 0.5) + smooth_noise(S, S, 8, 0.25)
rgb = np.stack([0.24 + soil * 0.12, 0.19 + soil * 0.09, 0.13 + soil * 0.06], -1)
img = Image.fromarray((rgb.clip(0, 1) * 255).astype(np.uint8))
d = ImageDraw.Draw(img)
for k in range(1400):                      # kuru yapraklar (kahve, turuncu, sarı tonları), ağırlıkla yere yatık
    cx, cy = rng.uniform(0, S), rng.uniform(0, S)
    L = rng.uniform(14, 34)
    Wd = L * rng.uniform(0.35, 0.55)
    rot = rng.uniform(0, math.pi)
    tone = rng.choice([(0.42, 0.27, 0.12), (0.55, 0.36, 0.14), (0.36, 0.3, 0.16), (0.48, 0.4, 0.2), (0.3, 0.22, 0.12)])
    shade = rng.uniform(0.7, 1.15)
    pts = []
    for t in np.linspace(0, 2 * math.pi, 14, endpoint=False):
        x = math.cos(t) * L * 0.5
        y = math.sin(t) * Wd * 0.5 * (1.0 - 0.3 * math.cos(t))
        pts.append((cx + x * math.cos(rot) - y * math.sin(rot), cy + x * math.sin(rot) + y * math.cos(rot)))
    d.polygon(pts, fill=tuple(int(c * shade * 255) for c in tone))
for k in range(90):                        # ince dallar
    x0, y0 = rng.uniform(0, S), rng.uniform(0, S)
    a = rng.uniform(0, math.pi)
    L = rng.uniform(30, 110)
    d.line([(x0, y0), (x0 + math.cos(a) * L, y0 + math.sin(a) * L)], fill=(52, 38, 26), width=int(rng.integers(2, 5)))
for k in range(260):                       # yosun lekeleri
    x0, y0, r = rng.uniform(0, S), rng.uniform(0, S), rng.uniform(4, 16)
    d.ellipse([x0 - r, y0 - r, x0 + r, y0 + r], fill=(60, 78, 34))
img = img.filter(ImageFilter.GaussianBlur(0.6))
os.makedirs(os.path.join(ROOT, "Textures"), exist_ok=True)
img.save(os.path.join(ROOT, "Textures", "forest_floor.jpg"), quality=88)


# ---------------------------------------------------------------- eğrelti yaprağı dokusu (alfa)
def frond_texture():
    W, H = 256, 1024
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    dr = ImageDraw.Draw(im)
    pinnae = 34
    for i in range(pinnae):
        t = (i + 0.5) / pinnae                    # 0 dip, 1 uç
        y = H - 20 - t * (H - 60)
        length = (W * 0.46) * (1.0 - t ** 1.4) * (0.55 + 0.45 * min(1.0, t * 4))
        for side in (-1, 1):
            base = (W / 2, y)
            tip = (W / 2 + side * length, y - length * 0.35)
            mid = ((base[0] + tip[0]) / 2, (base[1] + tip[1]) / 2)
            nx, ny = -(tip[1] - base[1]), (tip[0] - base[0])
            nl = math.hypot(nx, ny) or 1
            wdt = 9 + 6 * (1 - t)
            pts = [base, (mid[0] + nx / nl * wdt, mid[1] + ny / nl * wdt), tip, (mid[0] - nx / nl * wdt * 0.6, mid[1] - ny / nl * wdt * 0.6)]
            g = rng.uniform(0.85, 1.1)
            dr.polygon(pts, fill=(int(52 * g), int(98 * g), int(30 * g), 255))
    dr.line([(W / 2, H - 4), (W / 2, 40)], fill=(70, 88, 34, 255), width=5)
    return im


# ---------------------------------------------------------------- eğrelti: kavisli yaprak şeritleri
g = GLB()
fern_tex = g.tex(img_bytes(frond_texture(), "PNG"), "image/png")
m_fern = g.material("Fern", tex=fern_tex, roughness=0.75)
g.materials[m_fern]["alphaMode"] = "MASK"
g.materials[m_fern]["alphaCutoff"] = 0.45
g.materials[m_fern]["doubleSided"] = True
pos, nrm, uv, idx = [], [], [], []
fronds = 13
for f in range(fronds):
    az = f / fronds * 2 * math.pi + rng.uniform(-0.2, 0.2)
    length = rng.uniform(0.75, 1.15)
    rise = rng.uniform(0.55, 0.8)              # ilk yükseliş, sonra sarkar
    width = rng.uniform(0.3, 0.38)
    dirx, dirz = math.cos(az), math.sin(az)
    sx, sz = -dirz, dirx                       # yan eksen
    seg = 10
    base = len(pos)
    for k in range(seg + 1):
        t = k / seg
        r = t * length
        y = 0.04 + rise * math.sin(t * math.pi * 0.62) - 0.25 * t * t
        cx, cz = dirx * r, dirz * r
        wv = width * (1.0 - 0.7 * t) * 0.5
        tilt = 0.35                            # şerit hafif içe kıvrık (V kesit)
        for s in (-1, 1):
            pos.append((cx + sx * wv * s, y + abs(s) * wv * tilt * (1 - t), cz + sz * wv * s))
            n = np.array([-dirx * 0.3, 1.0, -dirz * 0.3])
            nrm.append(tuple(n / np.linalg.norm(n)))
            uv.append(((s + 1) * 0.5, t))
    for k in range(seg):
        a = base + k * 2
        idx += [a, a + 1, a + 2, a + 1, a + 3, a + 2]
g.mesh("Fern", pos, nrm, uv, idx, m_fern)
os.makedirs(os.path.join(ROOT, "Props"), exist_ok=True)
g.save(os.path.join(ROOT, "Props", "fern.glb"))


# ---------------------------------------------------------------- yosunlu kaya
def rock_texture():
    S = 512
    base = 0.5 + smooth_noise(S, S, 32, 0.6) + smooth_noise(S, S, 6, 0.3)
    rgb = np.stack([0.42 + base * 0.2, 0.41 + base * 0.2, 0.38 + base * 0.18], -1)
    moss = (smooth_noise(S, S, 48, 1.0) > 0.08)[..., None]
    rgb = np.where(moss, rgb * np.array([0.55, 0.8, 0.4]), rgb)
    return Image.fromarray((rgb.clip(0, 1) * 255).astype(np.uint8))


g = GLB()
m_rock = g.material("Rock", tex=g.tex(img_bytes(rock_texture(), "JPEG"), "image/jpeg"), roughness=0.85)
seg_u, seg_v = 32, 20
P = np.zeros((seg_v + 1, seg_u + 1, 3))
UV = np.zeros((seg_v + 1, seg_u + 1, 2))
ph = rng.uniform(0, 6.28, 6)
for j in range(seg_v + 1):
    th = j / seg_v * math.pi
    for i in range(seg_u + 1):
        phi = i / seg_u * 2 * math.pi
        dvec = np.array([math.sin(th) * math.cos(phi), math.cos(th), math.sin(th) * math.sin(phi)])
        bump = 1.0 + 0.18 * math.sin(3 * dvec[0] + ph[0]) * math.cos(2 * dvec[2] + ph[1]) + 0.1 * math.sin(5 * dvec[1] + ph[2]) \
            + 0.06 * math.sin(9 * dvec[0] + 7 * dvec[2] + ph[3])
        p = dvec * bump
        p[1] = p[1] * 0.62 if p[1] > 0 else p[1] * 0.25        # yassı, alt düz (zemine oturur)
        P[j, i] = p
        UV[j, i] = (i / seg_u * 2.0, j / seg_v)
N = np.zeros_like(P)
for j in range(seg_v + 1):
    for i in range(seg_u + 1):
        a = P[min(j + 1, seg_v), i] - P[max(j - 1, 0), i]
        b = P[j, (i + 1) % seg_u] - P[j, (i - 1) % seg_u]
        n = np.cross(b, a)
        ln = np.linalg.norm(n)
        N[j, i] = n / ln if ln > 1e-8 else (P[j, i] / (np.linalg.norm(P[j, i]) + 1e-8))
        if np.dot(N[j, i], P[j, i]) < 0:
            N[j, i] = -N[j, i]
pos = P.reshape(-1, 3).tolist()
nrm = N.reshape(-1, 3).tolist()
uv = UV.reshape(-1, 2).tolist()
idx = []
for j in range(seg_v):
    for i in range(seg_u):
        a = j * (seg_u + 1) + i
        b = a + 1
        c = a + (seg_u + 1)
        dd = c + 1
        idx += [a, b, c, b, dd, c]
g.mesh("Rock", pos, nrm, uv, idx, m_rock)
g.save(os.path.join(ROOT, "Props", "mossy_rock.glb"))


# ---------------------------------------------------------------- devrik kütük
def bark_texture():
    w, h = 256, 512
    n = smooth_noise(h, w, 16, 1.0)
    fiss = np.abs(np.sin(np.linspace(0, 16 * math.pi, h)[:, None] + n * 4.0))
    v = 0.5 + 0.35 * fiss ** 0.6 + rng.normal(0, 0.04, (h, w))
    moss = smooth_noise(h, w, 40, 1.0)[..., None] > 0.12
    rgb = np.stack([v * 0.34, v * 0.29, v * 0.23], -1)
    rgb = np.where(moss, rgb * np.array([0.7, 1.15, 0.55]), rgb)
    return Image.fromarray((rgb.clip(0, 1) * 255).astype(np.uint8))


g = GLB()
m_bark = g.material("Log Bark", tex=g.tex(img_bytes(bark_texture(), "JPEG"), "image/jpeg"), roughness=0.9)
pos, nrm, uv, idx = [], [], [], []
L, R0, sides, rings = 3.2, 0.21, 16, 12
for k in range(rings + 1):
    t = k / rings
    x = (t - 0.5) * L
    bend = 0.08 * math.sin(t * math.pi)
    r = R0 * (1.0 - 0.18 * t) * (1.0 + 0.05 * math.sin(t * 9))
    for i in range(sides + 1):
        a = i / sides * 2 * math.pi
        ny, nz = math.cos(a), math.sin(a)
        pos.append((x, r + r * ny * 0.95 + bend, r * nz))
        nrm.append((0.0, ny, nz))
        uv.append((t * 3.0, i / sides))
for k in range(rings):
    for i in range(sides):
        a = k * (sides + 1) + i
        b = a + 1
        c = a + (sides + 1)
        dd = c + 1
        idx += [a, b, c, b, dd, c]
g.mesh("Log", pos, nrm, uv, idx, m_bark)
g.save(os.path.join(ROOT, "Props", "fallen_log.glb"))
print("vp dressing ->", os.path.normpath(ROOT))

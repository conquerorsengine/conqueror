#!/usr/bin/env python3
"""AR template assets (CC0, generated): plane visualizer dots, placement reticle, selection ring, feature point, scan
prompt icon (phone with motion arcs). Also copies the room textures it shares with the Archviz template.
Output: Resources/Templates/Projects/ar/Assets/{AR,Textures}."""
import math
import os
import shutil
import wave

import numpy as np

from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..", "..", "Resources", "Templates")
OUT = os.path.join(ROOT, "Projects", "ar", "Assets")
os.makedirs(os.path.join(OUT, "AR"), exist_ok=True)
os.makedirs(os.path.join(OUT, "Textures"), exist_ok=True)
SS = 4   # süper örnekleme (yumuşak kenarlar)


def canvas(size):
    return Image.new("RGBA", (size * SS, size * SS), (0, 0, 0, 0))


def finish(img, size, name):
    img.resize((size, size), Image.LANCZOS).save(os.path.join(OUT, "AR", name))


# Düzlem görselleştirici (ARCore gibi): hafif beyaz dolgu üstünde düzenli beyaz noktalar. 4 x 4 nokta, döşenir.
size = 128
img = canvas(size)
d = ImageDraw.Draw(img)
d.rectangle([0, 0, size * SS, size * SS], fill=(255, 255, 255, 16))
cell = size * SS / 4
r = 2.3 * SS
for i in range(4):
    for j in range(4):
        cx, cy = (i + 0.5) * cell, (j + 0.5) * cell
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(255, 255, 255, 235))
finish(img, size, "plane_dots.png")

# Yerleştirme göstergesi: dört boşluklu halka + orta nokta, koyu gölge halkası (açık zeminde de seçilsin)
size = 256
img = canvas(size)
sh = canvas(size)
c = size * SS / 2
for target, col, grow in ((sh, (0, 0, 0, 120), 2.5 * SS), (img, (255, 255, 255, 255), 0)):
    dd = ImageDraw.Draw(target)
    ro, ri = 112 * SS + grow, 100 * SS - grow
    for k in range(4):
        a0 = k * 90 + 12
        a1 = k * 90 + 78
        dd.pieslice([c - ro, c - ro, c + ro, c + ro], a0, a1, fill=col)
    dd.ellipse([c - ri, c - ri, c + ri, c + ri], fill=(0, 0, 0, 0))
    rd = 11 * SS + grow
    dd.ellipse([c - rd, c - rd, c + rd, c + rd], fill=col)
sh = sh.filter(ImageFilter.GaussianBlur(3 * SS))
out = Image.alpha_composite(sh, img)
finish(out, size, "reticle.png")

# Seçim halkası: yumuşak kenarlı ince halka (betik renklendirir)
img = canvas(size)
px = img.load()
for y in range(0, size * SS, SS):
    for x in range(0, size * SS, SS):
        dx, dy = (x - c) / (size * SS / 2), (y - c) / (size * SS / 2)
        rr = math.sqrt(dx * dx + dy * dy)
        a = max(0.0, 1.0 - abs(rr - 0.86) / 0.07) ** 1.5 + 0.18 * max(0.0, 1.0 - abs(rr - 0.86) / 0.2)
        a = min(1.0, a) * (1.0 if rr < 0.99 else 0.0)
        for oy in range(SS):
            for ox in range(SS):
                px[x + ox, y + oy] = (255, 255, 255, int(255 * a))
finish(img, size, "selection_ring.png")

# Öznitelik noktası: yumuşak parlak nokta
size = 64
img = canvas(size)
px = img.load()
for y in range(size * SS):
    for x in range(size * SS):
        dx, dy = (x + 0.5 - size * SS / 2) / (size * SS / 2), (y + 0.5 - size * SS / 2) / (size * SS / 2)
        rr = math.sqrt(dx * dx + dy * dy)
        a = max(0.0, 1.0 - rr) ** 2.2
        px[x, y] = (255, 255, 255, int(255 * a))
finish(img, size, "feature_point.png")

# Tarama simgesi: eğik telefon + iki yanda hareket yayları (ARCore'un "yüzey bul" animasyonu gibi)
size = 160
img = canvas(size)
d = ImageDraw.Draw(img)
W = 6 * SS
phone = canvas(size)
dp = ImageDraw.Draw(phone)
pw, ph = 54 * SS, 92 * SS
x0, y0 = c * 160 / 256 - pw / 2, 160 * SS / 2 - ph / 2
dp.rounded_rectangle([x0, y0, x0 + pw, y0 + ph], radius=10 * SS, outline=(255, 255, 255, 255), width=W)
dp.rounded_rectangle([x0 + 10 * SS, y0 + 7 * SS, x0 + pw - 10 * SS, y0 + 10 * SS], radius=2 * SS, fill=(255, 255, 255, 255))
dp.ellipse([x0 + pw / 2 - 4 * SS, y0 + ph - 14 * SS, x0 + pw / 2 + 4 * SS, y0 + ph - 6 * SS], fill=(255, 255, 255, 255))
phone = phone.rotate(-14, resample=Image.BICUBIC, center=(160 * SS / 2, 160 * SS / 2))
img = Image.alpha_composite(img, phone)
d = ImageDraw.Draw(img)
cc = 160 * SS / 2
for side in (-1, 1):
    for k, rad in enumerate((62 * SS, 74 * SS)):
        box = [cc - rad, cc - rad, cc + rad, cc + rad]
        start = 180 - 22 if side < 0 else -22
        d.arc(box, start, start + 44, fill=(255, 255, 255, 230 - k * 90), width=5 * SS)
finish(img, 160, "scan_phone.png")

# Yerleştirme sesi: yumuşak "pop" (yükselen kısa ton + hafif tık), 0.18 s
rate = 22050
n = int(rate * 0.18)
t = np.arange(n) / rate
f = 520.0 + 900.0 * (1.0 - np.exp(-t * 40.0))
tone = np.sin(2 * np.pi * np.cumsum(f) / rate) * np.exp(-t * 22.0)
click = np.random.default_rng(3).normal(0.0, 1.0, n) * np.exp(-t * 400.0) * 0.25
env = np.minimum(t / 0.003, 1.0)
pcm = (np.clip((tone * 0.7 + click) * env, -1, 1) * 30000).astype(np.int16)
with wave.open(os.path.join(OUT, "AR", "place.wav"), "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(rate)
    w.writeframes(pcm.tobytes())

# Oda dokuları: Archviz şablonuyla aynı (Poly Haven / ambientCG CC0)
for name in ("laminate_floor_02.jpg", "white_plaster.jpg", "fabric_pattern_07.jpg", "lawn_grass.jpg"):
    shutil.copy(os.path.join(ROOT, "Projects", "archviz", "Assets", "Textures", name), os.path.join(OUT, "Textures", name))
print("ok")

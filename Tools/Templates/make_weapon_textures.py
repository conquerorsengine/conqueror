#!/usr/bin/env python3
"""HUD and effect textures for the shooter templates (Resources/Templates/Shared/Weapons/Textures)."""
import os
import sys

import numpy as np
from PIL import Image

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Resources", "Templates", "Shared", "Weapons", "Textures")
os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(11)


def save(name, rgb, a):
    img = np.dstack([np.clip(rgb, 0, 1), np.clip(a, 0, 1)])
    Image.fromarray((img * 255 + 0.5).astype(np.uint8)).save(os.path.join(OUT, name))


def grid(s):
    y, x = np.mgrid[0:s, 0:s].astype(np.float32)
    return x + 0.5 - s / 2, y + 0.5 - s / 2


# Mermi deliği: koyu merkez, yırtık kenarlı halka, çevrede hafif is
s = 128
x, y = grid(s)
r = np.hypot(x, y) / (s / 2)
ang = np.arctan2(y, x)
jag = 0.08 * np.sin(ang * 7 + 1.3) + 0.05 * np.sin(ang * 13)
hole = r < 0.24 + jag * 0.5
ring = (r < 0.44 + jag) & ~hole
soot = np.clip(1 - r, 0, 1) ** 2 * 0.55
a = np.maximum(np.where(hole, 1.0, 0), np.where(ring, 0.85, 0))
a = np.maximum(a, soot)
c = np.where(hole, 0.02, np.where(ring, 0.18, 0.12))
save("bullet_hole.png", np.dstack([c, c, c * 1.02]), a)

# Nişangah parçası (ince çizgi), isabet işareti, nokta
s = 64
x, y = grid(s)
# Beyaz çizgi + koyu kenar (açık ve koyu zeminde okunur)
inner = (np.abs(x) < 2.5) & (np.abs(y) < 28)
outer = (np.abs(x) < 5) & (np.abs(y) < 31)
c = np.where(inner, 1.0, 0.0)
save("crosshair_line.png", np.dstack([c, c, c]), np.where(inner, 1.0, np.where(outer, 0.55, 0.0)))
r = np.hypot(x, y)
c = np.where(r < 4.5, 1.0, 0.0)
save("crosshair_dot.png", np.dstack([c, c, c]), np.where(r < 4.5, 1.0, np.where(r < 7.5, 0.55, 0.0)))
d1 = np.abs(x - y) / np.sqrt(2)
d2 = np.abs(x + y) / np.sqrt(2)
rr = np.hypot(x, y)
hm = (((d1 < 2.5) | (d2 < 2.5)) & (rr > 9) & (rr < 26)).astype(np.float32)
save("hitmarker.png", np.ones((s, s, 3)), hm)
# Hasar kenarı (kırmızı vinyet)
s = 256
x, y = grid(s)
r = np.hypot(x / (s / 2), y / (s / 2))
save("damage_vignette.png", np.dstack([np.full((s, s), 0.75), np.full((s, s), 0.02), np.full((s, s), 0.02)]), np.clip((r - 0.55) / 0.6, 0, 1) ** 1.5)
# Namlu alevi (yıldız)
s = 128
x, y = grid(s)
r = np.hypot(x, y) / (s / 2)
ang = np.arctan2(y, x)
star = np.clip(1 - r / (0.35 + 0.65 * np.abs(np.cos(ang * 3)) ** 8), 0, 1)
core = np.clip(1 - r * 2.5, 0, 1)
a = np.clip(star * 0.9 + core, 0, 1)
save("muzzle_flash.png", np.dstack([np.ones((s, s)), 0.75 + 0.25 * core, 0.35 + 0.65 * core]), a)
# Namlu alevi yandan (mercek biçimli, iki ucu sivri, ortası parlak): namlu eksenini içeren çapraz düzlemlerde
w_, h_ = 64, 192
yy, xx = np.mgrid[0:h_, 0:w_].astype(np.float32) + 0.5
t = yy / h_
half = 0.5 * np.clip(np.sin(np.pi * t), 0, 1) ** 0.75
dx = np.abs(xx / w_ - 0.5) / np.maximum(half, 1e-3)
side_a = np.clip(1.0 - dx, 0, 1) ** 1.3 * np.clip(np.sin(np.pi * t), 0, 1) ** 0.5
side_core = np.clip(1.0 - dx * 2.2, 0, 1) * np.clip(np.sin(np.pi * t) * 1.4, 0, 1)
side_img = np.dstack([np.ones((h_, w_)), 0.72 + 0.28 * side_core, 0.3 + 0.7 * side_core, np.clip(side_a * 0.95 + side_core, 0, 1)])
Image.fromarray((np.clip(side_img, 0, 1) * 255 + 0.5).astype(np.uint8)).save(os.path.join(OUT, "muzzle_side.png"))
# Yumuşak parıltı (parçacıklar)
s = 64
x, y = grid(s)
r = np.hypot(x, y) / (s / 2)
save("soft_glow.png", np.ones((s, s, 3)), np.clip(1 - r, 0, 1) ** 2)
# Atış hedefi (halkalar)
s = 512
x, y = grid(s)
r = np.hypot(x, y) / (s / 2)
ringi = np.floor(r * 10)
col = np.where(ringi % 2 == 0, 0.95, 0.12)
red = r < 0.1
rgb = np.dstack([np.where(red, 0.9, col), np.where(red, 0.12, col), np.where(red, 0.1, col)])
save("target.png", rgb, np.where(r < 1.0, 1.0, 0.0))
print("weapon textures written to", os.path.abspath(OUT))

# Dürbün görüntüsü (tam nişanda ekranı kaplar): siyah çerçeve, yumuşak kenarlı daire, ince artı, ortada kırmızı nokta,
# merceğin kenarında hafif koyulaşma. Kare doku; HUD en-boy oranından bağımsız ortada kare olarak çizer.
s = 1024
yy, xx = np.mgrid[0:s, 0:s] / (s - 1) * 2 - 1
r = np.sqrt(xx * xx + yy * yy)
lens = np.clip((0.97 - r) / 0.025, 0, 1)                 # 1: mercek içi
edge_dark = np.clip((r - 0.75) / 0.22, 0, 1) ** 2 * 0.55   # merceğin iç kenarı koyulaşır
line = ((np.abs(xx) < 0.0028) | (np.abs(yy) < 0.0028)) & (r < 0.93)
thick = (((np.abs(xx) < 0.009) & (np.abs(yy) > 0.45)) | ((np.abs(yy) < 0.009) & (np.abs(xx) > 0.45))) & (r < 0.95)
dot = r < 0.008
a = 1 - lens * (1 - edge_dark)
a = np.where(line | thick, 1.0, a)
red = dot
rgb = np.dstack([np.where(red, 0.9, 0.0), np.zeros((s, s)), np.zeros((s, s))])
a = np.where(red, 1.0, a)
save("scope_overlay.png", rgb, a)

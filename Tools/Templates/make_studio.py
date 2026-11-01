#!/usr/bin/env python3
"""Car Configurator studio assets made from code (CC0): round cyclorama (infinity cove) in white and dark variants,
a smooth turntable with a chrome edge and an LED ring, a soft contact shadow texture and the hotspot icon.
Output: Resources/Templates/Projects/configurator/Assets/Studio."""
import math
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_track import GLB  # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Resources", "Templates", "Projects", "configurator", "Assets", "Studio")
os.makedirs(OUT, exist_ok=True)


def revolve(profile, segments, u_repeat, v_scale):
    """profile: [(r, y, nr, ny)] (normals 2D, profile order defines the visible side: outward then down for convex
    objects, outward then up for the inside of a bowl). Returns pos, nrm, uv, idx (CCW toward the normal)."""
    pos, nrm, uv = [], [], []
    s = [0.0]
    for i in range(1, len(profile)):
        s.append(s[-1] + math.hypot(profile[i][0] - profile[i - 1][0], profile[i][1] - profile[i - 1][1]))
    for j in range(segments + 1):
        phi = 2.0 * math.pi * j / segments
        c, sn = math.cos(phi), math.sin(phi)
        for i, (r, y, nr, ny) in enumerate(profile):
            pos.append((r * c, y, r * sn))
            l = math.hypot(nr, ny) or 1.0
            nrm.append((nr / l * c, ny / l, nr / l * sn))
            uv.append((j / segments * u_repeat, s[i] * v_scale))
    n = len(profile)
    idx = []
    for j in range(segments):
        for i in range(n - 1):
            a = j * n + i
            b = j * n + i + 1
            d = (j + 1) * n + i
            cc = (j + 1) * n + i + 1
            idx += [a, d, b, b, d, cc]
    return pos, nrm, uv, idx


def cyc_profile():
    """Floor (0..10 m) -> fillet (radius 5) -> wall (15 m radius, up to 13 m). Inside surface: normals toward the axis."""
    p = []
    for r in np.linspace(0.0, 10.0, 21):
        p.append((float(r), 0.0, 0.0, 1.0))
    for k in range(1, 25):
        t = k / 24.0 * math.pi / 2
        p.append((10.0 + 5.0 * math.sin(t), 5.0 - 5.0 * math.cos(t), -math.sin(t), math.cos(t)))
    for y in np.linspace(5.0, 13.0, 9)[1:]:
        p.append((15.0, float(y), -1.0, 0.0))
    return p


def save_cyc(name, color, roughness):
    g = GLB()
    mat = g.material(name, color=color, roughness=roughness, metallic=0.0)
    pos, nrm, uv, idx = revolve(cyc_profile(), 192, 16.0, 0.25)
    g.mesh(name, pos, nrm, uv, idx, mat)
    g.save(os.path.join(OUT, name + ".glb"))


save_cyc("Cyclorama", (0.78, 0.785, 0.79), 0.62)
save_cyc("Cyclorama_Dark", (0.022, 0.023, 0.026), 0.3)

# Döner platform: koyu grafit üst, yuvarlatılmış krom kenar, tabanda LED şerit
g = GLB()
m_top = g.material("Turntable Top", color=(0.035, 0.037, 0.042), roughness=0.42, metallic=0.0)
m_edge = g.material("Turntable Edge", color=(0.85, 0.86, 0.88), roughness=0.18, metallic=1.0)
m_led = g.material("Turntable LED", color=(0.02, 0.02, 0.02), roughness=0.4, metallic=0.0)
g.materials[m_led]["emissiveFactor"] = [0.55, 0.78, 1.0]
g.materials[m_led]["extensions"] = {"KHR_materials_emissive_strength": {"emissiveStrength": 6.0}}
top = [(float(r), 0.075, 0.0, 1.0) for r in np.linspace(0.0, 3.17, 33)]
pos, nrm, uv, idx = revolve(top, 192, 1.0, 1.0)
g.mesh("Turntable Top", pos, nrm, uv, idx, m_top)
edge = []
for k in range(0, 13):
    t = k / 12.0 * math.pi / 2
    edge.append((3.17 + 0.035 * math.sin(t), 0.04 + 0.035 * math.cos(t), math.sin(t), math.cos(t)))
edge.append((3.205, 0.02, 1.0, 0.0))
pos, nrm, uv, idx = revolve(edge, 192, 1.0, 1.0)
g.mesh("Turntable Edge", pos, nrm, uv, idx, m_edge)
led = [(3.208, 0.018, 1.0, 0.0), (3.208, 0.004, 1.0, 0.0)]
pos, nrm, uv, idx = revolve(led, 192, 1.0, 1.0)
g.mesh("Turntable LED", pos, nrm, uv, idx, m_led)
g.gj_name = "Turntable"
g.save(os.path.join(OUT, "Turntable.glb"))

# Temas gölgesi: aracın altına izdüşen yumuşak elips (alfa = koyuluk)
w, h = 256, 512
y, x = np.mgrid[0:h, 0:w].astype(np.float32) + 0.5
nx = (x - w / 2) / (w / 2)
ny = (y - h / 2) / (h / 2)
d = np.sqrt((nx / 0.8) ** 2 + (ny / 0.88) ** 2)
core = np.clip(1.0 - d, 0.0, 1.0)
alpha = np.clip(core * 1.6, 0.0, 1.0) ** 1.6 * 0.9
# Tekerlek altları biraz daha koyu (dört temas noktası)
for wx, wy in ((-0.62, -0.6), (0.62, -0.6), (-0.62, 0.52), (0.62, 0.52)):
    alpha = np.maximum(alpha, np.clip(1.0 - np.hypot((nx - wx) / 0.2, (ny - wy) / 0.12), 0.0, 1.0) ** 2 * 0.95)
img = np.dstack([np.zeros((h, w)), np.zeros((h, w)), np.zeros((h, w)), alpha])
Image.fromarray((img * 255 + 0.5).astype(np.uint8)).save(os.path.join(OUT, "ContactShadow.png"))

# Hotspot simgesi: ince halka + merkez nokta + yumuşak parıltı
s = 128
y, x = np.mgrid[0:s, 0:s].astype(np.float32) + 0.5
r = np.hypot(x - s / 2, y - s / 2) / (s / 2)
ring = np.clip(1.0 - np.abs(r - 0.62) / 0.07, 0.0, 1.0)
dot = np.clip((0.22 - r) / 0.04, 0.0, 1.0)
glow = np.clip(1.0 - r, 0.0, 1.0) ** 2 * 0.35
a = np.clip(np.maximum(np.maximum(ring, dot), glow), 0.0, 1.0)
img = np.dstack([np.ones((s, s)), np.ones((s, s)), np.ones((s, s)), a])
Image.fromarray((img * 255 + 0.5).astype(np.uint8)).save(os.path.join(OUT, "Hotspot.png"))
print("studio assets ->", os.path.normpath(OUT))

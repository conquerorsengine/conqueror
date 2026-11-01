#!/usr/bin/env python3
"""VR template assets made from code (CC0): left / right motion controllers (grip handle, tracking ring with a coloured
light strip, thumbstick, trigger) and the teleport destination marker texture.
Output: Resources/Templates/Projects/vr/Assets/{Models,Textures}."""
import math
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from make_track import GLB  # noqa: E402

ROOT = os.path.join(HERE, "..", "..", "..", "Resources", "Templates", "Projects", "vr", "Assets")


def lathe(profile, segments, transform):
    """profile: [(r, y)] around the local Y axis; transform(p) -> world point. Smooth normals from the profile."""
    pos, nrm, uv, idx = [], [], [], []
    n = len(profile)
    for j in range(segments + 1):
        a = 2 * math.pi * j / segments
        ca, sa = math.cos(a), math.sin(a)
        for i, (r, y) in enumerate(profile):
            # profil normali (iki komşu farkından)
            r0, y0 = profile[max(i - 1, 0)]
            r1, y1 = profile[min(i + 1, n - 1)]
            dr, dy = r1 - r0, y1 - y0
            nr, ny = dy, -dr
            l = math.hypot(nr, ny) or 1.0
            p = np.array([r * ca, y, r * sa])
            q = np.array([nr / l * ca, ny / l, nr / l * sa])
            pos.append(tuple(transform(p)))
            nrm.append(tuple(transform(q, direction=True)))
            uv.append((j / segments, i / max(n - 1, 1)))
    for j in range(segments):
        for i in range(n - 1):
            a = j * n + i
            b = a + n
            idx += [a, a + 1, b, a + 1, b + 1, b]
    return pos, nrm, uv, idx


def torus(R, r, segs, sides, transform):
    pos, nrm, uv, idx = [], [], [], []
    for j in range(segs + 1):
        u = 2 * math.pi * j / segs
        for i in range(sides + 1):
            v = 2 * math.pi * i / sides
            cx, cz = math.cos(u), math.sin(u)
            p = np.array([(R + r * math.cos(v)) * cx, r * math.sin(v), (R + r * math.cos(v)) * cz])
            q = np.array([math.cos(v) * cx, math.sin(v), math.cos(v) * cz])
            pos.append(tuple(transform(p)))
            nrm.append(tuple(transform(q, direction=True)))
            uv.append((j / segs, i / sides))
    for j in range(segs):
        for i in range(sides):
            a = j * (sides + 1) + i
            b = a + sides + 1
            idx += [a, b, a + 1, a + 1, b, b + 1]
    return pos, nrm, uv, idx


def xf(rot_x_deg, offset):
    t = math.radians(rot_x_deg)
    c, s = math.cos(t), math.sin(t)
    R = np.array([[1, 0, 0], [0, c, -s], [0, s, c]])
    off = np.array(offset)

    def f(p, direction=False):
        q = R @ np.asarray(p, float)
        return q if direction else q + off
    return f


def controller(path, ring_color):
    g = GLB()
    body = g.material("Body", color=(0.09, 0.095, 0.11), roughness=0.45, metallic=0.1)
    trim = g.material("Trim", color=(0.6, 0.62, 0.66), roughness=0.3, metallic=0.9)
    light = g.material("Ring Light", color=(0.02, 0.02, 0.02), roughness=0.4)
    g.materials[light]["emissiveFactor"] = list(ring_color)
    g.materials[light]["extensions"] = {"KHR_materials_emissive_strength": {"emissiveStrength": 5.0}}
    # Kabza: aşağı doğru, el gibi hafif öne yatık (yerel -Z ileri, +Y yukarı); tepesi kontrolcü merkezinde
    grip = [(0.0, -0.13), (0.016, -0.128), (0.021, -0.12), (0.023, -0.08), (0.022, -0.03), (0.02, 0.0), (0.014, 0.012), (0.0, 0.016)]
    g.mesh("Grip", *lathe(grip, 24, xf(-22, (0, 0, 0.02))), body)
    # İzleme halkası: tepede, öne açılı
    g.mesh("Ring", *torus(0.045, 0.0075, 48, 12, xf(58, (0, 0.02, -0.035))), body)
    g.mesh("Ring Light", *torus(0.045, 0.0032, 48, 8, xf(58, (0, 0.0215, -0.0355))), light)
    # Başparmak çubuğu ve tetik
    stick = [(0.0, 0.0), (0.006, 0.0), (0.006, 0.012), (0.011, 0.014), (0.011, 0.019), (0.0, 0.02)]
    g.mesh("Thumbstick", *lathe(stick, 16, xf(-12, (0.0, 0.012, -0.004))), trim)
    trig = [(0.0, -0.02), (0.009, -0.018), (0.01, 0.0), (0.007, 0.012), (0.0, 0.014)]
    g.mesh("Trigger", *lathe(trig, 12, xf(-70, (0.0, -0.012, -0.028))), trim)
    g.save(path)


os.makedirs(os.path.join(ROOT, "Models"), exist_ok=True)
controller(os.path.join(ROOT, "Models", "Controller_Left.glb"), (0.25, 0.65, 1.0))
controller(os.path.join(ROOT, "Models", "Controller_Right.glb"), (1.0, 0.55, 0.15))

# Işınlanma hedefi: halka + yön oku (alfa kırpmalı, ışıksız)
s = 256
y, x = np.mgrid[0:s, 0:s].astype(np.float32) + 0.5
r = np.hypot(x - s / 2, y - s / 2) / (s / 2)
ring = np.clip(1.0 - np.abs(r - 0.82) / 0.07, 0, 1)
inner = np.clip(1.0 - np.abs(r - 0.55) / 0.025, 0, 1) * 0.6
ang = np.arctan2(y - s / 2, x - s / 2)
arrow = (np.abs(x - s / 2) < (y - s * 0.12) * 0.32) & (y > s * 0.12) & (y < s * 0.36)
a = np.clip(np.maximum(np.maximum(ring, inner), arrow.astype(np.float32)), 0, 1)
img = np.dstack([np.ones((s, s)), np.ones((s, s)), np.ones((s, s)), a])
os.makedirs(os.path.join(ROOT, "Textures"), exist_ok=True)
Image.fromarray((img * 255 + 0.5).astype(np.uint8)).save(os.path.join(ROOT, "Textures", "teleport_marker.png"))
print("vr assets ->", os.path.normpath(ROOT))

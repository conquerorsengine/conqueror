#!/usr/bin/env python3
"""Top Down şablonunun zemin imleci (Decal): ince parlak halka + iç yumuşak dolgu. Çıktı: Assets/Textures/cursor_ring.png"""
import os
import sys

import numpy as np
from PIL import Image

out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..",
                                                         "Resources", "Templates", "Projects", "topdown", "Assets", "Textures")
os.makedirs(out, exist_ok=True)
s = 256
yy, xx = np.mgrid[0:s, 0:s] / (s - 1) * 2 - 1
r = np.sqrt(xx * xx + yy * yy)
ring = np.exp(-((r - 0.82) / 0.05) ** 2)
fill = np.clip(1 - r / 0.82, 0, 1) ** 2 * 0.18
a = np.clip(ring + fill, 0, 1) * (r < 1.0)
img = np.dstack([np.full((s, s), 0.55), np.full((s, s), 0.85), np.ones((s, s)), a])
Image.fromarray((img * 255).astype(np.uint8)).save(os.path.join(out, "cursor_ring.png"))
print("cursor_ring.png")

# Tıklama efekti parçacığı: yumuşak nokta
dot = np.clip(1 - r, 0, 1) ** 2
img = np.dstack([np.ones((s, s)), np.ones((s, s)), np.ones((s, s)), dot])
Image.fromarray((img * 255).astype(np.uint8)).save(os.path.join(out, "soft_dot.png"))
print("soft_dot.png")

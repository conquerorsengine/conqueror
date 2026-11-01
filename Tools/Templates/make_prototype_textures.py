#!/usr/bin/env python3
"""Prototype grid textures for the project templates (Unreal-style world grid).

usage: make_prototype_textures.py <out dir>
One texture = 1 m x 1 m: strong 1 m border, 50 cm cross and faint 10 cm subdivisions, and a slight
checker so large floors read at grazing angles. Use with MeshRenderer.WorldAlignedUV (Tiling 1 = 1 m)."""
import os
import sys

import numpy as np
from PIL import Image

META = """TextureImporter:
  Type: Default
  Filter: Trilinear
  Wrap: Repeat
  Mipmaps: true
  Anisotropy: 16
  MaxSize: 4096
  NormalFromGrayscale: false
  Bumpiness: 2
  PixelsPerUnit: 100
"""


def grid(base, line, sub, checker, seed):
    S = 512
    y, x = np.mgrid[0:S, 0:S].astype(np.float32)
    rng = np.random.default_rng(seed)
    v = np.full((S, S), base, np.float32) + (rng.random((S, S)) - 0.5) * 0.01
    # 25 cm dama (çok hafif)
    q = ((x // (S / 4)) + (y // (S / 4))) % 2
    v += (q - 0.5) * checker
    def dist(c):
        dx = np.minimum(x % c, c - (x % c))
        dy = np.minimum(y % c, c - (y % c))
        return np.minimum(dx, dy)
    v -= np.where(dist(S / 10) < 1.2, sub, 0)
    v -= np.where(dist(S / 2) < 1.6, line * 0.55, 0)
    edge = np.minimum(np.minimum(x, S - 1 - x), np.minimum(y, S - 1 - y))
    v -= np.where(edge < 3.0, line, 0)
    return np.clip(v, 0, 1)


def save(out, name, v, tint):
    rgb = np.stack([v * tint[0], v * tint[1], v * tint[2]], -1)
    Image.fromarray((np.clip(rgb, 0, 1) * 255 + 0.5).astype(np.uint8)).save(os.path.join(out, name))
    open(os.path.join(out, name + ".meta"), "w").write(META)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    save(out, "WorldGrid.png", grid(0.70, 0.30, 0.05, 0.025, 3), (1.0, 1.0, 1.01))         # zemin / bloklar
    save(out, "WorldGridDark.png", grid(0.36, 0.14, 0.03, 0.02, 4), (1.0, 1.0, 1.03))     # duvarlar
    save(out, "WorldGridBlue.png", grid(0.80, 0.22, 0.04, 0.0, 5), (0.15, 0.42, 1.0))     # fizik küpleri
    save(out, "WorldGridOrange.png", grid(0.85, 0.22, 0.04, 0.0, 6), (1.0, 0.45, 0.12))   # vurgular


if __name__ == "__main__":
    main()

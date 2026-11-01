#!/usr/bin/env python3
"""Archviz şablonunun budanmış çit dokusu (şimşir): binlerce küçük yaprak, katman katman (arkadakiler koyu, öndekiler
açık), kenarlardan sarılarak döşenebilir. 1024 piksel = 1 metre (WorldAlignedUV, Tiling 1).

Kullanım: make_hedge.py <çıktı .jpg>
"""
import math
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

SIZE = 1024
SS = 2                      # yaprak kenarları için süper örnekleme
PALETTE = [(60, 96, 34), (75, 114, 39), (91, 133, 47), (52, 86, 31), (107, 146, 57), (81, 125, 52), (68, 104, 44)]


def leaf_polygon(cx, cy, length, width, angle):
    """Sivri uçlu yaprak (iki yay): merkez, boy, en, açı."""
    pts = []
    n = 10
    for i in range(n + 1):
        t = i / n
        x = (t - 0.5) * length
        y = math.sin(t * math.pi) * width * 0.5
        pts.append((x, y))
    for i in range(n, -1, -1):
        t = i / n
        x = (t - 0.5) * length
        y = -math.sin(t * math.pi) * width * 0.5
        pts.append((x, y))
    c, s = math.cos(angle), math.sin(angle)
    return [(cx + x * c - y * s, cy + x * s + y * c) for x, y in pts]


def main():
    out = sys.argv[1]
    rng = np.random.default_rng(527)
    n = SIZE * SS
    img = Image.new("RGB", (n, n), (14, 24, 10))
    draw = ImageDraw.Draw(img)
    layers = [(9000, 0.45), (9000, 0.7), (8000, 1.0)]       # (yaprak sayısı, parlaklık): arkadan öne
    for count, bright in layers:
        for _ in range(count):
            cx, cy = rng.uniform(0, n, 2)
            length = rng.uniform(15, 26) * SS
            width = length * rng.uniform(0.42, 0.55)
            angle = rng.uniform(0, math.pi * 2)
            base = np.array(PALETTE[rng.integers(len(PALETTE))], float)
            k = bright * rng.uniform(0.75, 1.2)
            col = tuple(int(min(255, c * k)) for c in base)
            # Kenara yakın yapraklar karşı kenarda da çizilir (döşenebilir)
            for ox in (-n, 0, n):
                for oy in (-n, 0, n):
                    x, y = cx + ox, cy + oy
                    if -40 * SS < x < n + 40 * SS and -40 * SS < y < n + 40 * SS:
                        draw.polygon(leaf_polygon(x, y, length, width, angle), fill=col)
                        # Orta damar: biraz açık ince çizgi
                        c, s = math.cos(angle), math.sin(angle)
                        hl = length * 0.4
                        vein = tuple(min(255, int(v * 1.18)) for v in col)
                        draw.line([(x - c * hl, y - s * hl), (x + c * hl, y + s * hl)], fill=vein, width=max(1, SS))
    img = img.resize((SIZE, SIZE), Image.LANCZOS).filter(ImageFilter.UnsharpMask(radius=1.2, percent=40, threshold=2))
    # Kümeler: düşük frekanslı, döşenebilir gölge alanı (yaprak öbekleri arasındaki derinlik)
    field = rng.uniform(0, 1, (16, 16))
    big = np.tile(field, (3, 3))
    blur = np.array(Image.fromarray((big * 255).astype(np.uint8)).resize((SIZE * 3, SIZE * 3), Image.BICUBIC)
                    .filter(ImageFilter.GaussianBlur(40)), float)[SIZE:2 * SIZE, SIZE:2 * SIZE] / 255.0
    shade = 0.72 + 0.42 * (blur - blur.min()) / max(blur.max() - blur.min(), 1e-6)
    arr = np.clip(np.array(img, float) * shade[..., None], 0, 255).astype(np.uint8)
    Image.fromarray(arr).save(out, quality=88)
    print(out)


if __name__ == "__main__":
    main()

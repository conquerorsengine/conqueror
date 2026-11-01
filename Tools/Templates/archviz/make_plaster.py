#!/usr/bin/env python3
"""Archviz şablonunun beyaz boyalı sıva dokusu: Poly Haven painted_plaster_wall (CC0) dokusunun ince ayrıntısı korunur,
rengi nötr, hafif sıcak bir beyaza (doğrusal albedo ~0.7, gerçek beyaz iç cephe boyası) taşınır.

Kullanım: make_plaster.py <painted_plaster_wall.jpg> <çıktı .jpg>
"""
import sys

import numpy as np
from PIL import Image, ImageFilter

WHITE = np.array([0.885, 0.875, 0.855])     # sRGB taban (doğrusal ~0.76 / 0.74 / 0.71)


def main():
    src, out = sys.argv[1:3]
    img = Image.open(src).convert("L")
    lum = np.asarray(img, float) / 255.0
    low = np.asarray(img.filter(ImageFilter.GaussianBlur(48)), float) / 255.0
    detail = lum - low                        # ince sıva dokusu
    broad = low - low.mean()                  # geniş lekeler (boya dalgalanması)
    v = 0.55 * detail + 0.25 * broad
    rgb = np.clip(WHITE[None, None, :] + v[..., None], 0.0, 1.0)
    Image.fromarray((rgb * 255).astype(np.uint8)).save(out, quality=88)
    print(out, rgb.reshape(-1, 3).mean(0).round(3))


if __name__ == "__main__":
    main()

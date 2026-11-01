#!/usr/bin/env python3
"""Crowd şablonunun bina cepheleri (kodla üretilir, dış varlık yok): dünya hizalı UV ile kutu binalara giydirilir.

facade_<renk>.jpg  üst katlar: 4 bölme x 2 kat = 10 m x 6.4 m (Tiling 0.1 / 0.15625). Çerçeveli pencereler (gökyüzü
                   yansıyan koyu cam, bazılarında perde), taş denizlik, kat silmesi, hafif sıva dokusu.
shop_<n>.jpg       zemin kat: 4 bölme = 10 m x 4.5 m (Tiling 0.1 / 0.2222). Vitrinler (içeride sıcak ışık ve raflar),
                   cam kapı, tabela bandı, taş süpürgelik.

Kullanım: make_facades.py <çıktı Textures klasörü>
"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

PX_PER_M = 102.4            # 10 m = 1024 px


def m(v):
    return int(round(v * PX_PER_M))


def plaster(w, h, base, rng, amount=10):
    noise = rng.normal(0, 1, (h // 8 + 1, w // 8 + 1))
    noise = np.array(Image.fromarray(((noise * 0.5 + 0.5).clip(0, 1) * 255).astype(np.uint8)).resize((w, h), Image.BICUBIC),
                     float) / 255.0 - 0.5
    fine = rng.normal(0, 0.5, (h, w))
    img = np.array(base, float)[None, None, :] + (noise[..., None] * amount + fine[..., None] * 3.0)
    return img.clip(0, 255).astype(np.uint8)


def glass(img, x0, y0, x1, y1, rng, curtain=None, warm=0.0):
    """Gökyüzü yansıyan koyu cam: üstte açık, altta koyu; çapraz parlama; isteğe bağlı perde / içeride sıcak ışık.
    Cam kendi görüntüsünde çizilip yapıştırılır: parlama çerçevenin dışına taşmaz."""
    w, h = x1 - x0, y1 - y0
    pane = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(pane)
    for y in range(h):
        t = y / max(h - 1, 1)
        top, bot = np.array([150, 172, 190]), np.array([38, 48, 58])
        c = top * (1 - t) ** 1.6 + bot * (1 - (1 - t) ** 1.6)
        if warm > 0:
            c = c * (1 - warm * 0.55) + np.array([250, 205, 140]) * warm * 0.55 * t
        d.line([(0, y), (w - 1, y)], fill=tuple(int(v) for v in c))
    for k in range(3):
        off = int(w * (0.15 + k * 0.08))
        d.polygon([(off, 0), (off + w // 10, 0), (off - w // 3 + w // 10, h), (off - w // 3, h)],
                  fill=(150, 168, 182) if k == 0 else (120, 138, 152))
    if curtain:
        cw = int(w * 0.22)
        for xa, xb in ((0, cw), (w - cw, w)):
            d.rectangle([xa, 0, xb, h], fill=curtain)
            for xx in range(xa, xb, 6):
                d.line([(xx, 0), (xx, h)], fill=tuple(max(0, c - 18) for c in curtain))
    img.paste(pane, (x0, y0))


def facade(path, wall, frame, seed):
    rng = np.random.default_rng(seed)
    W, H = m(10.0), m(6.4)
    img = Image.fromarray(plaster(W, H, wall, rng))
    d = ImageDraw.Draw(img)
    stone = tuple(min(255, int(c * 1.06 + 12)) for c in wall)
    shade = tuple(int(c * 0.72) for c in wall)
    for floor in range(2):
        fy = H - m(3.2) * (floor + 1)         # katın üst kenarı (görüntü koordinatı)
        # Kat silmesi (üstte ince taş bant + gölge)
        d.rectangle([0, fy, W, fy + m(0.14)], fill=stone)
        d.rectangle([0, fy + m(0.14), W, fy + m(0.19)], fill=shade)
        for bay in range(4):
            cx = m(1.25 + bay * 2.5)
            wx0, wx1 = cx - m(0.65), cx + m(0.65)
            wy1 = fy + m(3.2) - m(0.9)       # pencere alt kenarı katın 0.9 m üstünde
            wy0 = wy1 - m(1.7)
            # Pencere boşluğu (iç gölge), çerçeve, cam, kayıt
            d.rectangle([wx0 - m(0.04), wy0 - m(0.04), wx1 + m(0.04), wy1 + m(0.02)], fill=shade)
            d.rectangle([wx0, wy0, wx1, wy1], fill=frame)
            curtain = None
            r = rng.random()
            if r < 0.35:
                curtain = (int(200 + rng.integers(0, 40)), int(185 + rng.integers(0, 40)), int(160 + rng.integers(0, 40)))
            fw = m(0.07)
            glass(img, wx0 + fw, wy0 + fw, wx1 - fw, wy1 - fw, rng, curtain, warm=0.25 if r > 0.85 else 0.0)
            d.rectangle([cx - m(0.035), wy0 + fw, cx + m(0.035), wy1 - fw], fill=frame)                 # orta kayıt
            d.rectangle([wx0 + fw, wy0 + m(0.55), wx1 - fw, wy0 + m(0.55) + m(0.05)], fill=frame)        # üst kanat
            # Taş denizlik + altında gölge
            d.rectangle([wx0 - m(0.1), wy1, wx1 + m(0.1), wy1 + m(0.08)], fill=stone)
            d.rectangle([wx0 - m(0.1), wy1 + m(0.08), wx1 + m(0.1), wy1 + m(0.12)], fill=shade)
            # Lento (pencere üstü taş)
            d.rectangle([wx0 - m(0.06), wy0 - m(0.16), wx1 + m(0.06), wy0 - m(0.04)], fill=stone)
    img = img.filter(ImageFilter.SMOOTH)
    img.save(path, quality=88)
    print(path)


def shop(path, sign, seed):
    rng = np.random.default_rng(seed)
    W, H = m(10.0), m(4.5)
    wall = (196, 190, 180)
    img = Image.fromarray(plaster(W, H, wall, rng, 6))
    d = ImageDraw.Draw(img)
    dark = (40, 42, 46)
    # Tabela bandı (üstte) ve süpürgelik (altta)
    d.rectangle([0, m(0.35), W, m(1.05)], fill=sign)
    d.rectangle([0, m(1.05), W, m(1.12)], fill=tuple(int(c * 0.6) for c in sign))
    d.rectangle([0, H - m(0.35), W, H], fill=(150, 146, 140))
    d.rectangle([0, H - m(0.38), W, H - m(0.35)], fill=(110, 106, 100))
    for bay in range(4):
        x0, x1 = m(0.18 + bay * 2.5), m(2.32 + bay * 2.5)
        y0, y1 = m(1.3), H - m(0.38)
        d.rectangle([x0, y0, x1, y1], fill=dark)                                # vitrin çerçevesi (koyu metal)
        door = bay == 1
        fw = m(0.06)
        if door:
            # Cam kapı (iki kanat) + üstünde vasistas
            glass(img, x0 + fw, y0 + fw, x1 - fw, y0 + m(0.55), rng)
            d.rectangle([x0 + fw, y0 + m(0.55), x1 - fw, y0 + m(0.6)], fill=dark)
            glass(img, x0 + fw, y0 + m(0.6), (x0 + x1) // 2 - fw // 2, y1 - fw, rng, warm=0.6)
            glass(img, (x0 + x1) // 2 + fw // 2, y0 + m(0.6), x1 - fw, y1 - fw, rng, warm=0.6)
            d.rectangle([(x0 + x1) // 2 - m(0.3), y0 + m(1.6), (x0 + x1) // 2 - m(0.1), y0 + m(1.64)], fill=(190, 190, 186))
            d.rectangle([(x0 + x1) // 2 + m(0.1), y0 + m(1.6), (x0 + x1) // 2 + m(0.3), y0 + m(1.64)], fill=(190, 190, 186))
        else:
            glass(img, x0 + fw, y0 + fw, x1 - fw, y1 - fw, rng, warm=0.75)
            # Vitrin içi: raflar ve ürün siluetleri (sıcak ışıkta)
            for shelf in range(3):
                sy = y0 + m(0.9 + shelf * 0.6)
                d.rectangle([x0 + m(0.15), sy, x1 - m(0.15), sy + m(0.03)], fill=(120, 96, 70))
                for k in range(6):
                    px = x0 + m(0.25) + k * (x1 - x0 - m(0.5)) // 6
                    ph = m(0.15 + rng.random() * 0.25)
                    col = tuple(int(v) for v in rng.integers(60, 200, 3))
                    d.rectangle([px, sy - ph, px + m(0.18), sy], fill=col)
            d.rectangle([(x0 + x1) // 2 - m(0.02), y0 + fw, (x0 + x1) // 2 + m(0.02), y1 - fw], fill=dark)
        # Tente gölgesi (vitrin üstü)
        d.rectangle([x0, y0, x1, y0 + m(0.08)], fill=(30, 30, 32))
    img = img.filter(ImageFilter.SMOOTH)
    img.save(path, quality=88)
    print(path)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    facade(os.path.join(out, "facade_cream.jpg"), (226, 214, 190), (240, 238, 232), 3)
    facade(os.path.join(out, "facade_terracotta.jpg"), (196, 120, 92), (236, 232, 222), 5)
    facade(os.path.join(out, "facade_gray.jpg"), (168, 170, 172), (52, 54, 58), 7)
    shop(os.path.join(out, "shop_a.jpg"), (36, 88, 74), 11)
    shop(os.path.join(out, "shop_b.jpg"), (122, 38, 44), 13)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Editor gizmo icons (CC0, drawn by code): point / spot / directional / area light and camera. White glyph with a dark
outline on transparent background; the editor tints the white with the light colour (like Unity's scene icons).
Output: Resources/Icons/Gizmos/*.png (64 x 64)."""
import math
import os

from PIL import Image, ImageDraw, ImageFilter

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Resources", "Icons", "Gizmos")
os.makedirs(OUT, exist_ok=True)
S = 256          # çizim çözünürlüğü (4x), sonra 64'e küçültülür
W = (255, 255, 255, 255)


def finish(img, name):
    # Koyu dış çizgi: glifin alfasını genişletip altına koyu katman
    a = img.split()[3]
    halo = a.filter(ImageFilter.MaxFilter(13)).filter(ImageFilter.GaussianBlur(2))
    base = Image.new("RGBA", img.size, (18, 20, 24, 0))
    base.putalpha(halo.point(lambda v: int(v * 0.85)))
    base.alpha_composite(img)
    base.resize((64, 64), Image.LANCZOS).save(os.path.join(OUT, name + ".png"))


def bulb():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.ellipse([64, 28, 192, 156], fill=W)                       # cam
    d.polygon([(92, 140), (164, 140), (150, 182), (106, 182)], fill=W)
    for k in range(3):                                            # duy
        y = 188 + k * 16
        d.rounded_rectangle([102, y, 154, y + 10], radius=5, fill=W)
    d.ellipse([116, 232, 140, 244], fill=W)
    return img


def sun():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.ellipse([84, 84, 172, 172], fill=W)
    for k in range(8):
        a = k * math.pi / 4
        c, s = math.cos(a), math.sin(a)
        d.line([(128 + c * 64, 128 + s * 64), (128 + c * 108, 128 + s * 108)], fill=W, width=16)
    return img


def spot():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([96, 28, 160, 92], radius=10, fill=W)    # gövde
    d.polygon([(100, 92), (156, 92), (212, 196), (44, 196)], fill=(255, 255, 255, 120))   # ışık konisi
    d.polygon([(100, 92), (156, 92), (176, 128), (80, 128)], fill=W)
    d.line([(44, 214), (212, 214)], fill=W, width=14)
    return img


def area():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([40, 70, 216, 150], radius=10, fill=W)
    for k in range(5):
        x = 64 + k * 32
        d.line([(x, 168), (x - 10, 220)], fill=W, width=12)
    return img


def camera():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([24, 84, 172, 188], radius=16, fill=W)
    d.polygon([(172, 112), (232, 78), (232, 194), (172, 160)], fill=W)
    d.ellipse([44, 36, 92, 84], fill=W)
    d.ellipse([100, 36, 148, 84], fill=W)
    return img


for name, fn in (("light_point", bulb), ("light_directional", sun), ("light_spot", spot), ("light_area", area), ("camera", camera)):
    finish(fn(), name)
print("gizmo icons ->", os.path.normpath(OUT))

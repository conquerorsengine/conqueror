#!/usr/bin/env python3
"""App icons from the vector emblem (Resources/Logo/Conqueror_Emblem.svg, see make_logo_svg.py).

usage: make_icons.py
Writes Resources/Logo/AppIcon/: conqueror_<size>.png (16 ... 1024: white emblem on the brand blue rounded square) and
Conqueror.ico (16 - 256, Windows executables). The editor, the hub and games without their own icon use these.
"""
import os
import sys
import tempfile

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import svgpath  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(HERE))
LOGO = os.path.join(ROOT, "Resources", "Logo")
OUT = os.path.join(LOGO, "AppIcon")
BLUE = (0.16, 0.39, 0.92, 1.0)      # hub vurgu mavisi
SIZES = [16, 24, 32, 48, 64, 128, 256, 512, 1024]


def main():
    os.makedirs(OUT, exist_ok=True)
    emblem = os.path.join(LOGO, "Conqueror_Emblem.svg")
    pngs = {}
    # Amblem kutuya kütle merkeziyle oturur: sınırlayıcı kutuyla ortalanınca ince burun ve kuyruk ağır gövdeyi sağ üste itiyor
    offset = svgpath.center_of_mass_offset(emblem)
    for s in SIZES:
        # Küçük boyutlarda amblem kutuyu daha çok doldurur (16 px'de okunsun), köşe yarıçapı oransal
        pad = 0.10 if s <= 32 else 0.15
        path = os.path.join(OUT, f"conqueror_{s}.png")
        svgpath.render(emblem, path, s, background=BLUE, padding=pad, corner=0.22, offset=offset)
        pngs[s] = path
    Image.open(pngs[256]).save(os.path.join(OUT, "Conqueror.ico"), sizes=[(s, s) for s in (16, 24, 32, 48, 64, 128, 256)])
    print("icons ->", os.path.relpath(OUT, ROOT))


if __name__ == "__main__":
    main()

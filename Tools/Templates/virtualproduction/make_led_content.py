#!/usr/bin/env python3
"""Virtual Production şablonunun LED duvar içerikleri (Poly Haven, CC0 panoramalar).

LED duvarı 270 derecelik kavis (yarıçap 10 m, 7 m yükseklik): duvar dokusu panoramanın 270 derecelik dilimi ve
-9 .. +30 derece enlem bandıdır (sahnenin ortasından bakınca açılar birebir: ağaçlar doğal boyda, panoramadaki zemin
sahne zeminiyle aynı perspektifte buluşur). Tavan dokusu üst yarıkürenin kutupsal izdüşümüdür (silindir kapağının
düzlemsel UV'si: u = +X, v = +Z). Yüksek çözünürlüklü kaynak olarak Poly Haven'ın ton eşlenmiş JPG'si kullanılır
(<kimlik>_tm.jpg); yoksa HDR ton eşlenir. Sahnenin ortam / yansıma ışığı için HDR kopyaları alınır.

Kullanım: make_led_content.py <kaynak klasörü> <çıktı Assets klasörü>
  kaynak klasöründe <kimlik>_tm.jpg (ton eşlenmiş panorama) ve isteğe bağlı <kimlik>/<kimlik>.hdr
"""
import os
import shutil
import sys

import numpy as np
from PIL import Image

SETS = {"Forest": "rainforest_trail", "Sunset": "industrial_sunset_puresky"}


def read_hdr(path):
    """Radiance RGBE (.hdr), RLE dahil."""
    with open(path, "rb") as f:
        data = f.read()
    end = data.index(b"\n\n") + 2
    header = data[:end].decode("latin-1")
    rest = data[end:]
    line_end = rest.index(b"\n")
    dims = rest[:line_end].decode().split()
    h, w = int(dims[1]), int(dims[3])
    pos = line_end + 1
    img = np.zeros((h, w, 4), np.uint8)
    for y in range(h):
        if rest[pos] == 2 and rest[pos + 1] == 2:
            pos += 4
            for c in range(4):
                x = 0
                while x < w:
                    n = rest[pos]
                    pos += 1
                    if n > 128:
                        n -= 128
                        img[y, x:x + n, c] = rest[pos]
                        pos += 1
                    else:
                        img[y, x:x + n, c] = np.frombuffer(rest[pos:pos + n], np.uint8)
                        pos += n
                    x += n
        else:
            img[y] = np.frombuffer(rest[pos:pos + w * 4], np.uint8).reshape(w, 4)
            pos += w * 4
    e = img[..., 3].astype(np.int32)
    scale = np.where(e > 0, np.ldexp(1.0, e - 136), 0.0)
    return img[..., :3].astype(np.float64) * scale[..., None]


# Duvar: panoramanın ortası (boylam 0) duvarın ortasına gelir; CENTER ile kaydırılır (derece, + sağa)
WALL_DEG = 270.0
LAT_TOP, LAT_BOTTOM = 30.0, -9.0
CENTER = {"Forest": -6.0, "Sunset": 0.0}


def tonemapped(src, hid):
    """Yüksek çözünürlüklü LDR panorama (0..1 float, sRGB)."""
    tm = os.path.join(src, hid + "_tm.jpg")
    if os.path.exists(tm):
        Image.MAX_IMAGE_PIXELS = None
        return np.asarray(Image.open(tm).convert("RGB"), np.float32) / 255.0
    rgb = read_hdr(os.path.join(src, hid, hid + ".hdr"))
    lum = 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]
    key = 0.22 / max(np.exp(np.mean(np.log(lum + 1e-4))), 1e-4)
    x = rgb * key
    x = x * (1 + x / 4.0) / (1 + x)                        # Reinhard (beyaz nokta 2)
    return (np.clip(x, 0, 1) ** (1 / 2.2)).astype(np.float32)


def sample(pano, lon_deg, lat_deg):
    """Eşdikdörtgen panoramadan çift doğrusal örnek (boylam -180..180, enlem -90..90)."""
    h, w = pano.shape[:2]
    u = ((lon_deg / 360.0 + 0.5) % 1.0) * w - 0.5
    v = (0.5 - lat_deg / 180.0) * h - 0.5
    u0 = np.floor(u).astype(int)
    v0 = np.clip(np.floor(v).astype(int), 0, h - 2)
    fu = (u - u0)[..., None]
    fv = np.clip(v - v0, 0, 1)[..., None]
    u0 %= w
    u1 = (u0 + 1) % w
    a = pano[v0, u0] * (1 - fu) + pano[v0, u1] * fu
    b = pano[v0 + 1, u0] * (1 - fu) + pano[v0 + 1, u1] * fu
    return a * (1 - fv) + b * fv


def main():
    src, out = sys.argv[1:3]
    os.makedirs(os.path.join(out, "Textures"), exist_ok=True)
    os.makedirs(os.path.join(out, "Sky"), exist_ok=True)
    for name, hid in SETS.items():
        pano = tonemapped(src, hid)
        c = CENTER.get(name, 0.0)
        # Duvar: 5120 piksel genişlik (270 derece), yükseklik açısal oranla
        W = 5120
        H = int(round(W * (LAT_TOP - LAT_BOTTOM) / WALL_DEG))
        lon = c + (np.arange(W)[None, :] + 0.5) / W * WALL_DEG - WALL_DEG / 2.0
        lat = LAT_TOP - (np.arange(H)[:, None] + 0.5) / H * (LAT_TOP - LAT_BOTTOM)
        wall = sample(pano, np.broadcast_to(lon, (H, W)), np.broadcast_to(lat, (H, W)))
        Image.fromarray((np.clip(wall, 0, 1) * 255 + 0.5).astype(np.uint8)).save(os.path.join(out, "Textures", f"LED_{name}.jpg"), quality=90)
        # Tavan: kapak UV'si (u = +X, v = +Z), merkez başucu; kenar duvarın üst kenarının görüldüğü enlem (30 derece)
        S = 1024
        yy, xx = np.mgrid[0:S, 0:S].astype(np.float32) + 0.5
        dx = xx / S * 2 - 1
        dz = yy / S * 2 - 1
        r = np.clip(np.sqrt(dx * dx + dz * dz), 0, 1)
        az = np.degrees(np.arctan2(dx, -dz))              # 0 = duvarın ortası (-Z), + sağ (+X)
        latc = 90.0 - (90.0 - LAT_TOP) * r
        ceil = sample(pano, c + az, latc)
        Image.fromarray((np.clip(ceil, 0, 1) * 255 + 0.5).astype(np.uint8)).save(os.path.join(out, "Textures", f"LED_{name}_Ceiling.jpg"), quality=88)
        hdr = os.path.join(src, hid, hid + ".hdr")
        if os.path.exists(hdr):
            shutil.copy(hdr, os.path.join(out, "Sky", f"{name}.hdr"))
        print(name, wall.shape, "ceiling", ceil.shape)


if __name__ == "__main__":
    main()

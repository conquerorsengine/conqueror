#!/usr/bin/env python3
"""Procedural assets for the Conqueror sample "Ember Trail" (original 2D platformer).

usage: gen_assets.py [Assets dir]

Pixel art (16 px = 1 tile): Wick the candle spirit, enemies, items, three tile sets (16-tile autotile atlases),
parallax backgrounds. Every texture gets a .meta with point filtering (crisp pixels).
Audio: chiptune sound effects and four original music loops (title + one per world).
"""
import math
import os
import sys
import wave

import numpy as np
from PIL import Image

SR = 44100
OUT = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("--") else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Assets")
TEX = os.path.join(OUT, "Textures")
SFX = os.path.join(OUT, "Audio")
MUS = os.path.join(OUT, "Music")
for d in (TEX, SFX, MUS):
    os.makedirs(d, exist_ok=True)

T = 16   # piksel / karo


def hexc(h, a=255):
    h = h.lstrip("#")
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), a)


def save(img, name, wrap="Clamp"):
    path = os.path.join(TEX, name)
    img.save(path, optimize=True)
    with open(path + ".meta", "w") as f:
        f.write("TextureImporter:\n  Type: Sprite\n  Filter: Point\n  Wrap: %s\n  Mipmaps: false\n  Anisotropy: 1\n"
                "  MaxSize: 4096\n  NormalFromGrayscale: false\n  Bumpiness: 2\n  PixelsPerUnit: 16\n" % wrap)


class Canvas:
    def __init__(self, w, h):
        self.img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        self.px = self.img.load()
        self.w, self.h = w, h

    def put(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h and c is not None:
            self.px[x, y] = c

    def rect(self, x0, y0, x1, y1, c):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.put(x, y, c)

    def sprite(self, ox, oy, rows, pal):
        """rows: strings, '.' transparent, other chars -> palette."""
        for j, row in enumerate(rows):
            for i, ch in enumerate(row):
                if ch != ".":
                    self.put(ox + i, oy + j, pal[ch])

    def outline(self, x0, y0, w, h, c):
        """1 px dark outline around opaque pixels inside a cell."""
        src = [[self.px[x0 + i, y0 + j][3] > 0 for i in range(w)] for j in range(h)]
        for j in range(h):
            for i in range(w):
                if src[j][i]:
                    continue
                for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    a, b = i + di, j + dj
                    if 0 <= a < w and 0 <= b < h and src[b][a]:
                        self.put(x0 + i, y0 + j, c)
                        break


OUTLINE = hexc("1b1426")

# ----------------------------------------------------------------------------- Wick (oyuncu)

WICK_PAL = {
    "w": hexc("f4ead2"), "s": hexc("cdbd9a"), "d": hexc("8f7d63"), "e": hexc("2a1d2e"),
    "y": hexc("ffe27a"), "o": hexc("ff9a3c"), "r": hexc("e8502e"), "b": hexc("ff6fa0"), "k": hexc("4a3a2a"),
}

FLAME = [
    ["....y....", "...yoy...", "..yooy...", "..yoroy..", "...oro...", "....r...."],
    ["...y.....", "...oyy...", "..yooy...", "..yoroy..", "...oro...", "....r...."],
    [".....y...", "...yyo...", "...yooy..", "..yoroy..", "...oro...", "....r...."],
]


def wick_frame(c, ox, frame, kind):
    # Gövde: 8 piksel genişlikte mum; alev üstte
    bob = {"idle0": 0, "idle1": 1, "run0": 0, "run1": 1, "run2": 0, "run3": 1, "jump": -1, "fall": 0, "dash": 1, "hit": 0, "land": 2}[kind]
    body_h = 8 - (1 if kind == "land" else 0) + (1 if kind == "jump" else 0)
    top = c.h - 2 - body_h + bob   # kare yüksekliği 20 (alev için üstte boşluk)
    x0 = ox + 4
    lean = 1 if kind in ("dash",) else 0
    for j in range(body_h):
        for i in range(8):
            col = "w"
            if i == 0 or i == 7:
                col = "s"
            if i == 6:
                col = "s" if j > 1 else "w"
            if j == body_h - 1:
                col = "d" if i in (0, 7) else "s"
            c.put(x0 + i + (lean if j < 3 else 0), top + j, WICK_PAL[col])
    # Wax drip
    c.put(x0 + 2, top + 1, WICK_PAL["s"])
    c.put(x0 + 2, top + 2, WICK_PAL["s"])
    # Gözler + yanak
    eye_y = top + 3
    if kind == "hit":
        for (ex, ey) in ((x0 + 2, eye_y), (x0 + 5, eye_y)):
            c.put(ex, ey, WICK_PAL["e"]); c.put(ex - 1, ey - 1, WICK_PAL["e"]); c.put(ex + 1, ey + 1, WICK_PAL["e"])
    else:
        blink = kind == "idle1"
        c.put(x0 + 3 + lean, eye_y, WICK_PAL["e"])
        c.put(x0 + 5 + lean, eye_y, WICK_PAL["e"])
        if not blink:
            c.put(x0 + 3 + lean, eye_y + 1, WICK_PAL["e"])
            c.put(x0 + 5 + lean, eye_y + 1, WICK_PAL["e"])
        c.put(x0 + 2 + lean, eye_y + 2, WICK_PAL["b"])
        c.put(x0 + 6 + lean, eye_y + 2, WICK_PAL["b"])
    # Fitil + alev
    c.put(x0 + 4 + lean, top - 1, WICK_PAL["k"])
    fl = FLAME[{"idle0": 0, "idle1": 1, "run0": 0, "run1": 1, "run2": 2, "run3": 1, "jump": 2, "fall": 1, "dash": 2, "hit": 0, "land": 0}[kind]]
    fy = top - 1 - len(fl)
    fx = x0 + lean
    if kind == "dash":
        fx -= 2
    for j, row in enumerate(fl):
        for i, ch in enumerate(row):
            if ch != ".":
                c.put(fx + i, fy + j, WICK_PAL[ch])
    # Ayaklar
    fy = top + body_h
    feet = {"idle0": (1, 5), "idle1": (1, 5), "run0": (0, 5), "run1": (1, 4), "run2": (2, 6), "run3": (1, 4),
            "jump": (1, 6), "fall": (0, 6), "dash": (0, 6), "hit": (1, 5), "land": (0, 6)}[kind]
    for f in feet:
        c.put(x0 + f, fy, WICK_PAL["d"])
        c.put(x0 + f + 1, fy, WICK_PAL["d"])


def wick_sheet():
    kinds = ["idle0", "idle1", "run0", "run1", "run2", "run3", "jump", "fall", "dash", "hit", "land"]
    c = Canvas(T * len(kinds), T + 4)
    for i, k in enumerate(kinds):
        wick_frame(c, i * T, 0, k)
        c.outline(i * T, 0, T, T + 4, OUTLINE)
    # Satır yüksekliği 20 px (alev için); oyun karesi 16x20
    save(c.img, "wick.png")


# ----------------------------------------------------------------------------- düşmanlar

def enemies_sheet():
    c = Canvas(T * 8, T)
    slug = {"g": hexc("7bc96f"), "G": hexc("4c9a4a"), "e": hexc("1b1426"), "w": hexc("ffffff"), "s": hexc("c8794a"), "S": hexc("8f4f2f")}
    walk = [
        ["................", "................", "................", "................", "................", "........w..w....",
         ".......we.we....", ".....SSs.ss.....", "....SssSs.......", "...SsSsSsSgg....", "...SsssSsggGg...", "..gggggggggGGg..",
         ".gGGGGGGGGGGGGg.", ".gggggggggggggg.", "................", "................"],
        ["................", "................", "................", "................", "................", ".........w..w...",
         "........we.we...", ".....SSs.ss.....", "....SssSs.......", "...SsSsSsSgg....", "...SsssSsggGg...", "...ggggggggGGg..",
         "..gGGGGGGGGGGGg.", "..ggggggggggggg.", "................", "................"],
    ]
    for f in range(2):
        c.sprite(f * T, 0, walk[f], slug)
    moth = {"p": hexc("c9a8ff"), "P": hexc("8a6ad6"), "b": hexc("3d2b5a"), "e": hexc("ffe27a"), "y": hexc("fff3b0")}
    fly = [
        ["................", "................", ".PP..........PP.", "PppP........PppP", "PpppP..bb..PpppP", ".PpppPbeebPpppP.",
         "..PppPbbbbPppP..", "...PPbbbbbbPP...", "....PpbbbbpP....", "...PppPbbPppP...", "..PppP.bb.PppP..", "..PPP......PPP..",
         "................", "................", "................", "................"],
        ["................", "................", "................", "................", ".......bb.......", "......beeb......",
         "....PPbbbbPP....", "..PPppbbbbppPP..", ".PpppPbbbbPpppP.", "PppppPbbbbPppppP", ".PPPP..bb..PPPP.", "................",
         "................", "................", "................", "................"],
    ]
    for f in range(2):
        c.sprite((2 + f) * T, 0, fly[f], moth)
    thorn = {"r": hexc("d9534f"), "R": hexc("8e2f3c"), "s": hexc("f2f0e6"), "e": hexc("1b1426"), "w": hexc("ffffff")}
    tw = [
        ["................", "................", "................", "....s..s..s.....", "...sRs.Rs.sR....", "..sRRRRRRRRRs...",
         ".sRrrrrrrrrrRs..", "..RrrrrrrrrrrRs.", ".sRrrwerrwerrR..", "..RrrrrrrrrrrRs.", ".sRrrrrrrrrrrR..", "..RRRRRRRRRRRR..",
         "...R.R....R.R...", "................", "................", "................"],
        ["................", "................", "................", ".....s..s..s....", "....sRs.Rs.sR...", "..sRRRRRRRRRs...",
         ".sRrrrrrrrrrRs..", "..RrrrrrrrrrrRs.", ".sRrrwerrwerrR..", "..RrrrrrrrrrrRs.", ".sRrrrrrrrrrrR..", "..RRRRRRRRRRRR..",
         "....R.R..R.R....", "................", "................", "................"],
    ]
    for f in range(2):
        c.sprite((4 + f) * T, 0, tw[f], thorn)
    squash = ["................"] * 11 + ["..SSsssSSsss....", ".gggggggggggggg.", ".gGGGGGGGGGGGGg.", "................", "................"]
    c.sprite(6 * T, 0, squash, slug)
    poof = {"w": hexc("ffffff"), "y": hexc("fff3b0")}
    pf = ["................", "................", "....w.....w.....", "......y.y.......", "..w..yyyyy..w...", ".....yy.yy......",
          "...yyy...yyy....", ".....yy.yy......", "..w..yyyyy..w...", "......y.y.......", "....w.....w.....", "................",
          "................", "................", "................", "................"]
    c.sprite(7 * T, 0, pf, poof)
    for f in range(7):
        c.outline(f * T, 0, T, T, OUTLINE)
    save(c.img, "enemies.png")


# ----------------------------------------------------------------------------- nesneler

def items_sheet():
    c = Canvas(T * 13, T)
    em = {"y": hexc("fff3b0"), "o": hexc("ffb347"), "r": hexc("ff6a3d"), "w": hexc("ffffff")}
    for f in range(4):
        s = [3, 4, 3, 2][f]
        cx, cy = f * T + 8, 8
        for y in range(-s - 1, s + 2):
            for x in range(-s - 1, s + 2):
                d = abs(x) + abs(y)
                if d <= s + 1:
                    col = "r" if d == s + 1 else ("o" if d >= s - 1 else "y")
                    c.put(cx + x, cy + y, em[col])
        c.put(cx - 1, cy - 1, em["w"])
    sh = {"y": hexc("ffe27a"), "o": hexc("ffb020"), "w": hexc("ffffff"), "p": hexc("ffd6f0")}
    shard = ["................", ".......w........", "......wyw.......", "......yyy.......", ".....yyoyy......", "..w.yyyoyyy.w...",
             "...yyyyoyyyy....", "....yyyoyyy.....", ".....yyoyy......", ".....yyoyy......", "....yy.o.yy.....", "...yy.....yy....",
             "...y.......y....", "................", "................", "................"]
    c.sprite(4 * T, 0, shard, sh)
    sp = {"g": hexc("6fd36f"), "G": hexc("3a8f4a"), "m": hexc("a0a6b8"), "M": hexc("5d6378"), "p": hexc("ff7ab8"), "P": hexc("c4407e")}
    spring = ["................", "................", "................", "................", "................", "................",
              "...PPPPPPPPPP...", "..PppppppppppP..", "..PPPPPPPPPPPP..", ".....m....m.....", "......M..M......", ".....m....m.....",
              "......M..M......", ".....m....m.....", "...MMMMMMMMMM...", "...MmmmmmmmmM..."]
    spring2 = ["................"] * 9 + ["...PPPPPPPPPP...", "..PppppppppppP..", "..PPPPPPPPPPPP..", ".....mMMMMm.....", ".....MmmmmM.....",
                                         "...MMMMMMMMMM...", "...MmmmmmmmmM..."]
    c.sprite(5 * T, 0, spring, sp)
    c.sprite(6 * T, 0, spring2, sp)
    la = {"m": hexc("5d6378"), "M": hexc("3a3f52"), "g": hexc("cfd6e8"), "y": hexc("ffe27a"), "o": hexc("ff9a3c"), "d": hexc("2e3446")}
    lant = ["......MM........", ".....M..M.......", "....MMMMMM......", "...MggggggM.....", "...MgddddgM.....", "...MgdFFdgM.....",
            "...MgdFFdgM.....", "...MgddddgM.....", "...MggggggM.....", "....MMMMMM......", ".....mmmm.......", "......mm........",
            "......mm........", "......mm........", ".....mmmm.......", "....MMMMMM......"]
    for f, flame in enumerate((None, True)):
        rows = [r.replace("F", "y" if flame else "d") for r in lant]
        c.sprite((7 + f) * T + 2, 0, rows, la)
        if flame:
            c.put((7 + f) * T + 2 + 5, 4, la["o"])
            c.put((7 + f) * T + 2 + 6, 4, la["o"])
    spk = {"s": hexc("e6e9f2"), "S": hexc("8d93a8")}
    spikes = ["................"] * 8 + ["..s...s...s...s.", "..s...s...s...s.", ".sSs.sSs.sSs.sSs", ".sSs.sSs.sSs.sSs",
                                         "sSSSsSSSsSSSsSSS", "sSSSsSSSsSSSsSSS", "SSSSSSSSSSSSSSSS", "SSSSSSSSSSSSSSSS"]
    c.sprite(9 * T, 0, spikes, spk)
    pl = {"w": hexc("c98b4f"), "W": hexc("8a5a30"), "l": hexc("e8b27a")}
    plank = ["llllllllllllllll", "wwwwwwwwwwwwwwww", "wwWwwwwwwwwWwwww", "WWWWWWWWWWWWWWWW", "..W.........W...", "..W.........W...",
             "................"] + ["................"] * 9
    c.sprite(10 * T, 0, plank, pl)
    cr = {"b": hexc("b07d52"), "B": hexc("7a5233"), "l": hexc("d9a674"), "x": hexc("4a2f1d")}
    crumble = ["llllllllllllllll", "lbbbbbbbbbbbbbbB", "lbbbbbbbbbbbbbbB", "lbbBbbbbbbBbbbbB", "lbbbbbbbbbbbbbbB", "lbbbbbbBbbbbbbbB",
               "lbbbbbbbbbbbbbbB", "BBBBBBBBBBBBBBBB", "lbbbbbbbblbbbbbB", "lbbbbbbbblbbbbbB", "lbbBbbbbblbbbBbB", "lbbbbbbbblbbbbbB",
               "lbbbbbbbblbbbbbB", "lbbbbbbbblbbbbbB", "BBBBBBBBBBBBBBBB", "BBBBBBBBBBBBBBBB"]
    c.sprite(11 * T, 0, crumble, cr)
    cracked = [r[:4] + "x" + r[5:9] + "x" + r[10:] if i % 3 else r for i, r in enumerate(crumble)]
    c.sprite(12 * T, 0, cracked, cr)
    for f in range(4, 9):
        c.outline(f * T, 0, T, T, OUTLINE)
    save(c.img, "items.png")


def goal_sheet():
    # Büyük fener: 32x48, sönük / yanık
    c = Canvas(64, 48)
    m, M, g, d = hexc("6b7186"), hexc("3a3f52"), hexc("d9e0f2"), hexc("2e3446")
    y, o, w = hexc("ffe27a"), hexc("ff9a3c"), hexc("fff8e0")
    for f in range(2):
        ox = f * 32
        c.rect(ox + 14, 0, ox + 17, 3, M)
        c.rect(ox + 8, 4, ox + 23, 7, M)
        c.rect(ox + 6, 8, ox + 25, 9, m)
        c.rect(ox + 6, 10, ox + 25, 29, M)
        c.rect(ox + 8, 11, ox + 23, 28, g if f else d)
        if f:
            c.rect(ox + 11, 14, ox + 20, 26, y)
            c.rect(ox + 13, 12, ox + 18, 22, w)
            c.rect(ox + 12, 23, ox + 19, 27, o)
        c.rect(ox + 15, 11, ox + 16, 28, M)
        c.rect(ox + 6, 30, ox + 25, 32, m)
        c.rect(ox + 13, 33, ox + 18, 44, M)
        c.rect(ox + 14, 33, ox + 15, 44, m)
        c.rect(ox + 9, 45, ox + 22, 47, M)
        c.outline(ox, 0, 32, 48, OUTLINE)
    save(c.img, "goal.png")


def platform_sprite():
    c = Canvas(48, 8)
    top, mid, dark, bolt = hexc("e2e6f2"), hexc("a6adc2"), hexc("5d6378"), hexc("ffe27a")
    c.rect(0, 0, 47, 0, top)
    c.rect(0, 1, 47, 5, mid)
    c.rect(0, 6, 47, 7, dark)
    for x in (3, 23, 43):
        c.put(x, 3, bolt)
    save(c.img, "platform.png")


# ----------------------------------------------------------------------------- karo setleri (16'lık otomatik döşeme)

def tileset(name, base, base_dark, base_light, cap, cap_dark, cap_light, speck, seed, cap_style):
    rng = np.random.default_rng(seed)
    c = Canvas(T * 4, T * 4)
    for idx in range(16):
        n, e, s, w = bool(idx & 1), bool(idx & 2), bool(idx & 4), bool(idx & 8)
        ox, oy = (idx % 4) * T, (idx // 4) * T
        for y in range(T):
            for x in range(T):
                v = rng.random()
                col = base if v > 0.18 else (base_dark if v < 0.09 else base_light)
                # Taş/toprak dokusu: yatay katmanlar
                if (y + (x // 5)) % 7 == 0 and rng.random() < 0.6:
                    col = base_dark
                c.put(ox + x, oy + y, col)
        # Benekler (taşlar, kristaller)
        for _ in range(3):
            sx, sy = int(rng.integers(2, 13)), int(rng.integers(5, 13))
            c.put(ox + sx, oy + sy, speck)
            c.put(ox + sx + 1, oy + sy, speck)
        # Kenarlar
        if not w:
            c.rect(ox, oy, ox, oy + T - 1, base_dark)
        if not e:
            c.rect(ox + T - 1, oy, ox + T - 1, oy + T - 1, base_dark)
        if not s:
            c.rect(ox, oy + T - 1, ox + T - 1, oy + T - 1, base_dark)
            c.rect(ox + 1, oy + T - 2, ox + T - 2, oy + T - 2, base_dark)
        if not n:
            # Üst kapak (çimen / yosun / altın kenar)
            for x in range(T):
                h = 3 + (1 if (x * 7 + seed) % 5 == 0 else 0)
                for y in range(h):
                    col = cap_light if y == 0 else (cap if y < h - 1 else cap_dark)
                    c.put(ox + x, oy + y, col)
                if cap_style == "grass" and (x * 3 + seed) % 4 == 0:
                    c.put(ox + x, oy + h, cap_dark)
                if cap_style == "drip" and (x * 5 + seed) % 6 == 0:
                    c.put(ox + x, oy + h, cap)
                    c.put(ox + x, oy + h + 1, cap_dark)
            if not w:
                c.put(ox, oy, None)
            if not e:
                c.put(ox + T - 1, oy, None)
        if not w and not n:
            c.px[ox, oy] = (0, 0, 0, 0)
        if not e and not n:
            c.px[ox + T - 1, oy] = (0, 0, 0, 0)
    save(c.img, name)


def tilesets():
    tileset("tiles_meadow.png", hexc("8a5a3c"), hexc("5e3b28"), hexc("a8714b"), hexc("6fcf4f"), hexc("3f9a3c"), hexc("b2f07a"),
            hexc("c9b8a0"), 3, "grass")
    tileset("tiles_cave.png", hexc("4a4a6a"), hexc("2c2c45"), hexc("5f5f86"), hexc("57d6c4"), hexc("2a8f8a"), hexc("b8fff2"),
            hexc("9b7bff"), 5, "drip")
    tileset("tiles_sky.png", hexc("e8e2d4"), hexc("b5ab98"), hexc("fffaf0"), hexc("ffd45e"), hexc("c89a2e"), hexc("fff2b0"),
            hexc("9fd3ff"), 7, "trim")


# ----------------------------------------------------------------------------- arka planlar (paralaks)

def background(name, w, h, layers):
    """layers: list of (kind, color, params). Transparent where empty; tiles horizontally."""
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px = img.load()
    for kind, col, p in layers:
        rng = np.random.default_rng(p.get("seed", 1))
        if kind == "hills":
            phase = rng.uniform(0, 6.28, 3)
            for x in range(w):
                u = x / w * 2 * math.pi
                top = p["base"] - (p["amp"] * (0.6 * math.sin(u * p["freq"] + phase[0]) + 0.3 * math.sin(u * p["freq"] * 2 + phase[1])
                                                + 0.1 * math.sin(u * p["freq"] * 5 + phase[2])))
                for y in range(int(top), h):
                    px[x, y] = col
        elif kind == "trees":
            for k in range(p["count"]):
                tx = int(k * w / p["count"] + rng.integers(0, w // p["count"] // 2 + 1))
                th = int(rng.integers(p["hmin"], p["hmax"]))
                trunk = p["trunk"]
                for y in range(h - th // 3, h):
                    for x in range(tx - 1, tx + 2):
                        px[x % w, y] = trunk
                r = th // 3
                cy = h - th // 3 - r // 2
                for y in range(-r, r + 1):
                    for x in range(-r, r + 1):
                        if x * x + (y * 1.2) ** 2 <= r * r:
                            px[(tx + x) % w, min(h - 1, max(0, cy + y))] = col
        elif kind == "clouds":
            for k in range(p["count"]):
                cx, cy = int(rng.integers(0, w)), int(rng.integers(p["ymin"], p["ymax"]))
                for b in range(4):
                    bx, by, r = cx + b * 7 - 10, cy + int(rng.integers(-3, 3)), int(rng.integers(5, 10))
                    for y in range(-r, r + 1):
                        for x in range(-r, r + 1):
                            if x * x + y * y <= r * r and 0 <= by + y < h:
                                px[(bx + x) % w, by + y] = col
        elif kind == "spikes":   # sarkıtlar / dikitler
            for k in range(p["count"]):
                sx = int(rng.integers(0, w))
                sh = int(rng.integers(p["hmin"], p["hmax"]))
                sw = max(2, sh // 4)
                for j in range(sh):
                    half = int(sw * (1 - j / sh))
                    y = j if p["top"] else h - 1 - j
                    for x in range(-half, half + 1):
                        px[(sx + x) % w, y] = col
        elif kind == "crystals":
            for k in range(p["count"]):
                sx, sy = int(rng.integers(0, w)), int(rng.integers(h // 2, h))
                for j in range(6):
                    for x in range(-(2 - abs(j - 3) // 2), 3 - abs(j - 3) // 2):
                        px[(sx + x) % w, max(0, sy - j)] = col
        elif kind == "ruins":
            for k in range(p["count"]):
                cx = int(k * w / p["count"] + rng.integers(0, 12))
                ch = int(rng.integers(p["hmin"], p["hmax"]))
                for y in range(h - ch, h):
                    for x in range(cx, cx + 6):
                        px[x % w, y] = col
                for x in range(cx - 2, cx + 8):
                    px[x % w, h - ch] = col
                    px[x % w, h - ch + 1] = col
    # Üst iki satır saydam: dikey tekrar (Tiled) sınırında alt satırın ince bir çizgi olarak taşmasını önler
    for x in range(w):
        for y in range(2):
            px[x, y] = (0, 0, 0, 0)
    img.save(os.path.join(TEX, name), optimize=True)
    with open(os.path.join(TEX, name) + ".meta", "w") as f:
        f.write("TextureImporter:\n  Type: Sprite\n  Filter: Point\n  Wrap: Repeat\n  Mipmaps: false\n  Anisotropy: 1\n"
                "  MaxSize: 4096\n  NormalFromGrayscale: false\n  Bumpiness: 2\n  PixelsPerUnit: 16\n")


def backgrounds():
    W, H = 384, 160
    background("bg_meadow_far.png", W, H, [("clouds", hexc("ffffff", 200), dict(count=7, ymin=12, ymax=50, seed=2)),
                                           ("hills", hexc("9fd6a8"), dict(base=110, amp=28, freq=2, seed=3))])
    background("bg_meadow_near.png", W, H, [("hills", hexc("5fae5c"), dict(base=128, amp=16, freq=3, seed=4)),
                                            ("trees", hexc("3f8f4a"), dict(count=9, hmin=50, hmax=80, trunk=hexc("6b4a32"), seed=5))])
    background("bg_cave_far.png", W, H, [("spikes", hexc("2f2a4a"), dict(count=26, hmin=20, hmax=60, top=True, seed=6)),
                                         ("spikes", hexc("2f2a4a"), dict(count=22, hmin=16, hmax=46, top=False, seed=7))])
    background("bg_cave_near.png", W, H, [("spikes", hexc("3d3760"), dict(count=14, hmin=30, hmax=80, top=True, seed=8)),
                                          ("crystals", hexc("8f7bff"), dict(count=16, seed=9)),
                                          ("crystals", hexc("57d6c4"), dict(count=12, seed=10))])
    background("bg_sky_far.png", W, H, [("clouds", hexc("ffffff", 230), dict(count=10, ymin=20, ymax=120, seed=11))])
    background("bg_sky_near.png", W, H, [("ruins", hexc("d8c8e8"), dict(count=6, hmin=40, hmax=110, seed=12)),
                                         ("clouds", hexc("ffffff"), dict(count=6, ymin=130, ymax=158, seed=13))])


def ui_textures():
    # Yuvarlak parıltı (fener ışığı, parçacık)
    s = 64
    y, x = np.mgrid[0:s, 0:s]
    r = np.hypot(x + 0.5 - s / 2, y + 0.5 - s / 2) / (s / 2)
    a = np.clip(1 - r, 0, 1) ** 2
    img = np.dstack([np.ones((s, s)), np.ones((s, s)), np.ones((s, s)), a])
    Image.fromarray((img * 255).astype(np.uint8)).save(os.path.join(TEX, "glow.png"))
    # Kalp yerine: alev simgesi (HUD) ve parça simgesi
    c = Canvas(32, 20)
    wick_frame(c, 0, 0, "idle0")
    c.outline(0, 0, 16, 20, OUTLINE)
    save(c.img, "icons.png")
    # Tek tek HUD simgeleri (UI Image tüm dokuyu gösterir)
    items = Image.open(os.path.join(TEX, "items.png"))
    save(items.crop((T, 0, 2 * T, T)), "icon_ember.png")
    save(items.crop((4 * T, 0, 5 * T, T)), "icon_shard.png")
    wick = Image.open(os.path.join(TEX, "wick.png"))
    save(wick.crop((9 * T, 0, 10 * T, T + 4)), "icon_death.png")
    save(wick.crop((0, 0, T, T + 4)), "icon_wick.png")


# ----------------------------------------------------------------------------- ses

def t_axis(d):
    return np.arange(int(d * SR)) / SR


def env(n, a=0.002, d=0.05, s=0.6, r=0.05):
    a_n, d_n, r_n = max(int(a * SR), 1), max(int(d * SR), 1), max(int(r * SR), 1)
    s_n = max(0, n - a_n - d_n - r_n)
    e = np.concatenate([np.linspace(0, 1, a_n, endpoint=False), np.linspace(1, s, d_n, endpoint=False), np.full(s_n, s), np.linspace(s, 0, r_n)])
    return np.pad(e, (0, max(0, n - len(e))))[:n]


def osc(f, d, shape="square", duty=0.5):
    t = t_axis(d)
    f = np.broadcast_to(f, t.shape) if np.ndim(f) else np.full(t.shape, f)
    ph = (np.cumsum(f) / SR) % 1.0
    if shape == "square":
        return np.where(ph < duty, 1.0, -1.0)
    if shape == "tri":
        return 4.0 * np.abs(ph - 0.5) - 1.0
    return np.sin(2 * np.pi * ph)


def noise(d, seed):
    return np.random.default_rng(seed).uniform(-1, 1, int(d * SR))


def write_wav(path, x, gain=0.7, fade=0.002):
    x = np.asarray(x, np.float64)
    x = x / (np.max(np.abs(x)) or 1.0) * gain
    f = min(int(fade * SR), len(x) // 2)
    if f > 0:
        x[:f] *= np.linspace(0, 1, f)
        x[-f:] *= np.linspace(1, 0, f)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes((np.clip(x, -1, 1) * 32767).astype(np.int16).tobytes())


def sweep(f0, f1, d, shape="square", duty=0.5):
    return osc(np.geomspace(f0, f1, int(d * SR)), d, shape, duty) * env(int(d * SR), 0.002, d * 0.5, 0.4, d * 0.3)


def sfx():
    w = lambda n, x, g=0.6: write_wav(os.path.join(SFX, n), x, g)
    w("jump.wav", sweep(320, 760, 0.14, "square", 0.25), 0.45)
    w("dash.wav", np.concatenate([noise(0.16, 1) * env(int(0.16 * SR), 0.002, 0.1, 0.3, 0.04) * 0.6]) + sweep(900, 300, 0.16, "tri") * 0.6, 0.5)
    w("stomp.wav", sweep(500, 120, 0.12, "square", 0.5) + noise(0.12, 2) * env(int(0.12 * SR), 0.001, 0.05, 0.1, 0.05) * 0.4, 0.55)
    d = 0.18
    ember = np.zeros(int(d * SR))
    for k, f in enumerate((1318.5, 1975.5)):
        part = osc(f, 0.09, "square", 0.25) * env(int(0.09 * SR), 0.001, 0.03, 0.5, 0.04)
        ember[int(k * 0.06 * SR):int(k * 0.06 * SR) + len(part)] += part
    w("ember.wav", ember, 0.35)
    d = 0.6
    shard = np.zeros(int(d * SR))
    for k, f in enumerate((783.99, 987.77, 1174.66, 1567.98)):
        part = osc(f, 0.25, "square", 0.5) * env(int(0.25 * SR), 0.001, 0.1, 0.5, 0.1)
        o = int(k * 0.08 * SR)
        shard[o:o + len(part)] += part[:len(shard) - o]
    w("shard.wav", shard, 0.45)
    w("spring.wav", sweep(200, 1200, 0.25, "tri") + sweep(400, 1600, 0.25, "square", 0.125) * 0.3, 0.5)
    w("checkpoint.wav", np.concatenate([osc(f, 0.1, "square", 0.5) * env(int(0.1 * SR), 0.001, 0.04, 0.6, 0.03) for f in (523.25, 659.25, 783.99, 1046.5)]), 0.4)
    w("pop.wav", sweep(900, 80, 0.4, "square", 0.5) + noise(0.4, 3) * env(int(0.4 * SR), 0.001, 0.2, 0.1, 0.1) * 0.5, 0.55)
    w("crumble.wav", noise(0.3, 4) * env(int(0.3 * SR), 0.005, 0.2, 0.2, 0.08) * osc(30, 0.3, "square"), 0.45)
    w("land.wav", noise(0.06, 5) * env(int(0.06 * SR), 0.001, 0.03, 0.1, 0.02), 0.25)
    w("menu_move.wav", osc(880, 0.05, "square", 0.25) * env(int(0.05 * SR), 0.001, 0.02, 0.4, 0.02), 0.3)
    w("menu_select.wav", np.concatenate([osc(f, 0.07, "square", 0.25) * env(int(0.07 * SR), 0.001, 0.03, 0.5, 0.02) for f in (880, 1318.5)]), 0.35)
    # Bitiş fanfarı
    d = 2.0
    fan = np.zeros(int(d * SR))
    seq = [(523.25, 0.0, 0.12), (659.25, 0.12, 0.12), (783.99, 0.24, 0.12), (1046.5, 0.36, 0.5), (987.77, 0.9, 0.12), (1046.5, 1.02, 0.8)]
    for f, t0, l in seq:
        part = (osc(f, l, "square", 0.5) * 0.6 + osc(f / 2, l, "tri") * 0.5) * env(int(l * SR), 0.003, 0.1, 0.7, 0.1)
        o = int(t0 * SR)
        fan[o:o + len(part)] += part[:len(fan) - o]
    w("goal.wav", fan, 0.5)


NOTES = {"C": -9, "C#": -8, "D": -7, "D#": -6, "E": -5, "F": -4, "F#": -3, "G": -2, "G#": -1, "A": 0, "A#": 1, "B": 2}


def nf(n):
    return 440.0 * 2 ** ((NOTES[n[:-1]] + (int(n[-1]) - 4) * 12) / 12)


def song(path, bpm, prog, melody, lead_duty=0.25, bass_shape="tri", swing=0.0, drums=True, bars=16):
    """Chiptune loop: square lead (melody, 8th notes, '-' hold, '.' rest), triangle bass, noise drums. Original tunes."""
    beat = 60.0 / bpm
    total = bars * 4 * beat
    out = np.zeros(int(total * SR) + SR)

    def place(x, t):
        o = int(t * SR)
        n = min(len(x), len(out) - o)
        if n > 0:
            out[o:o + n] += x[:n]

    for bar in range(bars):
        chord = prog[bar % len(prog)]
        t0 = bar * 4 * beat
        # Bas: kök-beşli
        for k in range(8):
            n = chord[0] if k % 4 != 2 else chord[1]
            l = beat / 2 * 0.85
            place(osc(nf(n), l, bass_shape) * env(int(l * SR), 0.002, 0.05, 0.7, 0.03) * 0.33, t0 + k * beat / 2)
        # Akor arpeji (yumuşak)
        for k in range(16):
            n = chord[2 + k % (len(chord) - 2)]
            l = beat / 4 * 0.7
            place(osc(nf(n), l, "square", 0.125) * env(int(l * SR), 0.001, 0.03, 0.3, 0.02) * 0.05, t0 + k * beat / 4)
        if drums:
            for b in range(4):
                tb = t0 + b * beat
                place(noise(0.05, bar * 8 + b) * env(int(0.05 * SR), 0.001, 0.02, 0.1, 0.02) * 0.08, tb + beat / 2)
                if b % 2 == 0:
                    k = osc(np.geomspace(140, 50, int(0.12 * SR)), 0.12, "sine") * env(int(0.12 * SR), 0.001, 0.08, 0.1, 0.03)
                    place(k * 0.35, tb)
                else:
                    place(noise(0.12, 900 + bar * 4 + b) * env(int(0.12 * SR), 0.001, 0.05, 0.15, 0.05) * 0.16, tb)
    # Melodi
    mel = melody.split()
    step = beat / 2
    i = 0
    total_steps = bars * 8
    while i < total_steps:
        n = mel[i % len(mel)]
        if n in (".", "-"):
            i += 1
            continue
        length = 1
        while (i + length) < total_steps and mel[(i + length) % len(mel)] == "-":
            length += 1
        l = step * length * 0.92
        t = i * step + (swing * step if i % 2 else 0)
        v = osc(nf(n), l, "square", lead_duty) * env(int(l * SR), 0.003, 0.06, 0.65, 0.05)
        vib = 1 + 0.004 * np.sin(2 * np.pi * 5.5 * t_axis(l))
        v2 = osc(nf(n) * vib, l, "square", lead_duty) * env(int(l * SR), 0.003, 0.06, 0.65, 0.05)
        place((v * 0.5 + v2 * 0.5) * 0.16, t)
        i += length
    # Kısa yankı
    d = int(beat * 0.5 * SR)
    wet = np.zeros_like(out)
    wet[d:] += out[:-d] * 0.18
    out = out + wet
    out = out[: int(total * SR)]
    tmp = path + ".tmp.wav"
    write_wav(tmp, out, 0.8, fade=0.0)
    import subprocess
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", tmp, "-codec:a", "libmp3lame", "-b:a", "128k", path], check=True)
    os.remove(tmp)


def music():
    # Akorlar: [bas kök, bas beşli, arpej notaları...]
    song(os.path.join(MUS, "title.mp3"), 96,
         [["C3", "G2", "E4", "G4", "C5"], ["A2", "E2", "E4", "A4", "C5"], ["F2", "C3", "F4", "A4", "C5"], ["G2", "D3", "D4", "G4", "B4"]],
         "E5 - G5 - C6 - B5 A5 G5 - E5 - D5 E5 C5 - - - A4 - C5 - E5 - D5 C5 A4 - G4 - A4 B4 G4 - - -", drums=False)
    song(os.path.join(MUS, "meadow.mp3"), 132,
         [["C3", "G2", "E4", "G4", "C5"], ["F2", "C3", "F4", "A4", "C5"], ["G2", "D3", "D4", "G4", "B4"], ["C3", "G2", "E4", "G4", "C5"],
          ["A2", "E2", "E4", "A4", "C5"], ["F2", "C3", "F4", "A4", "C5"], ["D3", "A2", "D4", "F4", "A4"], ["G2", "D3", "D4", "G4", "B4"]],
         "G4 C5 E5 G5 - E5 C5 . A4 C5 F5 A5 - F5 C5 . B4 D5 G5 B5 - A5 G5 F5 E5 - C5 - G4 - . . "
         "A4 C5 E5 A5 - G5 E5 C5 F5 - E5 - D5 C5 A4 . F4 A4 D5 F5 - E5 D5 C5 B4 - G4 - D5 - . .", swing=0.12)
    song(os.path.join(MUS, "cave.mp3"), 104,
         [["A2", "E2", "A3", "C4", "E4"], ["F2", "C3", "F3", "A3", "C4"], ["G2", "D3", "G3", "B3", "D4"], ["E2", "B2", "E3", "G#3", "B3"]],
         "A4 - . C5 E5 - D5 C5 B4 - . A4 G#4 - E4 - F4 - A4 - C5 - B4 A4 G4 - B4 - D5 - . . "
         "E5 - . D5 C5 - B4 A4 B4 - C5 - D5 - E5 - F5 - E5 D5 C5 - A4 - G#4 - B4 - E5 - - -", lead_duty=0.5, bass_shape="tri")
    song(os.path.join(MUS, "sky.mp3"), 120,
         [["D3", "A2", "F#4", "A4", "D5"], ["B2", "F#2", "F#4", "B4", "D5"], ["G2", "D3", "G4", "B4", "D5"], ["A2", "E3", "E4", "A4", "C#5"]],
         "D5 - F#5 - A5 - F#5 E5 D5 - B4 - D5 - F#5 - G5 - F#5 E5 D5 - B4 - C#5 - E5 - A5 - - - "
         "B5 - A5 F#5 D5 - E5 F#5 G5 - F#5 E5 D5 - B4 - A4 - D5 - F#5 - E5 D5 C#5 - E5 - D5 - - -", lead_duty=0.125)


if __name__ == "__main__":
    wick_sheet()
    enemies_sheet()
    items_sheet()
    goal_sheet()
    platform_sprite()
    tilesets()
    backgrounds()
    ui_textures()
    if "--no-audio" not in sys.argv:
        sfx()
        music()
    print("assets written to", os.path.abspath(OUT))

#!/usr/bin/env python3
"""Kumanda / klavye tuş simgelerini üretir (Resources/Textures/Glyphs/<stil>/<ad>.png).

Simgeler bu betikle çizilir (üçüncü taraf görsel yok); yazılar motorla gelen Open Sans Bold ile.
Stiller: xbox, playstation, nintendo, deck (Steam Deck), keyboard. Mantıksal düğüm adları GLFW standart
yerleşimini izler: a = güney, b = doğu, x = batı, y = kuzey (Nintendo'da etiketler buna göre değişir).

    python3 Tools/Glyphs/make_glyphs.py
"""

import math
import os
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "Resources" / "Textures" / "Glyphs"
FONT = ROOT / "Resources" / "Fonts" / "opensans" / "OpenSans-Bold.ttf"
S = 128            # simge boyutu
SS = 4             # kenar yumuşatma için büyük çizip küçültme

DARK = (38, 40, 46, 255)
DECK = (58, 61, 68, 255)
WHITE = (240, 242, 246, 255)
KEY_FACE = (232, 234, 238, 255)
KEY_EDGE = (150, 154, 162, 255)
KEY_TEXT = (40, 42, 48, 255)


def canvas(w=S, h=S):
    img = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
    return img, ImageDraw.Draw(img)


def save(img, style, name):
    w, h = img.size
    img = img.resize((w // SS, h // SS), Image.LANCZOS)
    d = OUT / style
    d.mkdir(parents=True, exist_ok=True)
    img.save(d / f"{name}.png", optimize=True)


def font(px):
    return ImageFont.truetype(str(FONT), int(px * SS))


def text_center(draw, box, text, px, fill):
    f = font(px)
    l, t, r, b = draw.textbbox((0, 0), text, font=f)
    x0, y0, x1, y1 = box
    draw.text(((x0 + x1 - (r - l)) / 2 - l, (y0 + y1 - (b - t)) / 2 - t), text, font=f, fill=fill)


def fit_px(text, base):
    """Uzun etiketler küçülür."""
    n = len(text)
    return base if n <= 1 else base * 0.62 if n == 2 else base * 0.46 if n == 3 else base * 0.36 if n <= 5 else base * 0.3


def circle_button(style, name, label=None, label_color=WHITE, fill=DARK, shape=None, shape_color=None):
    img, d = canvas()
    m = 6 * SS
    d.ellipse((m, m, S * SS - m, S * SS - m), fill=fill)
    d.ellipse((m, m, S * SS - m, S * SS - m), outline=(255, 255, 255, 40), width=3 * SS)
    c = S * SS / 2
    if shape:
        r = 26 * SS
        w = 9 * SS
        col = shape_color
        if shape == "cross":
            for sgn in (1, -1):
                d.line((c - r, c - sgn * r, c + r, c + sgn * r), fill=col, width=w)
        elif shape == "circle":
            d.ellipse((c - r, c - r, c + r, c + r), outline=col, width=w)
        elif shape == "square":
            d.rectangle((c - r * 0.9, c - r * 0.9, c + r * 0.9, c + r * 0.9), outline=col, width=w)
        elif shape == "triangle":
            pts = [(c, c - r * 1.05), (c + r * 1.05, c + r * 0.8), (c - r * 1.05, c + r * 0.8)]
            d.line(pts + [pts[0]], fill=col, width=w, joint="curve")
    elif label:
        text_center(d, (0, 0, S * SS, S * SS), label, fit_px(label, 64), label_color)
    save(img, style, name)


def pill_button(style, name, label, fill=DARK, w=S, h=S):
    img, d = canvas(w, h)
    m = 10 * SS
    top = h * SS * 0.24
    d.rounded_rectangle((m, top, w * SS - m, h * SS - top), radius=24 * SS, fill=fill)
    d.rounded_rectangle((m, top, w * SS - m, h * SS - top), radius=24 * SS, outline=(255, 255, 255, 40), width=3 * SS)
    text_center(d, (m, top, w * SS - m, h * SS - top), label, fit_px(label, 58), WHITE)
    save(img, style, name)


def trigger_button(style, name, label, fill=DARK):
    img, d = canvas()
    m = 14 * SS
    d.rounded_rectangle((m, 8 * SS, S * SS - m, S * SS - 8 * SS), radius=34 * SS, fill=fill)
    d.rounded_rectangle((m, 8 * SS, S * SS - m, S * SS - 8 * SS), radius=34 * SS, outline=(255, 255, 255, 40), width=3 * SS)
    text_center(d, (m, 8 * SS, S * SS - m, S * SS - 8 * SS), label, fit_px(label, 58), WHITE)
    save(img, style, name)


def stick_button(style, name, label, fill=DARK):
    img, d = canvas()
    m = 6 * SS
    d.ellipse((m, m, S * SS - m, S * SS - m), fill=fill)
    i = 22 * SS
    d.ellipse((i, i, S * SS - i, S * SS - i), outline=(255, 255, 255, 90), width=4 * SS)
    text_center(d, (0, 0, S * SS, S * SS), label, fit_px(label, 56), WHITE)
    save(img, style, name)


def dpad(style, name, highlight, fill=DARK):
    img, d = canvas()
    c = S * SS / 2
    a, b = 18 * SS, 54 * SS
    arms = {"up": (c - a, c - b, c + a, c), "down": (c - a, c, c + a, c + b),
            "left": (c - b, c - a, c, c + a), "right": (c, c - a, c + b, c + a)}
    for arm, box in arms.items():
        d.rounded_rectangle(box, radius=8 * SS, fill=fill)
    d.rectangle((c - a, c - a, c + a, c + a), fill=fill)
    if highlight:
        for h in ([highlight] if highlight != "all" else list(arms)):
            x0, y0, x1, y1 = arms[h]
            p = 6 * SS
            d.rounded_rectangle((x0 + p, y0 + p, x1 - p, y1 - p), radius=6 * SS, fill=WHITE)
    save(img, style, name)


def menu_button(style, name, kind, fill=DARK):
    """start / back: hamburger, iki pencere, artı / eksi."""
    img, d = canvas()
    m = 10 * SS
    d.ellipse((m, m, S * SS - m, S * SS - m), fill=fill)
    c = S * SS / 2
    w = 7 * SS
    if kind == "menu":
        for dy in (-18, 0, 18):
            d.line((c - 26 * SS, c + dy * SS, c + 26 * SS, c + dy * SS), fill=WHITE, width=w)
    elif kind == "view":
        d.rectangle((c - 26 * SS, c - 20 * SS, c + 8 * SS, c + 8 * SS), outline=WHITE, width=w - 2 * SS)
        d.rectangle((c - 8 * SS, c - 6 * SS, c + 26 * SS, c + 22 * SS), outline=WHITE, width=w - 2 * SS)
    elif kind == "plus":
        d.line((c - 26 * SS, c, c + 26 * SS, c), fill=WHITE, width=w + 2 * SS)
        d.line((c, c - 26 * SS, c, c + 26 * SS), fill=WHITE, width=w + 2 * SS)
    elif kind == "minus":
        d.line((c - 26 * SS, c, c + 26 * SS, c), fill=WHITE, width=w + 2 * SS)
    elif kind == "dots":
        for dx in (-22, 0, 22):
            d.ellipse((c + (dx - 6) * SS, c - 6 * SS, c + (dx + 6) * SS, c + 6 * SS), fill=WHITE)
    else:
        text_center(d, (0, 0, S * SS, S * SS), kind, fit_px(kind, 60), WHITE)
    save(img, style, name)


def gamepad_style(style):
    fill = DECK if style == "deck" else DARK
    if style == "xbox":
        faces = {"a": ("A", (96, 192, 72, 255)), "b": ("B", (226, 70, 60, 255)), "x": ("X", (60, 130, 226, 255)), "y": ("Y", (238, 196, 50, 255))}
        for n, (lab, col) in faces.items():
            circle_button(style, n, lab, col, fill)
    elif style == "playstation":
        shapes = {"a": ("cross", (110, 160, 240, 255)), "b": ("circle", (236, 90, 90, 255)),
                  "x": ("square", (226, 130, 200, 255)), "y": ("triangle", (90, 200, 160, 255))}
        for n, (shape, col) in shapes.items():
            circle_button(style, n, fill=fill, shape=shape, shape_color=col)
    elif style == "nintendo":
        for n, lab in {"a": "B", "b": "A", "x": "Y", "y": "X"}.items():
            circle_button(style, n, lab, WHITE, fill)
    else:  # deck: tek renk A B X Y
        for n, lab in {"a": "A", "b": "B", "x": "X", "y": "Y"}.items():
            circle_button(style, n, lab, WHITE, fill)

    shoulder = {"xbox": ("LB", "RB", "LT", "RT", "LS", "RS"), "playstation": ("L1", "R1", "L2", "R2", "L3", "R3"),
                "nintendo": ("L", "R", "ZL", "ZR", "LS", "RS"), "deck": ("L1", "R1", "L2", "R2", "L3", "R3")}[style]
    pill_button(style, "lb", shoulder[0], fill)
    pill_button(style, "rb", shoulder[1], fill)
    trigger_button(style, "lt", shoulder[2], fill)
    trigger_button(style, "rt", shoulder[3], fill)
    stick_button(style, "ls", shoulder[4], fill)
    stick_button(style, "rs", shoulder[5], fill)
    stick_button(style, "left_stick", "L", fill)
    stick_button(style, "right_stick", "R", fill)

    start, back, guide = {"xbox": ("menu", "view", "X"), "playstation": ("menu", "view", "PS"),
                          "nintendo": ("plus", "minus", "H"), "deck": ("menu", "view", "STEAM")}[style]
    menu_button(style, "start", start, fill)
    menu_button(style, "back", back, fill)
    menu_button(style, "guide", guide, fill)
    for h in ("up", "down", "left", "right"):
        dpad(style, f"dpad_{h}", h, fill)
    dpad(style, "dpad", None, fill)
    if style == "deck":
        for n in ("l4", "r4", "l5", "r5"):
            pill_button(style, n, n.upper(), fill)
        menu_button(style, "quick_access", "dots", fill)


def keycap(name, label, width=S):
    img, d = canvas(width, S)
    m = 8 * SS
    d.rounded_rectangle((m, m + 6 * SS, width * SS - m, S * SS - m), radius=18 * SS, fill=KEY_EDGE)
    d.rounded_rectangle((m, m, width * SS - m, S * SS - m - 10 * SS), radius=18 * SS, fill=KEY_FACE)
    box = (m, m, width * SS - m, S * SS - m - 10 * SS)
    if label.startswith("arrow_"):
        c = ((box[0] + box[2]) / 2, (box[1] + box[3]) / 2)
        r = 22 * SS
        ang = {"arrow_up": -90, "arrow_down": 90, "arrow_left": 180, "arrow_right": 0}[label]
        pts = []
        for k, a in enumerate((0, 140, 220)):
            t = math.radians(ang + a)
            rr = r if k == 0 else r * 0.9
            pts.append((c[0] + rr * math.cos(t), c[1] + rr * math.sin(t)))
        d.polygon(pts, fill=KEY_TEXT)
    else:
        # Tuş yazısı küçük boyutta da okunsun: geniş tuşlarda kısa etiketler büyük kalır
        n = len(label)
        px = 72 if n == 1 else 54 if n == 2 else 44 if n == 3 else 40 if width > S else 34
        text_center(d, box, label, px, KEY_TEXT)
    save(img, "keyboard", name)


def mouse(name, button):
    img, d = canvas()
    x0, y0, x1, y1 = 30 * SS, 10 * SS, 98 * SS, 118 * SS
    d.rounded_rectangle((x0, y0, x1, y1), radius=34 * SS, fill=KEY_FACE, outline=KEY_EDGE, width=4 * SS)
    mid = (x0 + x1) / 2
    split = y0 + 46 * SS
    d.line((x0, split, x1, split), fill=KEY_EDGE, width=4 * SS)
    d.line((mid, y0, mid, split), fill=KEY_EDGE, width=4 * SS)
    hl = (226, 90, 70, 255)
    if button == "left":
        d.rounded_rectangle((x0 + 5 * SS, y0 + 5 * SS, mid - 3 * SS, split - 3 * SS), radius=20 * SS, fill=hl)
    elif button == "right":
        d.rounded_rectangle((mid + 3 * SS, y0 + 5 * SS, x1 - 5 * SS, split - 3 * SS), radius=20 * SS, fill=hl)
    elif button == "middle":
        d.rounded_rectangle((mid - 7 * SS, y0 + 14 * SS, mid + 7 * SS, y0 + 36 * SS), radius=6 * SS, fill=hl)
    save(img, "keyboard", name)


def keyboard_style():
    for c in "abcdefghijklmnopqrstuvwxyz":
        keycap(f"key_{c}", c.upper())
    for c in "0123456789":
        keycap(f"key_{c}", c)
    for i in range(1, 13):
        keycap(f"key_f{i}", f"F{i}")
    named = {"space": ("SPACE", 256), "enter": ("ENTER", 192), "escape": ("ESC", S), "tab": ("TAB", 160), "shift": ("SHIFT", 192),
             "ctrl": ("CTRL", 160), "alt": ("ALT", 160), "backspace": ("BKSP", 192), "delete": ("DEL", S), "up": ("arrow_up", S),
             "down": ("arrow_down", S), "left": ("arrow_left", S), "right": ("arrow_right", S)}
    for n, (lab, w) in named.items():
        keycap(f"key_{n}", lab, w)
    mouse("mouse_left", "left")
    mouse("mouse_right", "right")
    mouse("mouse_middle", "middle")
    mouse("mouse", None)


def main():
    if not FONT.exists():
        sys.exit(f"font missing: {FONT}")
    for style in ("xbox", "playstation", "nintendo", "deck"):
        gamepad_style(style)
    keyboard_style()
    count = sum(len(files) for _, _, files in os.walk(OUT))
    print(f"{count} glyphs -> {OUT}")


if __name__ == "__main__":
    main()

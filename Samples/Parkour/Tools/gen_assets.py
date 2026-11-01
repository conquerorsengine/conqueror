#!/usr/bin/env python3
"""Procedural assets for the Conqueror Parkour sample.

usage: gen_parkour_assets.py <project Assets dir>

Textures: tileable concrete/metal surfaces, skyscraper windows, glow/spark sprites.
Audio: footsteps, jump/land, wall-run wind loop, slide, checkpoint, finish fanfare, bounce pad, fall, UI.
Music: an original ambient synthwave loop written for this sample.
"""
import math
import os
import subprocess
import sys
import wave

import numpy as np
from PIL import Image, ImageFilter

SR = 44100
OUT = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("--") else "Assets"
TEX = os.path.join(OUT, "Textures")
SFX = os.path.join(OUT, "Audio")
MUS = os.path.join(OUT, "Music")
for d in (TEX, SFX, MUS):
    os.makedirs(d, exist_ok=True)
rng = np.random.default_rng(1234)


# ----------------------------------------------------------------------------- texture helpers

def save_rgb(path, rgb):
    img = (np.clip(rgb, 0, 1) * 255 + 0.5).astype(np.uint8)
    Image.fromarray(img).save(path, optimize=True)


def save_rgba(path, rgb, alpha):
    if rgb.ndim == 2:
        rgb = np.stack([rgb] * 3, axis=-1)
    img = np.dstack([np.clip(rgb, 0, 1), np.clip(alpha, 0, 1)[..., None]])
    Image.fromarray((img * 255 + 0.5).astype(np.uint8)).save(path, optimize=True)


def tileable_noise(size, scale, octaves=4, seed=0):
    """Sum of random-phase sinusoids on integer frequencies -> perfectly tileable."""
    r = np.random.default_rng(seed)
    y, x = np.mgrid[0:size, 0:size].astype(np.float32) / size
    out = np.zeros((size, size), np.float32)
    amp = 1.0
    for o in range(octaves):
        f = scale * (2 ** o)
        for _ in range(6):
            fx, fy = r.integers(-f, f + 1), r.integers(-f, f + 1)
            ph = r.uniform(0, 2 * math.pi)
            out += amp * np.sin(2 * math.pi * (fx * x + fy * y) + ph)
        amp *= 0.5
    out -= out.min()
    out /= max(out.max(), 1e-6)
    return out


def concrete_texture(size=512):
    """Light concrete with a 1 m panel seam (texture = 2x2 m tile)."""
    n1 = tileable_noise(size, 3, 5, seed=1)
    n2 = tileable_noise(size, 16, 3, seed=2)
    base = 0.50 + 0.10 * (n1 - 0.5) + 0.05 * (n2 - 0.5)
    speck = (rng.random((size, size)) > 0.985).astype(np.float32) * rng.uniform(-0.08, 0.06, (size, size))
    base += speck
    y, x = np.mgrid[0:size, 0:size].astype(np.float32)
    seam = np.minimum(np.minimum(x % (size / 2), (size / 2) - (x % (size / 2))), np.minimum(y % (size / 2), (size / 2) - (y % (size / 2))))
    base *= 1.0 - 0.32 * np.exp(-(seam / 1.6) ** 2)
    rgb = np.stack([base * 0.98, base * 0.99, base * 1.04], -1)
    save_rgb(os.path.join(TEX, "concrete.png"), rgb)


def metal_texture(size=256):
    """Dark brushed metal with diamond plate bumps."""
    n = tileable_noise(size, 2, 4, seed=5)
    y, x = np.mgrid[0:size, 0:size].astype(np.float32)
    brushed = 0.5 + 0.5 * np.sin(2 * math.pi * (y / size) * 64 + n * 4)
    cell = size / 8
    u = (x % cell) / cell - 0.5
    v = (y % cell) / cell - 0.5
    bump = np.exp(-((u + v) ** 2) / 0.006) * (np.abs(u - v) < 0.3)
    val = 0.20 + 0.05 * brushed + 0.10 * bump + 0.04 * (n - 0.5)
    rgb = np.stack([val, val * 1.02, val * 1.08], -1)
    save_rgb(os.path.join(TEX, "metal.png"), rgb)


def windows_texture(size=1024):
    """Skyscraper facade tile = 12 m x 24 m (8 window columns x 8 floors).

    Dark structure and glass, a mullion in every window, spandrel bands between floors and
    randomly lit offices (some floors busy, some dark). The towers use it with EmissionFromTexture,
    so only the lit windows glow.
    """
    cols, floors = 8, 8
    cw, fh = size // cols, size // floors
    img = np.zeros((size, size, 3), np.float32)
    img[:] = (0.030, 0.032, 0.040)
    for f in range(floors):
        mood = rng.random()
        p_lit = 0.10 if mood < 0.35 else (0.78 if mood > 0.82 else 0.38)
        for c in range(cols):
            x0, x1 = c * cw + int(cw * 0.07), (c + 1) * cw - int(cw * 0.07)
            y0, y1 = f * fh + int(fh * 0.20), (f + 1) * fh - int(fh * 0.06)
            gy = np.linspace(0.0, 1.0, y1 - y0, dtype=np.float32)[:, None, None]
            if rng.random() < p_lit:
                warm = rng.random() < 0.72
                col = np.array([1.0, 0.74, 0.42]) if warm else np.array([0.62, 0.80, 1.0])
                col = col * rng.uniform(0.35, 1.0)
                # Perde / tavan lambası etkisi: üstte biraz daha parlak
                img[y0:y1, x0:x1] = col * (1.0 - 0.3 * gy)
            else:
                glass = np.array([0.045, 0.050, 0.068])
                img[y0:y1, x0:x1] = glass * (1.15 - 0.3 * gy)
            xm = (x0 + x1) // 2
            img[y0:y1, xm - 3:xm + 3] = (0.030, 0.032, 0.040)
        # Kat arası bant (spandrel)
        img[f * fh:f * fh + int(fh * 0.20) - 6, :] = (0.022, 0.023, 0.028)
    save_rgb(os.path.join(TEX, "windows.png"), img)


def glow_texture(size=128):
    y, x = np.mgrid[0:size, 0:size].astype(np.float32)
    r = np.hypot((x + 0.5) / size - 0.5, (y + 0.5) / size - 0.5) * 2.0
    save_rgba(os.path.join(TEX, "glow.png"), np.ones((size, size)), np.clip(1.0 - r, 0, 1) ** 2.2)


def spark_texture(size=64):
    y, x = np.mgrid[0:size, 0:size].astype(np.float32)
    xx, yy = (x + 0.5) / size - 0.5, (y + 0.5) / size - 0.5
    r = np.hypot(xx, yy) * 2.0
    core = np.clip(1.0 - r * 2.2, 0, 1) ** 1.5
    star = np.exp(-np.abs(xx) * 40) * np.clip(1 - np.abs(yy) * 2.2, 0, 1) + np.exp(-np.abs(yy) * 40) * np.clip(1 - np.abs(xx) * 2.2, 0, 1)
    save_rgba(os.path.join(TEX, "spark.png"), np.ones((size, size)), np.clip(core + star * 0.8, 0, 1))


def crosshair_texture(size=64):
    y, x = np.mgrid[0:size, 0:size].astype(np.float32)
    r = np.hypot(x + 0.5 - size / 2, y + 0.5 - size / 2)
    ring = np.exp(-((r - 13) / 1.6) ** 2)
    dot = np.exp(-(r / 2.2) ** 2)
    save_rgba(os.path.join(TEX, "crosshair.png"), np.ones((size, size)), np.clip(ring * 0.75 + dot, 0, 1))


# ----------------------------------------------------------------------------- audio helpers

def t_axis(d):
    return np.arange(int(d * SR)) / SR


def env(n, a=0.005, d=0.05, s=0.6, r=0.08):
    a_n, d_n, r_n = max(int(a * SR), 1), max(int(d * SR), 1), max(int(r * SR), 1)
    s_n = max(0, n - a_n - d_n - r_n)
    e = np.concatenate([np.linspace(0, 1, a_n, endpoint=False), np.linspace(1, s, d_n, endpoint=False), np.full(s_n, s), np.linspace(s, 0, r_n)])
    return np.pad(e, (0, max(0, n - len(e))))[:n]


def osc_sine(f, d):
    t = t_axis(d)
    f = np.broadcast_to(f, t.shape) if np.ndim(f) else np.full(t.shape, f)
    return np.sin(2 * np.pi * np.cumsum(f) / SR)


def osc_saw(f, d):
    t = t_axis(d)
    f = np.broadcast_to(f, t.shape) if np.ndim(f) else np.full(t.shape, f)
    ph = (np.cumsum(f) / SR) % 1.0
    return 2.0 * ph - 1.0


def osc_square(f, d, duty=0.5):
    t = t_axis(d)
    f = np.broadcast_to(f, t.shape) if np.ndim(f) else np.full(t.shape, f)
    ph = (np.cumsum(f) / SR) % 1.0
    return np.where(ph < duty, 1.0, -1.0)


def noise(d, seed):
    return np.random.default_rng(seed).uniform(-1, 1, int(d * SR))


def lp(x, cutoff):
    from scipy.signal import lfilter
    a = math.exp(-2 * math.pi * cutoff / SR)
    return lfilter([1 - a], [1, -a], x)


def bp(x, lo, hi):
    from scipy.signal import butter, sosfilt
    sos = butter(2, [lo, hi], btype="band", fs=SR, output="sos")
    return sosfilt(sos, x)


def write_wav(path, x, gain=0.8, fade=0.003):
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


def place(buf, x, start):
    o = int(start * SR)
    n = min(len(x), len(buf) - o)
    if n > 0:
        buf[o:o + n] += x[:n]


def note(n):
    names = {"C": -9, "C#": -8, "D": -7, "D#": -6, "E": -5, "F": -4, "F#": -3, "G": -2, "G#": -1, "A": 0, "A#": 1, "B": 2}
    return 440.0 * 2 ** ((names[n[:-1]] + (int(n[-1]) - 4) * 12) / 12)


# ----------------------------------------------------------------------------- sound effects

def sfx():
    # Ayak sesleri: dört farklı kısa kum/beton adımı
    for i in range(4):
        d = 0.11
        n = bp(noise(d, 10 + i), 300 + i * 60, 2400 + i * 200) * env(int(d * SR), 0.001, 0.03, 0.15, 0.05)
        thump = osc_sine(np.geomspace(120 + i * 8, 60, int(d * SR)), d) * env(int(d * SR), 0.001, 0.04, 0.1, 0.04)
        write_wav(os.path.join(SFX, f"step{i + 1}.wav"), n * 0.8 + thump * 0.6, 0.55)
    d = 0.32
    whoosh = bp(noise(d, 21), 500, 3500) * env(int(d * SR), 0.02, 0.1, 0.3, 0.15)
    rise = osc_sine(np.geomspace(260, 520, int(d * SR)), d) * env(int(d * SR), 0.005, 0.12, 0.2, 0.1)
    write_wav(os.path.join(SFX, "jump.wav"), whoosh * 0.7 + rise * 0.35, 0.6)
    d = 0.4
    sparkle = sum(osc_sine(note(n), d) * env(int(d * SR), 0.003, 0.1, 0.2, 0.2) * (0.6 ** k) for k, n in enumerate(["E6", "B6", "E7"]))
    air = bp(noise(d, 22), 1500, 6000) * env(int(d * SR), 0.01, 0.1, 0.2, 0.2)
    write_wav(os.path.join(SFX, "doublejump.wav"), sparkle * 0.6 + air * 0.4, 0.6)
    d = 0.3
    land = lp(noise(d, 23), 900) * env(int(d * SR), 0.001, 0.08, 0.1, 0.12)
    boom = osc_sine(np.geomspace(140, 45, int(d * SR)), d) * env(int(d * SR), 0.001, 0.12, 0.1, 0.1)
    write_wav(os.path.join(SFX, "land.wav"), land * 0.6 + boom, 0.75)
    # Duvar koşusu: döngülenebilir rüzgâr (1.5 s, uçları yumuşak bağlanır)
    d = 1.5
    w = bp(noise(d + 0.2, 24), 400, 2800)
    w = w[: int(d * SR)] * (0.7 + 0.3 * np.sin(2 * np.pi * np.arange(int(d * SR)) / SR / d * 2))
    xf = int(0.1 * SR)
    w[:xf] = w[:xf] * np.linspace(0, 1, xf) + w[-xf:] * np.linspace(1, 0, xf)
    write_wav(os.path.join(SFX, "wind_loop.wav"), w[: len(w) - xf], 0.5, fade=0.0)
    d = 0.6
    scrape = bp(noise(d, 25), 800, 5000) * env(int(d * SR), 0.01, 0.1, 0.5, 0.25)
    write_wav(os.path.join(SFX, "slide.wav"), scrape, 0.5)
    d = 0.9
    bell = np.zeros(int(d * SR))
    for k, n in enumerate(["C6", "E6", "G6", "C7"]):
        place(bell, osc_sine(note(n), 0.6) * env(int(0.6 * SR), 0.002, 0.1, 0.3, 0.45) + 0.3 * osc_sine(note(n) * 2.01, 0.6) * env(int(0.6 * SR), 0.002, 0.05, 0.2, 0.3), k * 0.07)
    write_wav(os.path.join(SFX, "checkpoint.wav"), bell, 0.7)
    d = 2.2
    fan = np.zeros(int(d * SR))
    seq = [("C5", 0.0), ("E5", 0.12), ("G5", 0.24), ("C6", 0.36), ("G5", 0.6), ("C6", 0.72), ("E6", 0.84), ("G6", 0.96)]
    for n, st in seq:
        l = 0.5 if st < 0.9 else 1.2
        tone = (osc_square(note(n), l, 0.25) * 0.5 + osc_saw(note(n) * 1.003, l) * 0.3) * env(int(l * SR), 0.005, 0.1, 0.5, l * 0.5)
        place(fan, lp(tone, 5000), st)
    place(fan, bp(noise(1.2, 26), 3000, 9000) * env(int(1.2 * SR), 0.01, 0.4, 0.1, 0.6) * 0.3, 0.96)
    write_wav(os.path.join(SFX, "finish.wav"), fan, 0.75)
    d = 0.5
    boing = osc_sine(np.geomspace(180, 900, int(d * SR)), d) * env(int(d * SR), 0.002, 0.2, 0.3, 0.2)
    write_wav(os.path.join(SFX, "bounce.wav"), boing + 0.3 * osc_square(np.geomspace(90, 450, int(d * SR)), d) * env(int(d * SR), 0.002, 0.1, 0.1, 0.1), 0.6)
    d = 0.8
    fall = bp(noise(d, 27), 300, 2000) * env(int(d * SR), 0.05, 0.3, 0.4, 0.3) + osc_sine(np.geomspace(500, 80, int(d * SR)), d) * env(int(d * SR), 0.01, 0.3, 0.3, 0.3) * 0.4
    write_wav(os.path.join(SFX, "fall.wav"), fall, 0.6)
    d = 0.15
    write_wav(os.path.join(SFX, "ui_move.wav"), osc_sine(note("A6"), d) * env(int(d * SR), 0.001, 0.05, 0.2, 0.08), 0.35)
    write_wav(os.path.join(SFX, "ui_select.wav"), (osc_sine(note("E6"), 0.25) * env(int(0.25 * SR), 0.001, 0.08, 0.3, 0.1) + osc_sine(note("B6"), 0.25) * env(int(0.25 * SR), 0.06, 0.08, 0.3, 0.1)), 0.45)
    d = 0.18
    write_wav(os.path.join(SFX, "countdown.wav"), osc_square(note("A5"), d, 0.5) * env(int(d * SR), 0.002, 0.05, 0.4, 0.08), 0.4)
    d = 0.45
    write_wav(os.path.join(SFX, "go.wav"), (osc_square(note("A6"), d, 0.5) * 0.6 + osc_sine(note("A5"), d)) * env(int(d * SR), 0.002, 0.1, 0.5, 0.25), 0.5)


# ----------------------------------------------------------------------------- music

def music(path, bpm=104, bars=32):
    """Ambient synthwave loop: Am - F - C - G progression, arpeggios, pads, gated drums (original)."""
    beat = 60.0 / bpm
    total = bars * 4 * beat
    out = np.zeros(int(total * SR) + SR)
    prog = [("A2", ["A3", "C4", "E4"]), ("F2", ["F3", "A3", "C4"]), ("C3", ["C4", "E4", "G4"]), ("G2", ["G3", "B3", "D4"])]
    for bar in range(bars):
        root, chord = prog[bar % 4]
        t0 = bar * 4 * beat
        section = bar // 8
        # Pad
        for n in chord:
            l = 4 * beat
            pad = (osc_saw(note(n), l) + osc_saw(note(n) * 1.006, l)) * env(int(l * SR), 0.5, 0.5, 0.8, 0.8)
            place(out, lp(pad, 1400 + 600 * (section % 2)) * 0.06, t0)
        # Bas (sekizlik)
        if section >= 1:
            for k in range(8):
                l = beat / 2 * 0.9
                b = (osc_square(note(root), l, 0.5) * 0.5 + osc_sine(note(root) / 2, l)) * env(int(l * SR), 0.003, 0.08, 0.6, 0.05)
                place(out, lp(b, 700) * 0.22, t0 + k * beat / 2)
        # Arpej (onaltılık)
        if section != 0 or bar % 2 == 1:
            arp = chord + [chord[1].replace("3", "4").replace("4", "5")]
            for k in range(16):
                n = arp[k % 4]
                f = note(n) * (2 if section >= 2 else 1)
                l = beat / 4 * 0.8
                a = osc_square(f, l, 0.3) * env(int(l * SR), 0.002, 0.05, 0.3, 0.05)
                place(out, lp(a, 3500) * 0.07, t0 + k * beat / 4)
        # Davul
        if section >= 1:
            for b in range(4):
                tb = t0 + b * beat
                k = osc_sine(np.geomspace(150, 45, int(0.18 * SR)), 0.18) * env(int(0.18 * SR), 0.001, 0.12, 0.1, 0.05)
                place(out, k * 0.45, tb)
                if b % 2 == 1:
                    sn = bp(noise(0.2, bar * 4 + b), 900, 6000) * env(int(0.2 * SR), 0.001, 0.08, 0.2, 0.1)
                    place(out, sn * 0.18, tb)
                for h in range(2):
                    hh = bp(noise(0.04, 500 + bar * 8 + b * 2 + h), 6000, 12000) * env(int(0.04 * SR), 0.0005, 0.02, 0.1, 0.01)
                    place(out, hh * 0.08, tb + h * beat / 2)
    # Basit yankı
    d = int(beat * 0.75 * SR)
    wet = np.zeros_like(out)
    wet[d:] += out[:-d] * 0.25
    wet[2 * d:] += out[:-2 * d] * 0.12
    out = np.tanh((out + wet) * 1.4) * 0.8
    tmp = path + ".tmp.wav"
    write_wav(tmp, out[: int(total * SR)], 0.85, fade=0.0)
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", tmp, "-codec:a", "libmp3lame", "-b:a", "160k", path], check=True)
    os.remove(tmp)


# ----------------------------------------------------------------------------- level themes (Tools/gen_levels.py)
# Each uses its own generator so the textures above stay byte-identical.

def sand_texture(size=512):
    """Wind-rippled sandstone slab."""
    r = np.random.default_rng(21)
    n1 = tileable_noise(size, 3, 5, seed=21)
    n2 = tileable_noise(size, 12, 3, seed=22)
    y, x = np.mgrid[0:size, 0:size].astype(np.float32) / size
    ripple = 0.5 + 0.5 * np.sin(2 * math.pi * (y * 14 + x * 2) + n1 * 6)
    base = 0.62 + 0.08 * (n1 - 0.5) + 0.05 * ripple + 0.04 * (n2 - 0.5)
    base += (r.random((size, size)) > 0.98).astype(np.float32) * r.uniform(-0.06, 0.06, (size, size))
    save_rgb(os.path.join(TEX, "sand.png"), np.stack([base * 1.05, base * 0.86, base * 0.62], -1))


def ice_texture(size=512):
    """Pale blue ice with frost cracks."""
    n1 = tileable_noise(size, 2, 5, seed=31)
    n2 = tileable_noise(size, 9, 4, seed=32)
    crack = np.exp(-((n2 - 0.5) / 0.012) ** 2)
    base = 0.72 + 0.10 * (n1 - 0.5)
    rgb = np.stack([base * 0.86, base * 0.95, base * 1.08], -1)
    rgb = rgb * (1.0 - 0.25 * crack[..., None]) + 0.12 * crack[..., None]
    save_rgb(os.path.join(TEX, "ice.png"), rgb)


def rust_texture(size=512):
    """Riveted steel deck with rust stains."""
    n1 = tileable_noise(size, 3, 5, seed=41)
    n2 = tileable_noise(size, 10, 3, seed=42)
    y, x = np.mgrid[0:size, 0:size].astype(np.float32)
    cell = size / 4
    seam = np.minimum(np.minimum(x % cell, cell - x % cell), np.minimum(y % cell, cell - y % cell))
    u, v = (x % cell) - 10, (y % cell) - 10
    rivet = np.exp(-(u ** 2 + v ** 2) / 8.0)
    rust = np.clip((n1 - 0.45) * 3.0 + (n2 - 0.5), 0, 1)
    steel = 0.36 + 0.05 * (n2 - 0.5)
    rgb = np.stack([steel, steel * 1.02, steel * 1.06], -1) * (1 - rust[..., None]) + \
        np.array([0.42, 0.20, 0.09]) * (0.8 + 0.4 * n2[..., None]) * rust[..., None]
    rgb *= (1.0 - 0.45 * np.exp(-(seam / 1.4) ** 2))[..., None]
    rgb += 0.18 * rivet[..., None]
    save_rgb(os.path.join(TEX, "rust.png"), rgb)


def moss_texture(size=512):
    """Mossy stone blocks."""
    n1 = tileable_noise(size, 3, 5, seed=51)
    n2 = tileable_noise(size, 14, 3, seed=52)
    y, x = np.mgrid[0:size, 0:size].astype(np.float32)
    cell = size / 2
    seam = np.minimum(np.minimum(x % cell, cell - x % cell), np.minimum(y % cell, cell - y % cell))
    stone = 0.44 + 0.08 * (n2 - 0.5)
    moss = np.clip((n1 - 0.4) * 2.5, 0, 1)
    rgb = np.stack([stone, stone * 0.98, stone * 0.92], -1) * (1 - moss[..., None]) + \
        np.array([0.20, 0.36, 0.12]) * (0.8 + 0.5 * n2[..., None]) * moss[..., None]
    rgb *= (1.0 - 0.5 * np.exp(-(seam / 2.0) ** 2))[..., None]
    save_rgb(os.path.join(TEX, "moss.png"), rgb)


def panel_texture(size=512):
    """Dark glossy floor panels with thin light seams (neon night)."""
    n = tileable_noise(size, 4, 4, seed=61)
    y, x = np.mgrid[0:size, 0:size].astype(np.float32)
    cell = size / 4
    seam = np.minimum(np.minimum(x % cell, cell - x % cell), np.minimum(y % cell, cell - y % cell))
    base = 0.10 + 0.03 * (n - 0.5)
    val = base + 0.35 * np.exp(-(seam / 1.2) ** 2)
    save_rgb(os.path.join(TEX, "panel.png"), np.stack([val * 0.95, val * 0.97, val * 1.1], -1))


def theme_textures():
    sand_texture()
    ice_texture()
    rust_texture()
    moss_texture()
    panel_texture()


if __name__ == "__main__":
    if "--themes" in sys.argv:
        # Yalnızca seviye tema dokuları (ses ve müzik yeniden üretilmez)
        theme_textures()
        sys.exit(0)
    concrete_texture()
    metal_texture()
    windows_texture()
    glow_texture()
    spark_texture()
    crosshair_texture()
    theme_textures()
    sfx()
    music(os.path.join(MUS, "theme.mp3"))
    print("assets written to", OUT)

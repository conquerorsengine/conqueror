#!/usr/bin/env python3
"""Procedural assets for the Conqueror Tetris sample.

usage: gen_tetris_assets.py <project Assets dir>

Textures are grayscale/white so the game can tint them per piece. Audio is synthesised
(no samples): sound effects as 16-bit WAV, music as MP3 (ffmpeg + libmp3lame).
The music is an original chiptune arrangement of "Korobeiniki" (Russian folk song, 1861,
public domain melody); bass line, harmony and drums are written for this sample.
"""
import math
import os
import subprocess
import sys
import wave

import numpy as np
from PIL import Image, ImageFilter

SR = 44100
OUT = sys.argv[1] if len(sys.argv) > 1 else "Assets"
TEX = os.path.join(OUT, "Textures")
SFX = os.path.join(OUT, "Audio")
MUS = os.path.join(OUT, "Music")
for d in (TEX, SFX, MUS):
    os.makedirs(d, exist_ok=True)


# ----------------------------------------------------------------------------- textures

def rounded_mask(size, radius, inset=0.0, ss=4):
    """Anti-aliased rounded-rectangle coverage (supersampled)."""
    n = size * ss
    c = (np.arange(n) + 0.5) / ss
    x, y = np.meshgrid(c, c)
    lo, hi = inset + radius, size - inset - radius
    dx = np.maximum(np.maximum(lo - x, x - hi), 0.0)
    dy = np.maximum(np.maximum(lo - y, y - hi), 0.0)
    inside = (np.hypot(dx, dy) <= radius) & (x >= inset) & (x <= size - inset) & (y >= inset) & (y <= size - inset)
    return inside.reshape(size, ss, size, ss).mean(axis=(1, 3))


def save_rgba(path, rgb, alpha):
    rgb = np.clip(rgb, 0.0, 1.0)
    alpha = np.clip(alpha, 0.0, 1.0)
    if rgb.ndim == 2:
        rgb = np.stack([rgb] * 3, axis=-1)
    img = np.dstack([rgb, alpha[..., None]])
    Image.fromarray((img * 255.0 + 0.5).astype(np.uint8)).save(path, optimize=True)


def block_texture(size=64):
    s = size
    yy, xx = np.mgrid[0:s, 0:s].astype(np.float32)
    u, v = (xx + 0.5) / s, (yy + 0.5) / s
    outer = rounded_mask(s, 9.0, 0.5)
    inner = rounded_mask(s, 6.0, 6.0)
    # Vertical body gradient + soft bevel + glossy cap
    body = 0.86 - 0.20 * v
    bevel_light = np.clip(1.0 - (np.minimum(u, v) * s - 1.0) / 6.0, 0, 1) * 0.16
    bevel_dark = np.clip(1.0 - (np.minimum(1 - u, 1 - v) * s - 1.0) / 6.0, 0, 1) * 0.30
    rim = outer - inner                                  # bevel ring
    val = body + rim * (bevel_light - bevel_dark)
    gloss = np.exp(-(((u - 0.38) / 0.30) ** 2 + ((v - 0.22) / 0.13) ** 2)) * 0.22
    val = val + gloss * inner
    # Inner panel slightly brighter with a faint diagonal sheen
    sheen = np.clip(1.0 - np.abs((u + v) - 0.75) * 6.0, 0, 1) * 0.05
    val = val + inner * sheen
    # Thin dark outline for definition between neighbours
    edge = outer - rounded_mask(s, 8.0, 1.6)
    val = val * (1.0 - edge * 0.45)
    save_rgba(os.path.join(TEX, "block.png"), val, outer)


def ghost_texture(size=64):
    s = size
    outer = rounded_mask(s, 9.0, 1.0)
    inner = rounded_mask(s, 6.0, 5.0)
    ring = np.clip(outer - inner, 0, 1)
    alpha = ring * 0.95 + inner * 0.16
    save_rgba(os.path.join(TEX, "ghost.png"), np.ones((s, s)), alpha)


def cell_texture(size=64):
    """Empty board cell: dark inset square, drawn tinted by the board colour."""
    s = size
    outer = rounded_mask(s, 6.0, 2.0)
    inner = rounded_mask(s, 4.0, 4.5)
    val = 0.55 * outer + 0.20 * inner
    alpha = outer * 0.55
    save_rgba(os.path.join(TEX, "cell.png"), val, alpha)


def glow_texture(size=128):
    s = size
    yy, xx = np.mgrid[0:s, 0:s].astype(np.float32)
    r = np.hypot((xx + 0.5) / s - 0.5, (yy + 0.5) / s - 0.5) * 2.0
    a = np.clip(1.0 - r, 0, 1) ** 2.2
    save_rgba(os.path.join(TEX, "glow.png"), np.ones((s, s)), a)


def spark_texture(size=64):
    s = size
    yy, xx = np.mgrid[0:s, 0:s].astype(np.float32)
    x, y = (xx + 0.5) / s - 0.5, (yy + 0.5) / s - 0.5
    r = np.hypot(x, y) * 2.0
    core = np.clip(1.0 - r * 2.2, 0, 1) ** 1.5
    star = np.exp(-np.abs(x) * 40) * np.clip(1 - np.abs(y) * 2.2, 0, 1) + np.exp(-np.abs(y) * 40) * np.clip(1 - np.abs(x) * 2.2, 0, 1)
    a = np.clip(core + star * 0.8, 0, 1)
    save_rgba(os.path.join(TEX, "spark.png"), np.ones((s, s)), a)


def panel_texture(size=64):
    """Rounded panel for 9-slice sprites (border 16 px): white body + soft top highlight + rim."""
    s = size
    yy, xx = np.mgrid[0:s, 0:s].astype(np.float32)
    v = (yy + 0.5) / s
    outer = rounded_mask(s, 14.0, 0.5)
    inner = rounded_mask(s, 12.5, 2.0)
    rim = np.clip(outer - inner, 0, 1)
    val = (0.92 + 0.08 * (1 - v)) * inner + rim * 1.0
    save_rgba(os.path.join(TEX, "panel.png"), val, outer)


def frame_texture(size=128):
    """Neon frame: thin bright outline with an outer glow, transparent centre (9-slice border 32 px)."""
    s = size
    line = rounded_mask(s, 18.0, 14.0) - rounded_mask(s, 16.0, 16.5)
    line = np.clip(line, 0, 1)
    glow = np.array(Image.fromarray((line * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(5.0)), np.float32) / 255.0
    alpha = np.clip(line + glow * 1.8, 0, 1)
    save_rgba(os.path.join(TEX, "frame.png"), np.ones((s, s)), alpha)


def background_texture(w=1920, h=1080):
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    u, v = xx / w, yy / h
    top = np.array([0.035, 0.040, 0.105])
    bottom = np.array([0.090, 0.030, 0.140])
    grad = top[None, None, :] * (1 - v[..., None]) + bottom[None, None, :] * v[..., None]
    # Soft nebula blobs
    rng = np.random.default_rng(7)
    neb = np.zeros((h, w), np.float32)
    for _ in range(9):
        cx, cy = rng.uniform(0, w), rng.uniform(0, h)
        rad = rng.uniform(180, 520)
        neb += np.exp(-(((xx - cx) ** 2 + (yy - cy) ** 2) / (2 * rad * rad))) * rng.uniform(0.25, 0.6)
    tint = np.array([0.20, 0.10, 0.45])
    img = grad + neb[..., None] * tint[None, None, :] * 0.18
    # Perspective grid on the lower part (retro horizon)
    horizon = 0.62
    below = np.clip((v - horizon) / (1 - horizon), 0, 1)
    depth = 1.0 / np.maximum(below, 1e-3)
    lines_h = np.abs(np.sin(depth * 6.0)) < 0.06 + 0.0 * depth
    xw = (u - 0.5) * depth
    lines_v = np.abs(np.sin(xw * 9.0)) < 0.05
    grid = ((lines_h | lines_v) & (below > 0.01)).astype(np.float32) * np.clip(below * 1.4, 0, 1)
    grid = np.array(Image.fromarray((grid * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.2)), np.float32) / 255.0
    img += grid[..., None] * np.array([0.15, 0.25, 0.65])[None, None, :] * 0.55
    # Horizon glow (synthwave sun haze)
    hz = np.exp(-((v - horizon) / 0.035) ** 2) * (0.55 + 0.45 * np.exp(-((u - 0.5) / 0.28) ** 2))
    img += hz[..., None] * np.array([0.55, 0.18, 0.55])[None, None, :] * 0.55
    # Stars
    stars = np.zeros((h, w), np.float32)
    n = 900
    sx, sy = rng.integers(0, w, n), rng.integers(0, int(h * horizon), n)
    stars[sy, sx] = rng.uniform(0.25, 1.0, n)
    stars = np.array(Image.fromarray((stars * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.7)), np.float32) / 255.0 * 2.2
    img += stars[..., None] * np.array([0.8, 0.85, 1.0])[None, None, :]
    # Vignette
    vig = np.clip(1.0 - (np.hypot(u - 0.5, (v - 0.5) * 0.85) * 1.25) ** 2.0, 0.15, 1.0)
    img *= vig[..., None]
    save_rgba(os.path.join(TEX, "background.png"), img, np.ones((h, w)))


# ----------------------------------------------------------------------------- audio helpers

def t_axis(dur):
    return np.arange(int(dur * SR)) / SR


def env_adsr(n, a=0.005, d=0.05, s=0.6, r=0.08, sustain_time=None):
    a_n, d_n, r_n = int(a * SR), int(d * SR), int(r * SR)
    total = n
    s_n = max(0, total - a_n - d_n - r_n)
    e = np.concatenate([
        np.linspace(0, 1, max(a_n, 1), endpoint=False),
        np.linspace(1, s, max(d_n, 1), endpoint=False),
        np.full(s_n, s),
        np.linspace(s, 0, max(r_n, 1)),
    ])
    if len(e) < n:
        e = np.pad(e, (0, n - len(e)))
    return e[:n]


def square(freq, dur, duty=0.5, phase=0.0):
    t = t_axis(dur)
    f = np.broadcast_to(freq, t.shape) if np.ndim(freq) else np.full(t.shape, freq)
    ph = (np.cumsum(f) / SR + phase) % 1.0
    return np.where(ph < duty, 1.0, -1.0)


def triangle(freq, dur):
    t = t_axis(dur)
    f = np.broadcast_to(freq, t.shape) if np.ndim(freq) else np.full(t.shape, freq)
    ph = (np.cumsum(f) / SR) % 1.0
    return 4.0 * np.abs(ph - 0.5) - 1.0


def sine(freq, dur):
    t = t_axis(dur)
    f = np.broadcast_to(freq, t.shape) if np.ndim(freq) else np.full(t.shape, freq)
    return np.sin(2 * np.pi * np.cumsum(f) / SR)


def noise(dur, seed=1):
    return np.random.default_rng(seed).uniform(-1, 1, int(dur * SR))


def lowpass(x, cutoff):
    a = math.exp(-2 * math.pi * cutoff / SR)
    y = np.empty_like(x)
    acc = 0.0
    for i in range(len(x)):
        acc = (1 - a) * x[i] + a * acc
        y[i] = acc
    return y


def lowpass_fast(x, cutoff):
    # Vectorised IIR via scipy when available
    try:
        from scipy.signal import lfilter
        a = math.exp(-2 * math.pi * cutoff / SR)
        return lfilter([1 - a], [1, -a], x)
    except Exception:
        return lowpass(x, cutoff)


def write_wav(path, x, gain=0.8):
    x = np.asarray(x, np.float64)
    peak = np.max(np.abs(x)) or 1.0
    x = x / peak * gain
    # 3 ms fades: no clicks
    f = min(int(0.003 * SR), len(x) // 2)
    if f > 0:
        x[:f] *= np.linspace(0, 1, f)
        x[-f:] *= np.linspace(1, 0, f)
    pcm = (np.clip(x, -1, 1) * 32767).astype(np.int16)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())


def mix(*parts):
    n = max(len(p) for p in parts)
    out = np.zeros(n)
    for p in parts:
        out[: len(p)] += p
    return out


def at(x, offset_s, total_s):
    out = np.zeros(int(total_s * SR))
    o = int(offset_s * SR)
    n = min(len(x), len(out) - o)
    if n > 0:
        out[o:o + n] += x[:n]
    return out


def note_hz(name):
    names = {"C": -9, "C#": -8, "D": -7, "D#": -6, "E": -5, "F": -4, "F#": -3, "G": -2, "G#": -1, "A": 0, "A#": 1, "B": 2}
    pitch, octave = name[:-1], int(name[-1])
    return 440.0 * 2 ** ((names[pitch] + (octave - 4) * 12) / 12)


# ----------------------------------------------------------------------------- sound effects

def sfx_move():
    d = 0.045
    x = square(note_hz("A5"), d, 0.25) * env_adsr(int(d * SR), 0.001, 0.02, 0.3, 0.02)
    write_wav(os.path.join(SFX, "move.wav"), lowpass_fast(x, 6000), 0.45)


def sfx_rotate():
    d = 0.07
    f = np.linspace(note_hz("E5"), note_hz("B5"), int(d * SR))
    x = square(f, d, 0.125) * env_adsr(int(d * SR), 0.001, 0.03, 0.4, 0.03)
    write_wav(os.path.join(SFX, "rotate.wav"), lowpass_fast(x, 7000), 0.45)


def sfx_softdrop():
    d = 0.03
    x = square(note_hz("E4"), d, 0.5) * env_adsr(int(d * SR), 0.001, 0.015, 0.2, 0.01)
    write_wav(os.path.join(SFX, "softdrop.wav"), lowpass_fast(x, 3000), 0.3)


def sfx_harddrop():
    d = 0.22
    f = np.geomspace(220, 45, int(d * SR))
    body = sine(f, d) * env_adsr(int(d * SR), 0.001, 0.12, 0.2, 0.08)
    hit = lowpass_fast(noise(0.06, 3), 2500) * env_adsr(int(0.06 * SR), 0.0005, 0.03, 0.1, 0.02)
    write_wav(os.path.join(SFX, "harddrop.wav"), mix(body * 1.0, hit * 0.6), 0.85)


def sfx_lock():
    d = 0.08
    x = triangle(note_hz("C4"), d) * env_adsr(int(d * SR), 0.001, 0.04, 0.2, 0.03)
    click = lowpass_fast(noise(0.02, 5), 4000) * env_adsr(int(0.02 * SR), 0.0005, 0.01, 0.1, 0.005)
    write_wav(os.path.join(SFX, "lock.wav"), mix(x, click * 0.5), 0.55)


def arpeggio(notes, step, length, duty=0.25, cutoff=7000):
    total = step * len(notes) + length
    parts = []
    for i, n in enumerate(notes):
        tone = square(note_hz(n), length, duty) * env_adsr(int(length * SR), 0.002, 0.05, 0.5, length * 0.5)
        parts.append(at(tone, i * step, total))
    return lowpass_fast(mix(*parts), cutoff)


def sfx_clears():
    write_wav(os.path.join(SFX, "clear1.wav"), arpeggio(["C5", "E5", "G5"], 0.045, 0.16), 0.6)
    write_wav(os.path.join(SFX, "clear2.wav"), arpeggio(["C5", "E5", "G5", "C6"], 0.045, 0.18), 0.62)
    write_wav(os.path.join(SFX, "clear3.wav"), arpeggio(["C5", "E5", "G5", "C6", "E6"], 0.045, 0.2), 0.64)
    # Tetris: bright fanfare + shimmer
    fan = arpeggio(["C5", "E5", "G5", "C6", "E6", "G6", "C7"], 0.04, 0.35, 0.25, 9000)
    shimmer = sine(np.linspace(2000, 4200, int(0.6 * SR)), 0.6) * env_adsr(int(0.6 * SR), 0.05, 0.2, 0.3, 0.3) * 0.25
    write_wav(os.path.join(SFX, "tetris.wav"), mix(fan, at(shimmer, 0.15, 0.8)), 0.75)
    # T-spin: rising sweep
    d = 0.4
    f = np.geomspace(300, 1600, int(d * SR))
    sweep = square(f, d, 0.3) * env_adsr(int(d * SR), 0.01, 0.1, 0.6, 0.2)
    write_wav(os.path.join(SFX, "tspin.wav"), lowpass_fast(sweep, 5000), 0.6)


def sfx_misc():
    write_wav(os.path.join(SFX, "levelup.wav"), arpeggio(["G4", "C5", "E5", "G5", "C6", "E6", "G6"], 0.06, 0.25, 0.5, 8000), 0.65)
    write_wav(os.path.join(SFX, "gameover.wav"), arpeggio(["E5", "D5", "C5", "B4", "A4", "G#4", "A4"], 0.14, 0.35, 0.5, 4500), 0.65)
    d = 0.25
    f = np.geomspace(1200, 300, int(d * SR))
    whoosh = lowpass_fast(noise(d, 9), 3000) * env_adsr(int(d * SR), 0.03, 0.1, 0.3, 0.1) * 0.6 + sine(f, d) * env_adsr(int(d * SR), 0.01, 0.1, 0.2, 0.1) * 0.4
    write_wav(os.path.join(SFX, "hold.wav"), whoosh, 0.5)
    write_wav(os.path.join(SFX, "menu_move.wav"), square(note_hz("E6"), 0.035, 0.25) * env_adsr(int(0.035 * SR), 0.001, 0.015, 0.3, 0.01), 0.35)
    write_wav(os.path.join(SFX, "menu_select.wav"), arpeggio(["C6", "G6"], 0.05, 0.12, 0.25, 9000), 0.5)
    write_wav(os.path.join(SFX, "pause.wav"), arpeggio(["G5", "C5"], 0.07, 0.14, 0.5, 6000), 0.45)
    write_wav(os.path.join(SFX, "countdown.wav"), square(note_hz("A5"), 0.12, 0.5) * env_adsr(int(0.12 * SR), 0.002, 0.05, 0.4, 0.05), 0.45)
    write_wav(os.path.join(SFX, "go.wav"), arpeggio(["A5", "A6"], 0.0, 0.3, 0.5, 9000), 0.5)
    write_wav(os.path.join(SFX, "combo.wav"), arpeggio(["G5", "B5", "D6"], 0.035, 0.12, 0.25, 9000), 0.5)


# ----------------------------------------------------------------------------- music

# Korobeiniki (folk melody, public domain). Durations in eighth notes.
MELODY_A = [
    ("E5", 2), ("B4", 1), ("C5", 1), ("D5", 2), ("C5", 1), ("B4", 1),
    ("A4", 2), ("A4", 1), ("C5", 1), ("E5", 2), ("D5", 1), ("C5", 1),
    ("B4", 3), ("C5", 1), ("D5", 2), ("E5", 2),
    ("C5", 2), ("A4", 2), ("A4", 4),
    ("R", 1), ("D5", 2), ("F5", 1), ("A5", 2), ("G5", 1), ("F5", 1),
    ("E5", 3), ("C5", 1), ("E5", 2), ("D5", 1), ("C5", 1),
    ("B4", 2), ("B4", 1), ("C5", 1), ("D5", 2), ("E5", 2),
    ("C5", 2), ("A4", 2), ("A4", 4),
]
# Original harmony for this sample: one chord root per bar (8 eighths)
CHORDS_A = ["A2", "A2", "E2", "A2", "D2", "C2", "E2", "A2"]


def render_music(path, bpm=148, loops=2):
    eighth = 60.0 / bpm / 2.0
    bars = len(CHORDS_A)
    section = bars * 8 * eighth
    total = section * loops * 2 + 0.5
    out = np.zeros(int(total * SR))

    def place(x, start):
        o = int(start * SR)
        n = min(len(x), len(out) - o)
        if n > 0:
            out[o:o + n] += x[:n]

    for loop in range(loops * 2):
        base = loop * section
        bright = loop % 2 == 1            # second pass: octave doubling + busier drums
        # Lead
        t = base
        for name, dur in MELODY_A:
            length = dur * eighth
            if name != "R":
                f = note_hz(name)
                held = length * 0.95
                n = len(t_axis(held))
                vib = 1.0 + 0.004 * np.sin(2 * np.pi * 5.5 * np.arange(n) / SR) * np.clip(np.arange(n) / SR / 0.15, 0, 1)
                tone = square(f * vib, held, 0.25) * env_adsr(n, 0.004, 0.06, 0.7, 0.05)
                if bright:
                    tone += square(f * 2 * vib, held, 0.125) * env_adsr(n, 0.004, 0.06, 0.5, 0.05) * 0.35
                place(tone * 0.30, t)
            t += length
        # Bass: octave-pumping eighths on the chord root
        for bar, root in enumerate(CHORDS_A):
            f = note_hz(root)
            for k in range(8):
                ff = f * (2 if k % 2 else 1)
                tone = triangle(ff, eighth * 0.9) * env_adsr(int(eighth * 0.9 * SR), 0.003, 0.04, 0.6, 0.03)
                place(tone * 0.42, base + (bar * 8 + k) * eighth)
        # Drums: kick on 1 & 3, snare on 2 & 4, hats on eighths
        for beat in range(bars * 4):
            tb = base + beat * 2 * eighth
            if beat % 2 == 0:
                d = 0.16
                kick = sine(np.geomspace(140, 45, int(d * SR)), d) * env_adsr(int(d * SR), 0.001, 0.1, 0.2, 0.05)
                place(kick * 0.55, tb)
            else:
                d = 0.14
                sn = lowpass_fast(noise(d, beat), 5000) * env_adsr(int(d * SR), 0.001, 0.06, 0.2, 0.06)
                place(sn * 0.22, tb)
            for h in range(2 if not bright else 4):
                d = 0.03
                hat = np.diff(noise(d + 1.0 / SR, 100 + beat * 4 + h)) * env_adsr(int(d * SR), 0.0005, 0.015, 0.1, 0.01)
                place(hat * (0.10 if h % 2 == 0 else 0.06), tb + h * (eighth if not bright else eighth / 2))

    # Gentle master: soft clip + stereo-free mono -> mp3
    out = np.tanh(out * 1.2) * 0.85
    tmp = path + ".tmp.wav"
    write_wav(tmp, out, 0.85)
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", tmp, "-codec:a", "libmp3lame", "-b:a", "160k", path], check=True)
    os.remove(tmp)


def render_title_music(path, bpm=96):
    """Slow pad version of the theme for the title screen."""
    beat = 60.0 / bpm
    chords = [("A3", "C4", "E4"), ("F3", "A3", "C4"), ("D3", "F3", "A3"), ("E3", "G#3", "B3")]
    total = len(chords) * 4 * beat * 2
    out = np.zeros(int(total * SR) + SR)
    for rep in range(2):
        for i, chord in enumerate(chords):
            start = (rep * len(chords) + i) * 4 * beat
            dur = 4 * beat
            for k, n in enumerate(chord):
                f = note_hz(n)
                pad = (sine(f, dur) + 0.3 * sine(f * 2.003, dur) + 0.15 * triangle(f * 0.5, dur)) * env_adsr(int(dur * SR), 0.6, 0.4, 0.8, 1.2)
                o = int(start * SR)
                out[o:o + len(pad)] += pad * 0.18
            # Sparkle arpeggio on top
            for j in range(8):
                n = chord[j % 3]
                f = note_hz(n) * 4
                d = beat * 0.45
                tone = sine(f, d) * env_adsr(int(d * SR), 0.005, 0.1, 0.3, 0.2) * 0.08
                o = int((start + j * beat * 0.5) * SR)
                out[o:o + len(tone)] += tone
    out = lowpass_fast(out, 6000)
    tmp = path + ".tmp.wav"
    write_wav(tmp, out, 0.7)
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", tmp, "-codec:a", "libmp3lame", "-b:a", "128k", path], check=True)
    os.remove(tmp)


if __name__ == "__main__":
    block_texture()
    ghost_texture()
    panel_texture()
    frame_texture()
    cell_texture()
    glow_texture()
    spark_texture()
    background_texture()
    sfx_move()
    sfx_rotate()
    sfx_softdrop()
    sfx_harddrop()
    sfx_lock()
    sfx_clears()
    sfx_misc()
    render_music(os.path.join(MUS, "theme.mp3"))
    render_title_music(os.path.join(MUS, "title.mp3"))
    print("assets written to", OUT)

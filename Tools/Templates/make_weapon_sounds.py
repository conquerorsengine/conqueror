#!/usr/bin/env python3
"""Procedural weapon sounds for the shooter templates (Resources/Templates/Shared/Weapons/Audio).

usage: make_weapon_sounds.py [out dir]
Layered synthesis: transient crack (filtered noise burst), body thump (pitch-dropping sine), mechanical clicks,
early reflections + decaying room tail. All original.
"""
import os
import sys
import wave

import numpy as np
from scipy.signal import butter, sosfilt

SR = 44100
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "Resources", "Templates", "Shared", "Weapons", "Audio")
rng = np.random.default_rng(7)


def t(d):
    return np.arange(int(d * SR)) / SR


def noise(d):
    return rng.uniform(-1, 1, int(d * SR))


def band(x, lo, hi, order=2):
    return sosfilt(butter(order, [lo, hi], btype="band", fs=SR, output="sos"), x)


def low(x, f, order=2):
    return sosfilt(butter(order, f, btype="low", fs=SR, output="sos"), x)


def high(x, f, order=2):
    return sosfilt(butter(order, f, btype="high", fs=SR, output="sos"), x)


def expenv(d, tau):
    return np.exp(-t(d) / tau)


def mix(*parts):
    n = max(len(p) for p in parts)
    out = np.zeros(n)
    for p in parts:
        out[:len(p)] += p
    return out


def delay(x, sec, gain):
    o = int(sec * SR)
    return np.concatenate([np.zeros(o), x * gain])


def tail(x, length, decay, brightness):
    """Cheap room: sum of sparse decaying echoes of a low-passed copy."""
    out = np.zeros(len(x) + int(length * SR))
    out[:len(x)] += x
    src = low(x, brightness)
    for k in range(18):
        d = 0.012 + k * length / 18 + rng.uniform(0, 0.01)
        g = 0.32 * np.exp(-d / decay) * rng.uniform(0.6, 1.0)
        o = int(d * SR)
        out[o:o + len(src)] += src[:len(out) - o] * g
    return out


def save(name, x, gain=0.9):
    os.makedirs(OUT, exist_ok=True)
    x = x / (np.max(np.abs(x)) or 1.0) * gain
    f = min(int(0.003 * SR), len(x) // 2)
    x[-f:] *= np.linspace(1, 0, f)
    with wave.open(os.path.join(OUT, name), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes((np.clip(x, -1, 1) * 32767).astype(np.int16).tobytes())


def click(d=0.03, lo=1500, hi=7000, tau=0.006):
    return band(noise(d), lo, hi) * expenv(d, tau)


def gunshot(body_hz, crack_tau, body_tau, length, decay, bright):
    d = 0.6
    crack = band(noise(d), 1200, 9000) * expenv(d, crack_tau) * 1.0
    snap = high(noise(0.02), 3000) * expenv(0.02, 0.002) * 1.4
    f = body_hz * np.exp(-t(d) / 0.05) + body_hz * 0.35
    body = np.sin(2 * np.pi * np.cumsum(f) / SR) * expenv(d, body_tau) * 0.9
    boom = low(noise(d), 300) * expenv(d, body_tau * 1.6) * 1.6
    mech = delay(click(0.04, 2000, 8000, 0.008), 0.05, 0.25)
    dry = mix(snap, crack, body, boom, mech)
    return tail(np.tanh(dry * 2.2), length, decay, bright)


def main():
    save("pistol_shot.wav", gunshot(140, 0.018, 0.05, 0.9, 0.35, 2500))
    save("rifle_shot.wav", gunshot(95, 0.03, 0.08, 1.4, 0.6, 1800), 0.95)
    save("smg_shot.wav", gunshot(170, 0.012, 0.035, 0.6, 0.25, 3000), 0.85)
    # Mekanik
    save("dry_fire.wav", mix(click(0.05, 2500, 9000, 0.004), delay(click(0.03, 1500, 5000, 0.004), 0.025, 0.6)), 0.5)
    save("mag_out.wav", mix(click(0.06, 800, 4000, 0.01), delay(band(noise(0.15), 300, 2000) * expenv(0.15, 0.04), 0.03, 0.5)), 0.6)
    save("mag_in.wav", mix(band(noise(0.1), 400, 3000) * expenv(0.1, 0.02), delay(click(0.05, 1500, 7000, 0.006), 0.07, 1.3)), 0.7)
    save("slide.wav", mix(band(noise(0.18), 900, 6000) * expenv(0.18, 0.05) * np.linspace(1, 0.3, int(0.18 * SR)),
                          delay(click(0.04, 2000, 9000, 0.005), 0.16, 1.2)), 0.7)
    save("bolt.wav", mix(click(0.05, 700, 3500, 0.012), delay(band(noise(0.25), 500, 4000) * expenv(0.25, 0.08), 0.08, 0.6),
                         delay(click(0.05, 1500, 7000, 0.006), 0.35, 1.0)), 0.7)
    for i in range(3):
        d = 0.5
        ring = sum(np.sin(2 * np.pi * f * t(d)) * expenv(d, 0.06 + 0.03 * k) for k, f in enumerate((3100 + i * 300, 5200 + i * 200, 7400)))
        bounce = mix(ring * 0.4, delay(ring * 0.25, 0.09 + i * 0.02, 1.0), delay(ring * 0.12, 0.17 + i * 0.03, 1.0))
        save(f"shell{i + 1}.wav", bounce, 0.35)
    for i in range(3):
        d = 0.35
        save(f"impact_concrete{i + 1}.wav", mix(band(noise(d), 500 + i * 200, 6000) * expenv(d, 0.03), low(noise(d), 400) * expenv(d, 0.05) * 0.8,
                                                 delay(band(noise(0.25), 2000, 8000) * expenv(0.25, 0.08) * 0.3, 0.02, 1.0)), 0.6)
        ring = np.sin(2 * np.pi * (1800 + i * 400) * t(d)) * expenv(d, 0.12) + np.sin(2 * np.pi * (2900 + i * 300) * t(d)) * expenv(d, 0.08)
        save(f"impact_metal{i + 1}.wav", mix(band(noise(0.05), 2000, 9000) * expenv(0.05, 0.006), ring * 0.5), 0.6)
        save(f"impact_body{i + 1}.wav", mix(low(noise(0.2), 600) * expenv(0.2, 0.03), band(noise(0.08), 300, 1500) * expenv(0.08, 0.015) * 0.6), 0.55)
    save("hitmarker.wav", np.sin(2 * np.pi * 2400 * t(0.05)) * expenv(0.05, 0.012), 0.4)
    save("headshot.wav", mix(np.sin(2 * np.pi * 3000 * t(0.08)) * expenv(0.08, 0.02), delay(np.sin(2 * np.pi * 4200 * t(0.07)) * expenv(0.07, 0.02), 0.05, 1.0)), 0.45)
    save("kill.wav", mix(*[delay(np.sin(2 * np.pi * f * t(0.12)) * expenv(0.12, 0.04), k * 0.06, 1.0) for k, f in enumerate((880, 1320, 1760))]), 0.45)
    # Lazer (Laser Tag)
    d = 0.28
    f = 2400 * np.exp(-t(d) / 0.06) + 300
    zap = np.sign(np.sin(2 * np.pi * np.cumsum(f) / SR)) * expenv(d, 0.08) * 0.5 + np.sin(2 * np.pi * np.cumsum(f * 1.5) / SR) * expenv(d, 0.05) * 0.5
    save("laser_shot.wav", tail(low(zap, 6000), 0.4, 0.2, 4000), 0.6)
    save("laser_hit.wav", mix(np.sin(2 * np.pi * np.cumsum(np.linspace(1800, 600, int(0.25 * SR))) / SR) * expenv(0.25, 0.08),
                              band(noise(0.2), 2000, 8000) * expenv(0.2, 0.03) * 0.4), 0.55)
    save("laser_recharge.wav", np.sin(2 * np.pi * np.cumsum(np.linspace(300, 1800, int(0.6 * SR))) / SR) * np.linspace(0.2, 1, int(0.6 * SR)) * expenv(0.6, 0.5), 0.4)
    # Toplama, hasar alma
    save("pickup.wav", mix(*[delay(np.sin(2 * np.pi * f * t(0.1)) * expenv(0.1, 0.05), k * 0.05, 1.0) for k, f in enumerate((660, 990))]), 0.45)
    save("hurt.wav", mix(low(noise(0.3), 500) * expenv(0.3, 0.08), np.sin(2 * np.pi * np.cumsum(np.linspace(220, 120, int(0.3 * SR))) / SR) * expenv(0.3, 0.1)), 0.55)
    save("empty_click.wav", click(0.03, 3000, 9000, 0.003), 0.4)
    print("weapon sounds written to", os.path.abspath(OUT))


if __name__ == "__main__":
    main()

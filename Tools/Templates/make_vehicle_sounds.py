#!/usr/bin/env python3
"""Procedural vehicle sounds for the vehicle templates (Resources/Templates/Shared/Vehicles/Audio).

usage: make_vehicle_sounds.py [out dir]
engine_loop.wav: 4-cylinder firing pulses at 2000 rpm with intake / exhaust resonances; the buffer holds a whole
number of firing cycles so it loops without a click and VehicleController repitches it with the engine rpm.
engine_idle_loop.wav: same at 900 rpm, rougher. skid_loop.wav: tyre squeal (looping), impact*.wav, checkpoint /
lap / countdown beeps. All original.
"""
import os
import sys
import wave

import numpy as np
from scipy.signal import butter, sosfilt, sosfiltfilt

SR = 44100
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "Resources", "Templates", "Shared", "Vehicles", "Audio")
rng = np.random.default_rng(11)


def t(d):
    return np.arange(int(round(d * SR))) / SR


def band(x, lo, hi, order=2):
    return sosfilt(butter(order, [lo, hi], btype="band", fs=SR, output="sos"), x)


def low(x, f, order=2):
    return sosfilt(butter(order, f, btype="low", fs=SR, output="sos"), x)


def save(name, x, peak=0.85):
    x = np.asarray(x, np.float64)
    x = x / (np.abs(x).max() + 1e-9) * peak
    with wave.open(os.path.join(OUT, name), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes((x * 32767).astype(np.int16).tobytes())
    print(name, f"{len(x) / SR:.2f}s")


def periodic_lowpass(x, f):
    """Zero-phase filtering of a periodic buffer (wrap-around padding keeps the loop seamless)."""
    pad = len(x)
    y = np.concatenate([x, x, x])
    y = sosfiltfilt(butter(2, f, btype="low", fs=SR, output="sos"), y)
    return y[pad:2 * pad]


def periodic_band(x, lo, hi):
    pad = len(x)
    y = np.concatenate([x, x, x])
    y = sosfiltfilt(butter(2, [lo, hi], btype="band", fs=SR, output="sos"), y)
    return y[pad:2 * pad]


def engine(rpm, cycles, roughness):
    fire = rpm / 60.0 * 2.0                      # 4 silindir, 4 zamanlı: devir başına 2 ateşleme
    period = int(round(SR / fire))
    n = period * cycles
    out = np.zeros(n)
    # Her ateşleme: kısa basınç darbesi (hızla sönen, biraz farklı genlikte)
    pulse_t = np.arange(period) / SR
    for k in range(cycles):
        amp = 1.0 + rng.normal(0, roughness)
        tau = 0.0025 + 0.0008 * rng.uniform()
        p = amp * np.exp(-pulse_t / tau) * np.sin(2 * np.pi * fire * 3.0 * pulse_t + rng.uniform(0, 0.3))
        p += 0.6 * amp * np.exp(-pulse_t / (tau * 3)) * np.sin(2 * np.pi * fire * pulse_t)
        out[k * period:(k + 1) * period] += p
    # Krank sesi: ateşleme frekansının yarısı (silindirler arası dengesizlik)
    tt = np.arange(n) / SR
    out += 0.25 * np.sin(2 * np.pi * fire / 2 * tt) + 0.15 * np.sin(2 * np.pi * fire * 2 * tt + 0.7)
    # Egzoz rezonansı + emme hışırtısı (periyodik gürültü: döngü boyunca tekrar)
    noise = rng.normal(0, 1, n)
    body = periodic_lowpass(out, 900) * 1.0 + periodic_band(out, 1400, 3200) * 0.25
    hiss = periodic_band(noise, 2500, 7000) * 0.05 * (1 + 0.6 * np.sin(2 * np.pi * fire * tt))
    return body + hiss


def skid():
    d = 1.5
    n = int(d * SR)
    tt = np.arange(n) / SR
    noise = rng.normal(0, 1, n)
    # Lastik ötmesi: dar bantlı gürültü + yavaşça dalgalanan ton
    tone = np.sin(2 * np.pi * (1150 * tt + 40 * np.sin(2 * np.pi * 3 * tt) / (2 * np.pi * 3)))
    x = periodic_band(noise, 900, 1700) * 1.5 + tone * 0.35 * (0.7 + 0.3 * np.sin(2 * np.pi * 7 * tt))
    x += periodic_band(noise, 300, 600) * 0.4
    # Döngü: 1/1.5 s çarpanlı modülasyonlar tam devirde biter (3 Hz * 1.5 = 4.5 değil): yumuşak çapraz geçiş
    fade = int(0.15 * SR)
    w = np.linspace(0, 1, fade)
    x[:fade] = x[:fade] * w + x[-fade:] * (1 - w)
    return x[:-fade]


def impact(strength, seed):
    r = np.random.default_rng(seed)
    d = 0.9
    tt = t(d)
    noise = r.normal(0, 1, len(tt))
    thump = np.sin(2 * np.pi * (55 + 40 * np.exp(-tt / 0.05)) * tt) * np.exp(-tt / 0.12)
    crunch = band(noise, 500, 4500) * np.exp(-tt / (0.04 + 0.05 * strength))
    rattle = band(noise, 2500, 8000) * np.exp(-tt / 0.25) * 0.25 * (r.uniform(0, 1, len(tt)) > 0.97)
    return thump * 1.2 + crunch * (0.6 + strength) + rattle


def beep(freq, d, n=1, gap=0.0):
    tt = t(d)
    b = (np.sin(2 * np.pi * freq * tt) + 0.3 * np.sin(2 * np.pi * freq * 2 * tt)) * np.minimum(1, tt / 0.005) * np.exp(-tt / (d * 0.6))
    out = []
    for _ in range(n):
        out += [b, np.zeros(int(gap * SR))]
    return np.concatenate(out)


def chime(freqs, d):
    tt = t(d)
    out = np.zeros(len(tt))
    for i, f in enumerate(freqs):
        o = int(i * 0.09 * SR)
        seg = tt[:len(tt) - o]
        out[o:] += np.sin(2 * np.pi * f * seg) * np.exp(-seg / 0.35) * np.minimum(1, seg / 0.004)
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    save("engine_loop.wav", engine(2000, 200, 0.05), 0.8)
    save("engine_idle_loop.wav", engine(900, 60, 0.12), 0.8)
    save("skid_loop.wav", skid(), 0.7)
    save("impact_light.wav", impact(0.2, 3), 0.7)
    save("impact_heavy.wav", impact(1.0, 4), 0.9)
    save("countdown.wav", beep(660, 0.25), 0.6)
    save("countdown_go.wav", beep(1320, 0.6), 0.7)
    save("checkpoint.wav", chime([880, 1320], 0.6), 0.6)
    save("lap.wav", chime([660, 880, 1320], 0.9), 0.7)
    save("wrong_way.wav", beep(330, 0.18, 2, 0.08), 0.5)


if __name__ == "__main__":
    main()

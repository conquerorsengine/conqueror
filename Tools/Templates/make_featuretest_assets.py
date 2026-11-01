#!/usr/bin/env python3
"""Feature Test template assets made from code (CC0): alpha-clipped chain-link texture, conveyor arrow texture,
explosion and alarm sounds. Output: Resources/Templates/Projects/featuretest/Assets/{Textures,Audio}."""
import os
import wave

import numpy as np
from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Resources", "Templates", "Projects", "featuretest", "Assets")
TEX = os.path.join(ROOT, "Textures")
AUD = os.path.join(ROOT, "Audio")
os.makedirs(TEX, exist_ok=True)
os.makedirs(AUD, exist_ok=True)
rng = np.random.default_rng(7)

# Tel örgü (çapraz kafes): telin olduğu yer opak galvaniz gri, delikler tam saydam (alfa kırpma ile çizilir)
s = 256
y, x = np.mgrid[0:s, 0:s].astype(np.float32) + 0.5
period = s / 4.0
u = (x + y) / period
v = (x - y) / period
du = np.abs(u - np.round(u)) * period
dv = np.abs(v - np.round(v)) * period
wire_w = 3.2
d = np.minimum(du, dv)
alpha = np.clip((wire_w - d) / 1.2 + 0.5, 0.0, 1.0)
shade = 0.62 + 0.25 * np.clip(1.0 - d / wire_w, 0.0, 1.0)          # telin ortası parlak (yuvarlak tel)
rgb = np.stack([shade * 0.92, shade * 0.95, shade], axis=-1) + rng.normal(0.0, 0.015, (s, s, 1))
img = np.dstack([np.clip(rgb, 0, 1), alpha])
Image.fromarray((img * 255 + 0.5).astype(np.uint8)).save(os.path.join(TEX, "ChainLink.png"))

# Konveyör bandı: koyu kauçuk + sarı ok (şerit +V yönünde akar)
s = 256
y, x = np.mgrid[0:s, 0:s].astype(np.float32) + 0.5
base = 0.09 + rng.normal(0.0, 0.012, (s, s))
ribs = (np.abs(((y / s) * 16.0) % 1.0 - 0.5) < 0.06) * 0.05
belt = np.clip(base + ribs, 0, 1)
cx = np.abs(x - s / 2) / (s / 2)
yy = (y / s) % 1.0
chevron = np.abs(yy - (0.35 + cx * 0.3)) < 0.075
chevron &= cx < 0.62
rgb = np.stack([belt, belt, belt * 1.05], axis=-1)
rgb[chevron] = [0.95, 0.72, 0.1]
Image.fromarray((np.clip(rgb, 0, 1) * 255 + 0.5).astype(np.uint8)).save(os.path.join(TEX, "ConveyorArrows.png"))


def save_wav(name, samples, rate=22050):
    pcm = (np.clip(samples, -1.0, 1.0) * 32000).astype(np.int16)
    with wave.open(os.path.join(AUD, name), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())


# Patlama: alçak geçiren gürültü + düşük frekans gümbürtü, hızlı atak, uzun sönüm
rate = 22050
n = int(rate * 1.8)
t = np.arange(n) / rate
noise = rng.normal(0.0, 1.0, n)
low = np.zeros(n)
acc = 0.0
for i in range(n):                       # tek kutuplu alçak geçiren (zamanla kararır)
    k = 0.25 * np.exp(-t[i] * 2.2) + 0.02
    acc += k * (noise[i] - acc)
    low[i] = acc
rumble = np.sin(2 * np.pi * (48.0 - 18.0 * t) * t) * np.exp(-t * 2.5)
env = np.minimum(t / 0.004, 1.0) * np.exp(-t * 2.4)
boom = (low * 2.4 + rumble * 0.55) * env
boom /= np.max(np.abs(boom)) + 1e-6
save_wav("explosion.wav", boom * 0.95, rate)

# Alarm: iki tonlu kısa bip
n = int(rate * 0.32)
t = np.arange(n) / rate
f = np.where(t < 0.16, 1180.0, 880.0)
beep = np.sign(np.sin(2 * np.pi * f * t)) * 0.35 + np.sin(2 * np.pi * f * t) * 0.3
env = np.minimum(t / 0.005, 1.0) * np.minimum((0.32 - t) / 0.02, 1.0)
save_wav("alarm_beep.wav", beep * env * 0.8, rate)
print("featuretest assets ->", os.path.normpath(ROOT))

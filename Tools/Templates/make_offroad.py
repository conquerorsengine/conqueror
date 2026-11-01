#!/usr/bin/env python3
"""Vehicle şablonunun Offroad sahnesi: 400 x 400 m engebeli arazi (Terrain bileşeni), çevresi tepelerle kapalı vadi,
toprak patika döngüsü (düzleştirilmiş, toprak katmanı), dik yamaçlarda kaya katmanı, başlangıç düzlüğü.

Çıktılar:
  <inc>    sahne kurucunun okuduğu veri (başlangıç, rampa / kaya konumları ve yükseklikleri)
  <yaml>   TerrainComponent YAML bloğu (post_Offroad.py sahneye ekler)
  <dir>    katman dokuları (Poly Haven CC0 1k diffuse) -> Assets/Terrain

Kullanım: make_offroad.py <polyhaven doku klasörü> <inc> <yaml> <doku çıktı klasörü> [önizleme .png]
"""
import base64
import math
import os
import shutil
import sys

import numpy as np

SIZE = 400.0
HEIGHT = 45.0
RES = 257
SPLAT = 512
LAYERS = [("sparse_grass", (0.78, 1.05, 0.6), 70.0, 0.95), ("grass_path_2", (1.0, 0.95, 0.9), 90.0, 0.9),
          ("rocky_gravel", (0.95, 0.95, 0.95), 60.0, 0.85), ("grass_ground", (0.82, 1.05, 0.62), 55.0, 0.95)]
# Patika döngüsü (x, z), arazi merkezine göre
TRAIL = [(0, 60), (-40, 30), (-80, -10), (-110, -60), (-80, -120), (-20, -135), (40, -110), (70, -60),
         (110, -30), (130, 30), (100, 90), (40, 120), (-10, 110)]
TRAIL_HALF = 4.5
START = (0.0, 60.0)


def value_noise(shape, cells, rng):
    g = rng.uniform(-1, 1, (cells + 3, cells + 3))
    ys = np.linspace(0, cells, shape[0], endpoint=False)
    xs = np.linspace(0, cells, shape[1], endpoint=False)
    yi, xi = np.floor(ys).astype(int), np.floor(xs).astype(int)
    yf, xf = ys - yi, xs - xi
    sy, sx = yf * yf * (3 - 2 * yf), xf * xf * (3 - 2 * xf)
    a = g[np.ix_(yi, xi)]
    b = g[np.ix_(yi, xi + 1)]
    c = g[np.ix_(yi + 1, xi)]
    d = g[np.ix_(yi + 1, xi + 1)]
    top = a + (b - a) * sx[None, :]
    bot = c + (d - c) * sx[None, :]
    return top + (bot - top) * sy[:, None]


def fbm(shape, rng, base=4, octaves=5):
    out = np.zeros(shape)
    amp, total = 1.0, 0.0
    for o in range(octaves):
        out += value_noise(shape, base * 2 ** o, rng) * amp
        total += amp
        amp *= 0.5
    return out / total


def trail_points(step=1.0):
    p = np.array(TRAIL, float)
    n = len(p)
    pts = []
    for i in range(n):
        p0, p1, p2, p3 = p[(i - 1) % n], p[i], p[(i + 1) % n], p[(i + 2) % n]
        k = max(int(np.linalg.norm(p2 - p1) / step), 2)
        for t in np.linspace(0, 1, k, endpoint=False):
            t2, t3 = t * t, t * t * t
            pts.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3))
    return np.array(pts)


def distance_field(xs, zs, pts):
    """Her ızgara noktasının patikaya en yakın uzaklığı ve o noktadaki patika indeksi."""
    X, Z = np.meshgrid(xs, zs)
    best = np.full(X.shape, 1e9)
    idx = np.zeros(X.shape, int)
    for i in range(0, len(pts)):
        d = (X - pts[i, 0]) ** 2 + (Z - pts[i, 1]) ** 2
        m = d < best
        best[m] = d[m]
        idx[m] = i
    return np.sqrt(best), idx


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def build_heights(rng):
    xs = np.linspace(-SIZE / 2, SIZE / 2, RES)
    zs = np.linspace(-SIZE / 2, SIZE / 2, RES)
    X, Z = np.meshgrid(xs, zs)
    h = 9.0 + fbm((RES, RES), rng, 3, 5) * 9.0
    h += np.maximum(fbm((RES, RES), rng, 7, 3), 0) * 6.0          # tümsekler
    # Kenarlar yükselir (kapalı vadi)
    r = np.maximum(np.abs(X), np.abs(Z)) / (SIZE / 2)
    h += smoothstep(0.62, 1.0, r) ** 1.5 * 26.0
    pts = trail_points()
    d, idx = distance_field(xs, zs, pts)
    # Patika yüksekliği: patika boyunca arazinin yumuşatılmış hali
    from scipy.ndimage import gaussian_filter, map_coordinates
    hs = gaussian_filter(h, 6)
    fx = (pts[:, 0] + SIZE / 2) / SIZE * (RES - 1)
    fz = (pts[:, 1] + SIZE / 2) / SIZE * (RES - 1)
    path_h = map_coordinates(hs, [fz, fx], order=1)
    k = 25
    path_h = np.convolve(np.r_[path_h[-k:], path_h, path_h[:k]], np.ones(2 * k + 1) / (2 * k + 1), "same")[k:-k]
    w = 1.0 - smoothstep(TRAIL_HALF, TRAIL_HALF + 7.0, d)
    h = h * (1 - w) + (path_h[idx] - 0.15) * w
    # Başlangıç düzlüğü
    ds = np.sqrt((X - START[0]) ** 2 + (Z - START[1]) ** 2)
    sh = float(path_h[np.argmin(np.hypot(pts[:, 0] - START[0], pts[:, 1] - START[1]))])
    ws = 1.0 - smoothstep(14.0, 24.0, ds)
    h = h * (1 - ws) + sh * ws
    h = np.clip(h, 0.5, HEIGHT - 0.5)
    return xs, zs, h, pts, d


def height_at(h, x, z):
    fx = (x + SIZE / 2) / SIZE * (RES - 1)
    fz = (z + SIZE / 2) / SIZE * (RES - 1)
    x0, z0 = int(math.floor(fx)), int(math.floor(fz))
    tx, tz = fx - x0, fz - z0
    x1, z1 = min(x0 + 1, RES - 1), min(z0 + 1, RES - 1)
    a = h[z0, x0] * (1 - tx) + h[z0, x1] * tx
    b = h[z1, x0] * (1 - tx) + h[z1, x1] * tx
    return float(a * (1 - tz) + b * tz)


def build_splat(h, rng):
    from scipy.ndimage import zoom
    hh = zoom(h, SPLAT / RES, order=1)
    xs = np.linspace(-SIZE / 2, SIZE / 2, SPLAT)
    pts = trail_points(2.0)
    d, _ = distance_field(xs, xs, pts)
    gz, gx = np.gradient(hh, SIZE / SPLAT)
    slope = np.sqrt(gx * gx + gz * gz)
    noise = fbm((SPLAT, SPLAT), rng, 12, 4)
    trail = 1.0 - smoothstep(TRAIL_HALF - 0.5, TRAIL_HALF + 2.5 + noise * 1.5, d)
    X, Z = np.meshgrid(xs, xs)
    start = 1.0 - smoothstep(12.0, 18.0, np.sqrt((X - START[0]) ** 2 + (Z - START[1]) ** 2) + noise * 3)
    trail = np.maximum(trail, start)
    rock = smoothstep(0.42, 0.7, slope + noise * 0.15) * (1 - trail)
    meadow = smoothstep(0.0, 0.35, noise) * (1 - trail) * (1 - rock)
    grass = np.clip(1 - trail - rock - meadow, 0, 1)
    w = np.stack([grass, trail, rock, meadow], -1)
    w /= np.maximum(w.sum(-1, keepdims=True), 1e-6)
    return (w * 255 + 0.5).astype(np.uint8)


def main():
    texdir, inc, yaml_out, tex_out = sys.argv[1:5]
    preview = sys.argv[5] if len(sys.argv) > 5 else None
    rng = np.random.default_rng(42)
    xs, zs, h, pts, _ = build_heights(rng)
    splat = build_splat(h, rng)
    os.makedirs(tex_out, exist_ok=True)
    for name, _, _, _ in LAYERS:
        shutil.copy(os.path.join(texdir, name, "Diffuse.jpg"), os.path.join(tex_out, name + ".jpg"))
    q = np.round(np.clip(h / HEIGHT, 0, 1) * 65535).astype("<u2")
    with open(yaml_out, "w") as f:
        f.write("    TerrainComponent:\n")
        f.write(f"      Width: {SIZE}\n      Length: {SIZE}\n      Height: {HEIGHT}\n      Resolution: {RES}\n")
        f.write(f"      SplatResolution: {SPLAT}\n      LayerCount: {len(LAYERS)}\n      CastShadows: true\n")
        f.write("      ReceiveShadows: true\n      EnableCollider: true\n      Layers:\n")
        for name, tint, tiling, rough in LAYERS:
            f.write(f"        - Texture: Assets/Terrain/{name}.jpg\n          Tint: [{tint[0]}, {tint[1]}, {tint[2]}]\n")
            f.write(f"          Tiling: {tiling}\n          Roughness: {rough}\n          Metallic: 0\n")
        f.write("      Heights: " + base64.b64encode(q.tobytes()).decode() + "\n")
        f.write("      Splat: " + base64.b64encode(splat.tobytes()).decode() + "\n")
    # Kurucu verisi: başlangıç (patikanın yönüne bakar), rampalar, kayalar
    i0 = int(np.argmin(np.hypot(pts[:, 0] - START[0], pts[:, 1] - START[1])))
    t = pts[(i0 + 3) % len(pts)] - pts[i0]
    yaw = math.degrees(math.atan2(-t[0], -t[1]))
    lines = ["// make_offroad.py çıktısı: elle düzenlemeyin"]
    lines.append(f"var OFF_START = {{x: {START[0]:.2f}, y: {height_at(h, *START):.2f}, z: {START[1]:.2f}, yaw: {yaw:.2f}}};")
    ramps = []
    for frac in (0.12, 0.45, 0.78):
        i = int(frac * len(pts))
        p = pts[i]
        e = pts[(i + 7) % len(pts)]                     # rampa 7 m: üst kenarın altındaki arazi yüksekliği
        tt = e - p
        ry = math.degrees(math.atan2(-tt[0], -tt[1]))
        ramps.append(f"{{x: {p[0]:.2f}, y: {height_at(h, p[0], p[1]):.2f}, z: {p[1]:.2f}, yaw: {ry:.2f}, "
                     f"length: {float(np.hypot(*tt)):.2f}, yEnd: {height_at(h, e[0], e[1]):.2f}}}")
    lines.append("var OFF_RAMPS = [" + ", ".join(ramps) + "];")
    rocks = []
    rr = np.random.default_rng(5)
    tries = 0
    while len(rocks) < 26 and tries < 4000:
        tries += 1
        x, z = rr.uniform(-170, 170, 2)
        dmin = np.min(np.hypot(pts[:, 0] - x, pts[:, 1] - z))
        if dmin < 10 or math.hypot(x - START[0], z - START[1]) < 30:
            continue
        rocks.append(f"{{x: {x:.2f}, y: {height_at(h, x, z) - 0.4:.2f}, z: {z:.2f}, yaw: {rr.uniform(0, 360):.1f}, s: {rr.uniform(1.2, 3.4):.2f}}}")
    lines.append("var OFF_ROCKS = [" + ", ".join(rocks) + "];")
    # Patika üstü yarış çizgisi (VehicleAutopilot): 6 m aralık
    line = [f"{{x: {pts[i, 0]:.2f}, y: {height_at(h, pts[i, 0], pts[i, 1]) + 0.5:.2f}, z: {pts[i, 1]:.2f}}}" for i in range(0, len(pts), 6)]
    lines.append("var OFF_LINE = [" + ", ".join(line) + "];")
    lines.append(f"var OFF_SIZE = {SIZE};")
    with open(inc, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(f"terrain h {h.min():.1f}..{h.max():.1f} m, trail {len(pts)} pts, rocks {len(rocks)}")
    if preview:
        from PIL import Image
        img = (splat[..., :3].astype(np.float32))
        col = np.array([[70, 110, 50], [150, 120, 80], [120, 120, 120]], np.float32)
        rgb = (splat[..., 0:1] * col[0] + splat[..., 1:2] * col[1] + splat[..., 2:3] * col[2] + splat[..., 3:4] * np.array([110, 130, 60])) / 255
        shade = np.clip(0.6 + (np.gradient(np.kron(h, np.ones((2, 2)))[:SPLAT, :SPLAT], axis=1)) * 0.4, 0.3, 1.3)[..., None]
        Image.fromarray(np.clip(rgb * shade, 0, 255).astype(np.uint8)).save(preview)


if __name__ == "__main__":
    main()

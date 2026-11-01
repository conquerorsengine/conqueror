#!/usr/bin/env python3
"""Vehicle (Time Trial) şablonunun yarış pisti: kapalı Catmull-Rom orta çizgisinden asfalt yol, kenar çizgileri,
virajlarda kırmızı-beyaz kerbler, start/bitiş damalı çizgisi, iki yanda boyalı bariyer duvarı ve çim zemin.
Tek .glb (görsel + Mesh Collider) ve sahne kurucunun okuduğu veri dosyası (kontrol noktaları, başlangıç,
süs nesnesi konumları) üretir.

Dokular: Poly Haven (CC0) asphalt_track / sparse_grass 1k; kerb, bariyer, damalı desen burada çizilir.
Kullanım: make_track.py <polyhaven doku klasörü> <çıktı Track klasörü> <veri .inc> [üstten önizleme .png]
Pistin kendisi motor içinde kurulur (Racing Line eğrisi + Spline Extrude profilleri, parçalı; editörde düğümler
sürüklenerek düzenlenir). Bu betik dokuları (asfalt renk / normal / ORM, çim, çakıl, kerb, bariyer, start çizgisi) ve
eğri / kontrol noktası / süs verisini (Track.inc) yazar.
"""
import io
import json
import math
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_mannequin import Writer  # noqa: E402

# Orta çizgi kontrol noktaları (x, z) metre; araç -Z yönünde başlar. Saat yönünün tersine tur.
CONTROL = [(0, 70), (0, -60), (4, -110), (28, -142), (66, -152), (102, -136), (116, -102), (106, -70),
           (82, -50), (70, -22), (84, 6), (116, 22), (150, 44), (166, 84), (154, 124), (118, 146),
           (72, 152), (32, 140), (8, 112)]
ROAD_HALF = 6.0
LINE_W = 0.22
LINE_IN = 0.35            # kenar çizgisinin yol kenarına uzaklığı
KERB_W = 1.3
WALL_OFFSET = 13.0        # orta çizgiden bariyere
WALL_H = 1.05
WALL_T = 0.5
STEP = 2.0                # örnekleme aralığı (m)
ROAD_Y = 0.03
GROUND_MARGIN = 220.0


def catmull_rom_closed(pts, step):
    p = np.array(pts, np.float64)
    n = len(p)
    dense = []
    for i in range(n):
        p0, p1, p2, p3 = p[(i - 1) % n], p[i], p[(i + 1) % n], p[(i + 2) % n]
        seg = np.linalg.norm(p2 - p1)
        k = max(int(seg / 0.25), 4)
        for t in np.linspace(0, 1, k, endpoint=False):
            t2, t3 = t * t, t * t * t
            dense.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3))
    dense = np.array(dense)
    # Eşit aralıklı yeniden örnekleme
    d = np.r_[0, np.cumsum(np.linalg.norm(np.diff(np.vstack([dense, dense[:1]]), axis=0), axis=1))]
    total = d[-1]
    count = int(round(total / step))
    s = np.linspace(0, total, count, endpoint=False)
    loop = np.vstack([dense, dense[:1]])
    x = np.interp(s, d, loop[:, 0])
    z = np.interp(s, d, loop[:, 1])
    return np.stack([x, z], 1), total


def frames(c):
    n = len(c)
    t = np.roll(c, -1, 0) - np.roll(c, 1, 0)
    t /= np.linalg.norm(t, axis=1, keepdims=True)
    left = np.stack([t[:, 1], -t[:, 0]], 1)     # ileri (tx, tz) -> sol (tz, -tx)  (y yukarı, sağ el)
    # Eğrilik (işaretli: + sola dönüş)
    a = np.arctan2(t[:, 1], t[:, 0])
    da = np.angle(np.exp(1j * (np.roll(a, -1) - np.roll(a, 1))))
    curv = da / (2 * STEP)
    return t, left, curv


def png(img):
    b = io.BytesIO()
    img.save(b, "PNG")
    return b.getvalue()


def jpg_file(path, size=None, gain=1.0):
    img = Image.open(path).convert("RGB")
    if size:
        img = img.resize(size, Image.LANCZOS)
    if gain != 1.0:
        img = img.point(lambda v: min(255, int(v * gain)))
    b = io.BytesIO()
    img.save(b, "JPEG", quality=90)
    return b.getvalue()


def kerb_texture():
    img = Image.new("RGB", (64, 256), (235, 235, 230))
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, 63, 127], fill=(196, 22, 24))
    return png(img)


def wall_texture():
    # Beyaz beton bariyer, üstte kırmızı şerit, aralıklı reklam panosu yerine koyu bant
    w, h = 512, 128
    img = Image.new("RGB", (w, h), (226, 226, 222))
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, w - 1, 18], fill=(190, 24, 26))
    d.rectangle([0, h - 10, w - 1, h - 1], fill=(120, 120, 118))
    d.rectangle([0, 0, 3, h - 1], fill=(170, 170, 166))     # beton blok eki
    rng = np.random.default_rng(3)
    arr = np.asarray(img).astype(np.float32)
    arr *= 1.0 + rng.normal(0, 0.025, arr.shape[:2])[..., None]
    return png(Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8)))


def checker_texture():
    img = Image.new("RGB", (256, 64), (240, 240, 240))
    d = ImageDraw.Draw(img)
    for i in range(16):
        for j in range(4):
            if (i + j) % 2 == 0:
                d.rectangle([i * 16, j * 16, i * 16 + 15, j * 16 + 15], fill=(18, 18, 18))
    return png(img)


class GLB:
    def __init__(self):
        self.w = Writer()
        self.images, self.textures, self.materials, self.meshes, self.nodes = [], [], [], [], []

    def tex(self, raw, mime):
        self.images.append({"bufferView": self.w.view(raw), "mimeType": mime})
        self.textures.append({"source": len(self.images) - 1, "sampler": 0})
        return len(self.textures) - 1

    def material(self, name, color=None, tex=None, normal=None, mr=None, metallic=0.0, roughness=0.8, normal_scale=1.0):
        m = {"name": name, "pbrMetallicRoughness": {"metallicFactor": metallic, "roughnessFactor": roughness}}
        if color:
            m["pbrMetallicRoughness"]["baseColorFactor"] = list(color) + [1.0]
        if tex is not None:
            m["pbrMetallicRoughness"]["baseColorTexture"] = {"index": tex}
        if mr is not None:
            m["pbrMetallicRoughness"]["metallicRoughnessTexture"] = {"index": mr}
        if normal is not None:
            m["normalTexture"] = {"index": normal, "scale": normal_scale}
        self.materials.append(m)
        return len(self.materials) - 1

    def mesh(self, name, pos, nrm, uv, idx, mat):
        pos = np.asarray(pos, np.float32)
        nrm = np.asarray(nrm, np.float32)
        uv = np.asarray(uv, np.float32)
        idx = np.asarray(idx, np.uint32).reshape(-1)
        acc = lambda a, t, mm=False: self.w.accessor(
            dict({"componentType": 5126 if a.dtype == np.float32 else 5125, "count": int(len(a)), "type": t},
                 **({"min": a.min(0).tolist(), "max": a.max(0).tolist()} if mm else {})), a.tobytes())
        prim = {"attributes": {"POSITION": acc(pos, "VEC3", True), "NORMAL": acc(nrm, "VEC3"), "TEXCOORD_0": acc(uv, "VEC2")},
                "indices": acc(idx, "SCALAR"), "material": mat}
        self.meshes.append({"name": name, "primitives": [prim]})
        self.nodes.append({"name": name, "mesh": len(self.meshes) - 1})

    def save(self, path):
        gj = {"asset": {"version": "2.0", "generator": "Conqueror make_track.py"}, "scene": 0,
              "scenes": [{"nodes": [0]}],
              "nodes": [{"name": "Track", "children": list(range(1, len(self.nodes) + 1))}] + self.nodes,
              "meshes": self.meshes, "materials": self.materials, "images": self.images, "textures": self.textures,
              "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}],
              "accessors": self.w.accessors, "bufferViews": self.w.views, "buffers": [{"byteLength": len(self.w.data)}]}
        js = json.dumps(gj, separators=(",", ":")).encode()
        js += b" " * ((4 - len(js) % 4) % 4)
        data = self.w.data
        while len(data) % 4:
            data.append(0)
        with open(path, "wb") as f:
            f.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(data)))
            f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
            f.write(struct.pack("<II", len(data), 0x004E4942) + bytes(data))
        print(f"{os.path.basename(path)}: {len(self.meshes)} meshes, {len(data) / 1e6:.1f} MB")


def taper(mask, k=5):
    """Kerb / çakıl uçları birden kesilmesin: maske yumuşatılır (0..1 genişlik çarpanı, uçlarda sivrilir)."""
    m = mask.astype(np.float64)
    n = len(m)
    out = np.zeros(n)
    for d in range(-k, k + 1):
        out += np.roll(m, d)
    out /= (2 * k + 1)
    return np.clip((out - 0.15) / 0.85, 0.0, 1.0) * (np.convolve(np.r_[m[-k:], m, m[:k]], np.ones(2 * k + 1), "same")[k:-k] > 0)


def ribbon(c, left, s, a, b, ya, yb, u0, u1, vscale, closed=True, mask=None):
    """İki kenar (orta çizgiden a ve b uzaklıkta, yükseklik ya / yb) arasında şerit.
    mask: örnek başına 0..1 genişlik çarpanı (0 = yok; uçlarda sivrilen kerb / çakıl)."""
    n = len(c)
    pos, nrm, uv, idx = [], [], [], []
    rng = range(n) if closed else range(n - 1)
    for i in rng:
        j = (i + 1) % n
        if mask is not None and mask[i] <= 0.0 and mask[j] <= 0.0:
            continue
        base = len(pos)
        si, sj = s[i], s[i] + STEP
        for k, sv in ((i, si), (j, sj)):
            wk = 1.0 if mask is None else float(mask[k])
            for off, y, u in ((a, ya, u0), (a + (b - a) * wk, ya + (yb - ya) * wk, u0 + (u1 - u0) * wk)):
                p = c[k] + left[k] * off
                pos.append((p[0], y, p[1]))
                uv.append((u, sv / vscale))
        # Normal: şerit eğimi
        e = c[i] + left[i] * b - (c[i] + left[i] * a)
        across = np.array([e[0], yb - ya, e[1]])
        along = np.array([c[j][0] - c[i][0], 0.0, c[j][1] - c[i][1]])
        nv = np.cross(along, across)
        if nv[1] < 0:
            nv = -nv
        nv /= np.linalg.norm(nv) + 1e-9
        nrm += [nv] * 4
        # Üçgenler yukarı baksın (saat yönünün tersi, y yukarıdan)
        tri = [(base, base + 2, base + 1), (base + 1, base + 2, base + 3)]
        for t in tri:
            p0, p1, p2 = (np.array(pos[q]) for q in t)
            if np.cross(p1 - p0, p2 - p0)[1] < 0:
                t = (t[0], t[2], t[1])
            idx += list(t)
    return pos, nrm, uv, idx


def wall(c, left, s, off, inner_face_sign):
    """Bariyer duvarı: üst ve iki yan yüz (kutu profil) offset çizgisi boyunca."""
    n = len(c)
    pos, nrm, uv, idx = [], [], [], []
    half = WALL_T / 2.0
    pts = [c[i] + left[i] * off for i in range(n)]
    for i in range(n):
        j = (i + 1) % n
        sa, sb = s[i] / 4.0, (s[i] + STEP) / 4.0
        for side in (-1, 1):
            # yan yüz
            pa = pts[i] + left[i] * side * half
            pb = pts[j] + left[j] * side * half
            base = len(pos)
            for p, v in ((pa, sa), (pb, sb)):
                pos.append((p[0], 0.0, p[1]))
                uv.append((v, 1.0))
                pos.append((p[0], WALL_H, p[1]))
                uv.append((v, 0.0))
            nv = np.array([left[i][0] * side, 0.0, left[i][1] * side])
            nrm += [nv] * 4
            t1, t2 = (base, base + 2, base + 1), (base + 1, base + 2, base + 3)
            for t in (t1, t2):
                p0, p1, p2 = (np.array(pos[q]) for q in t)
                if np.dot(np.cross(p1 - p0, p2 - p0), nv) < 0:
                    t = (t[0], t[2], t[1])
                idx += list(t)
        # üst yüz
        base = len(pos)
        for p, k, v in ((pts[i], i, sa), (pts[j], j, sb)):
            for side in (-1, 1):
                q = p + left[k] * side * half
                pos.append((q[0], WALL_H, q[1]))
                uv.append((v, 0.02))
        nrm += [np.array([0.0, 1.0, 0.0])] * 4
        for t in ((base, base + 2, base + 1), (base + 1, base + 2, base + 3)):
            p0, p1, p2 = (np.array(pos[q]) for q in t)
            if np.cross(p1 - p0, p2 - p0)[1] < 0:
                t = (t[0], t[2], t[1])
            idx += list(t)
    return pos, nrm, uv, idx


def yaw_of(t):
    """Motorun ileri yönü -Z: ileri (sin, cos) = (-tx, -tz)."""
    return math.degrees(math.atan2(-t[0], -t[1]))


def main():
    texdir, out_dir, out_inc = sys.argv[1:4]
    preview = sys.argv[4] if len(sys.argv) > 4 else None
    c, total = catmull_rom_closed(CONTROL, STEP)
    n = len(c)
    s = np.arange(n) * STEP
    t, left, curv = frames(c)
    # Kendini kesme / dar viraj denetimi
    rmin = 1.0 / max(np.abs(curv).max(), 1e-6)
    print(f"track length {total:.0f} m, samples {n}, min radius {rmin:.1f} m")
    if rmin < WALL_OFFSET + 2:
        print("warning: tightest corner is narrower than the wall offset")

    outdir = out_dir
    os.makedirs(outdir, exist_ok=True)
    # Dokular ayrı dosyalar (Mesh Renderer: renk, normal, maske haritası)
    def put(name, data):
        with open(os.path.join(outdir, name), "wb") as f:
            f.write(data)
    put("asphalt_diffuse.jpg", jpg_file(os.path.join(texdir, "asphalt_track", "Diffuse.jpg"), gain=1.55))
    put("asphalt_normal.jpg", jpg_file(os.path.join(texdir, "asphalt_track", "nor_gl.jpg")))
    put("asphalt_orm.jpg", jpg_file(os.path.join(texdir, "asphalt_track", "arm.jpg")))
    put("grass_diffuse.jpg", jpg_file(os.path.join(texdir, "sparse_grass", "Diffuse.jpg")))
    put("grass_normal.jpg", jpg_file(os.path.join(texdir, "sparse_grass", "nor_gl.jpg")))
    put("gravel_diffuse.jpg", jpg_file(os.path.join(texdir, "gravel_floor", "Diffuse.jpg")))
    put("gravel_normal.jpg", jpg_file(os.path.join(texdir, "gravel_floor", "nor_gl.jpg")))
    put("kerb.png", kerb_texture())
    put("barrier.png", wall_texture())
    put("Checker.png", checker_texture())
    curvy = np.abs(curv) > 1.0 / 55.0
    grow = np.zeros(n, bool)
    for k in range(-6, 7):
        grow |= np.roll(curvy, k)
    outer_left = (curv < -1.0 / 55.0)       # sağa dönüş -> dış taraf sol
    outer_right = (curv > 1.0 / 55.0)
    lo = c.min(0) - GROUND_MARGIN
    hi = c.max(0) + GROUND_MARGIN

    # Sahne verisi: kontrol noktaları (eşit aralıklı, ilk = start), başlangıç ızgarası, süs nesneleri
    cps = []
    count = 8
    for k in range(count):
        i = int(round(k * n / count)) % n
        cps.append((float(c[i][0]), float(c[i][1]), yaw_of(t[i])))
    start_i = n - int(14 / STEP)
    st = (float(c[start_i][0]), float(c[start_i][1]), yaw_of(t[start_i]))
    # Lastik yığınları: viraj dışında bariyer önü; ışıklar: düzlüklerde dış duvar arkası
    tyres, lamps = [], []
    for i in range(0, n, 3):
        if outer_left[i] or outer_right[i]:
            side = 1 if outer_left[i] else -1
            q = c[i] + left[i] * side * (WALL_OFFSET - 0.9)
            tyres.append((float(q[0]), float(q[1])))
    for i in range(0, n, 24):
        if not curvy[i]:
            q = c[i] + left[i] * (WALL_OFFSET + 2.5)
            lamps.append((float(q[0]), float(q[1]), yaw_of(left[i])))
    with open(out_inc, "w", encoding="utf-8") as f:
        f.write("// make_track.py çıktısı: elle düzenlemeyin\n")
        f.write(f"var TRACK_LENGTH = {total:.1f};\n")
        f.write("var TRACK_CHECKPOINTS = [" + ", ".join(f"{{x: {x:.2f}, z: {z:.2f}, yaw: {y:.2f}}}" for x, z, y in cps) + "];\n")
        f.write(f"var TRACK_START = {{x: {st[0]:.2f}, z: {st[1]:.2f}, yaw: {st[2]:.2f}}};\n")
        f.write(f"var TRACK_WIDTH = {2 * ROAD_HALF:.1f};\n")
        f.write("var TRACK_TYRES = [" + ", ".join(f"{{x: {x:.2f}, z: {z:.2f}}}" for x, z in tyres) + "];\n")
        f.write("var TRACK_LAMPS = [" + ", ".join(f"{{x: {x:.2f}, z: {z:.2f}, yaw: {y:.2f}}}" for x, z, y in lamps) + "];\n")
        f.write("var TRACK_LINE = [" + ", ".join(f"{{x: {c[i][0]:.2f}, y: 0.0, z: {c[i][1]:.2f}}}" for i in range(0, n, 4)) + "];\n")
        f.write(f"var TRACK_ROAD_HALF = {ROAD_HALF};\nvar TRACK_LINE_W = {LINE_W};\nvar TRACK_LINE_IN = {LINE_IN};\nvar TRACK_KERB_W = {KERB_W};\n")
        f.write(f"var TRACK_WALL_OFFSET = {WALL_OFFSET};\nvar TRACK_WALL_H = {WALL_H};\nvar TRACK_WALL_T = {WALL_T};\nvar TRACK_ROAD_Y = {ROAD_Y};\n")
        f.write(f"var TRACK_GROUND_MIN = {{x: {lo[0]:.1f}, z: {lo[1]:.1f}}};\nvar TRACK_GROUND_MAX = {{x: {hi[0]:.1f}, z: {hi[1]:.1f}}};\n")
        f.write(f"var TRACK_MIN = {{x: {c.min(0)[0]:.1f}, z: {c.min(0)[1]:.1f}}};\nvar TRACK_MAX = {{x: {c.max(0)[0]:.1f}, z: {c.max(0)[1]:.1f}}};\n")
    if not preview:
        return
    # Önizleme (üstten)
    prev = Image.new("RGB", (800, 800), (40, 90, 40))
    d = ImageDraw.Draw(prev)
    sc = 780 / max(hi[0] - lo[0] - 2 * GROUND_MARGIN + 40, hi[1] - lo[1] - 2 * GROUND_MARGIN + 40)
    tp = lambda q: (10 + (q[0] - lo[0] - GROUND_MARGIN + 20) * sc, 10 + (q[1] - lo[1] - GROUND_MARGIN + 20) * sc)
    for i in range(n):
        j = (i + 1) % n
        d.line([tp(c[i] + left[i] * WALL_OFFSET), tp(c[j] + left[j] * WALL_OFFSET)], fill=(220, 220, 220), width=2)
        d.line([tp(c[i] - left[i] * WALL_OFFSET), tp(c[j] - left[j] * WALL_OFFSET)], fill=(220, 220, 220), width=2)
        d.line([tp(c[i]), tp(c[j])], fill=(200, 30, 30) if grow[i] else (30, 30, 30), width=max(2, int(12 * sc)))
    for x, z, _ in cps:
        d.ellipse([tp((x, z))[0] - 5, tp((x, z))[1] - 5, tp((x, z))[0] + 5, tp((x, z))[1] + 5], fill=(255, 220, 0))
    prev.save(preview)


if __name__ == "__main__":
    main()

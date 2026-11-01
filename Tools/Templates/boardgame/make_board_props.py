#!/usr/bin/env python3
"""3D Board Game şablonunun alçak poligon (low-poly, düz gölgeli) dekor modelleri ve oyun taşı.

Ağaç (çam, yuvarlak taçlı), çalı, kaya, karlı dağ zirvesi ve kulübe kodla üretilir (dış varlık yok); oyun taşı Poly Haven
"Chess Set" (CC0) içinden beyaz at alınır. Altıgen karolar 1 m: modeller bir karoya oturacak boyuttadır, tabanları y = 0.

Kullanım: make_board_props.py <çıktı Assets/Models klasörü> [chess_set_1k.gltf]
"""
import json
import math
import os
import random
import struct
import sys

import numpy as np


def srgb_to_linear(c):
    return [((x / 255.0) ** 2.2) for x in c]


class Glb:
    """Birden fazla malzemeli tek düğümlü statik ağ (+ kimlik kök düğüm: motor kök dönüşümünü uygulamaz)."""

    def __init__(self):
        self.data = bytearray()
        self.views, self.accessors, self.materials, self.prims = [], [], [], []
        self.images, self.textures = [], []

    def _view(self, raw, target=None):
        while len(self.data) % 4:
            self.data.append(0)
        v = {"buffer": 0, "byteOffset": len(self.data), "byteLength": len(raw)}
        if target:
            v["target"] = target
        self.data += raw
        self.views.append(v)
        return len(self.views) - 1

    def _acc(self, arr, ctype, typ, target, minmax=False):
        a = {"bufferView": self._view(arr.tobytes(), target), "componentType": ctype, "count": len(arr), "type": typ}
        if minmax:
            a["min"] = arr.min(axis=0).tolist()
            a["max"] = arr.max(axis=0).tolist()
        self.accessors.append(a)
        return len(self.accessors) - 1

    def material(self, name, rgb, rough=0.85, metal=0.0, emissive=None):
        m = {"name": name, "pbrMetallicRoughness": {"baseColorFactor": srgb_to_linear(rgb) + [1.0],
                                                    "metallicFactor": metal, "roughnessFactor": rough}}
        if emissive:
            m["emissiveFactor"] = emissive
        self.materials.append(m)
        return len(self.materials) - 1

    def add(self, tris, mat):
        """tris: (N, 3, 3) üçgen köşeleri; düz gölgeleme için köşeler paylaşılmaz (her yüzün kendi normali)."""
        tris = np.asarray(tris, np.float32).reshape(-1, 3, 3)
        n = np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0])
        n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)
        pos = tris.reshape(-1, 3)
        nrm = np.repeat(n, 3, axis=0).astype(np.float32)
        uv = np.stack([pos[:, 0] + pos[:, 2], pos[:, 1]], axis=1).astype(np.float32)
        idx = np.arange(len(pos), dtype=np.uint32)
        self.prims.append({"attributes": {"POSITION": self._acc(pos, 5126, "VEC3", 34962, True),
                                          "NORMAL": self._acc(nrm, 5126, "VEC3", 34962),
                                          "TEXCOORD_0": self._acc(uv, 5126, "VEC2", 34962)},
                           "indices": self._acc(idx, 5125, "SCALAR", 34963), "material": mat})

    def save(self, path, name):
        nodes = [{"name": "Root", "children": [1]}, {"name": name, "mesh": 0}]
        gj = {"asset": {"version": "2.0", "generator": "Conqueror make_board_props.py"}, "scene": 0,
              "scenes": [{"nodes": [0]}], "nodes": nodes, "meshes": [{"name": name, "primitives": self.prims}],
              "materials": self.materials, "accessors": self.accessors, "bufferViews": self.views,
              "buffers": [{"byteLength": len(self.data)}]}
        if self.images:
            gj["images"], gj["textures"], gj["samplers"] = self.images, self.textures, [{}]
        write_glb(path, gj, self.data)


def write_glb(path, gj, data):
    data = bytearray(data)
    while len(data) % 4:
        data.append(0)
    gj["buffers"] = [{"byteLength": len(data)}]
    js = json.dumps(gj, separators=(",", ":")).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(data)))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(data), 0x004E4942) + bytes(data))
    print(f"{os.path.basename(path)}: {len(data) / 1024:.0f} KB")


# ---------------------------------------------------------------------------------------------- şekiller

def ring(r, y, sides, phase=0.0, jitter=0.0, rng=None):
    pts = []
    for i in range(sides):
        a = phase + i * 2 * math.pi / sides
        rr = r * (1.0 + (rng.uniform(-jitter, jitter) if rng else 0.0))
        pts.append((math.cos(a) * rr, y, math.sin(a) * rr))
    return pts


def cone(r, y0, h, sides, rng=None, jitter=0.0, tip_offset=(0.0, 0.0)):
    base = ring(r, y0, sides, rng.uniform(0, 6.28) if rng else 0.0, jitter, rng)
    tip = (tip_offset[0], y0 + h, tip_offset[1])
    tris = []
    for i in range(sides):
        a, b = base[i], base[(i + 1) % sides]
        tris.append([a, tip, b])
        tris.append([a, b, (0.0, y0, 0.0)])          # alt kapak
    return tris


def frustum(r0, r1, y0, y1, sides, phase=0.0):
    lo, hi = ring(r0, y0, sides, phase), ring(r1, y1, sides, phase)
    tris = []
    for i in range(sides):
        j = (i + 1) % sides
        tris += [[lo[i], hi[i], hi[j]], [lo[i], hi[j], lo[j]]]
        tris += [[(0, y1, 0), hi[j], hi[i]], [(0, y0, 0), lo[i], lo[j]]]
    return tris


def icosphere(r, subdiv, rng=None, noise=0.0, squash=1.0, y0=0.0):
    t = (1 + 5 ** 0.5) / 2
    v = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t),
         (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
    v = [np.array(p, float) / np.linalg.norm(p) for p in v]
    f = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6),
         (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10),
         (8, 6, 7), (9, 8, 1)]
    for _ in range(subdiv):
        cache, nf = {}, []

        def mid(a, b):
            k = (min(a, b), max(a, b))
            if k not in cache:
                m = v[a] + v[b]
                v.append(m / np.linalg.norm(m))
                cache[k] = len(v) - 1
            return cache[k]
        for a, b, c in f:
            ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
            nf += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        f = nf
    pts = []
    for p in v:
        k = 1.0 + (rng.uniform(-noise, noise) if rng else 0.0)
        pts.append((p[0] * r * k, p[1] * r * k * squash, p[2] * r * k))
    lift = -min(p[1] for p in pts) + y0
    pts = [(p[0], p[1] + lift, p[2]) for p in pts]
    return [[pts[a], pts[b], pts[c]] for a, b, c in f]


def box(sx, sy, sz, cx=0.0, cy=0.0, cz=0.0):
    x0, x1, y0, y1, z0, z1 = cx - sx / 2, cx + sx / 2, cy, cy + sy, cz - sz / 2, cz + sz / 2
    c = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0), (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
    q = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 4, 7, 3), (1, 2, 6, 5), (3, 7, 6, 2), (0, 1, 5, 4)]
    tris = []
    for a, b, cc, d in q:
        tris += [[c[a], c[b], c[cc]], [c[a], c[cc], c[d]]]
    return tris


def roof(sx, sz, y0, h):
    """Beşik çatı (iki eğik yüz + iki üçgen alın)."""
    x0, x1, z0, z1 = -sx / 2, sx / 2, -sz / 2, sz / 2
    r0, r1 = (x0, y0 + h, 0.0), (x1, y0 + h, 0.0)
    a, b, c, d = (x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)
    return [[a, r0, r1], [a, r1, b], [d, c, r1], [d, r1, r0], [a, d, r0], [b, r1, c]]


# ---------------------------------------------------------------------------------------------- modeller

def pine(path, seed):
    rng = random.Random(seed)
    g = Glb()
    trunk = g.material("Bark", (92, 62, 40), 0.9)
    leaves = [g.material("Needles A", (46, 104, 58), 0.85), g.material("Needles B", (38, 92, 52), 0.85)]
    g.add(frustum(0.055, 0.045, 0.0, 0.22, 6), trunk)
    y, r = 0.15, 0.30
    for k in range(3):
        h = 0.34 - k * 0.04
        g.add(cone(r, y, h, 7, rng, 0.08, (rng.uniform(-0.02, 0.02), rng.uniform(-0.02, 0.02))), leaves[k % 2])
        y += h * 0.55
        r *= 0.74
    g.save(path, "Pine")


def round_tree(path, seed):
    rng = random.Random(seed)
    g = Glb()
    trunk = g.material("Bark", (98, 68, 44), 0.9)
    crown = g.material("Leaves", (88, 140, 60), 0.85)
    g.add(frustum(0.06, 0.04, 0.0, 0.32, 6), trunk)
    g.add(icosphere(0.26, 1, rng, 0.12, 0.92, 0.26), crown)
    g.save(path, "Tree")


def bush(path, seed):
    rng = random.Random(seed)
    g = Glb()
    g.add(icosphere(0.16, 1, rng, 0.15, 0.7, -0.03), g.material("Bush", (74, 128, 56), 0.9))
    g.save(path, "Bush")


def rock(path, seed, tone):
    rng = random.Random(seed)
    g = Glb()
    g.add(icosphere(0.2, 1, rng, 0.25, 0.65, -0.04), g.material("Rock", tone, 0.8))
    g.save(path, "Rock")


def peak(path, seed):
    """Dağ zirvesi: düzensiz 7 kenarlı koni + kar başlığı (Wall karolarının üstü)."""
    rng = random.Random(seed)
    g = Glb()
    stone = g.material("Mountain", (122, 118, 112), 0.85)
    snow = g.material("Snow", (236, 240, 246), 0.6)
    sides = 7
    base = ring(0.46, 0.0, sides, rng.uniform(0, 6.28), 0.12, rng)
    mid_y, top = 0.62, (rng.uniform(-0.04, 0.04), 0.95, rng.uniform(-0.04, 0.04))
    midr = [(p[0] * 0.38 + top[0] * 0.62, mid_y, p[2] * 0.38 + top[2] * 0.62) for p in base]
    tris = []
    for i in range(sides):
        j = (i + 1) % sides
        tris += [[base[i], midr[i], midr[j]], [base[i], midr[j], base[j]]]
    g.add(tris, stone)
    cap = []
    for i in range(sides):
        j = (i + 1) % sides
        cap.append([midr[i], top, midr[j]])
    g.add(cap, snow)
    g.save(path, "Peak")


def hut(path):
    g = Glb()
    wall = g.material("Plaster", (226, 214, 190), 0.85)
    roofm = g.material("Roof", (170, 64, 48), 0.75)
    door = g.material("Door", (90, 60, 38), 0.8)
    g.add(box(0.36, 0.24, 0.28), wall)
    g.add(roof(0.42, 0.34, 0.24, 0.16), roofm)
    g.add(box(0.08, 0.13, 0.01, 0.0, 0.0, 0.145), door)
    g.add(box(0.05, 0.12, 0.05, 0.12, 0.3, -0.05), wall)           # baca
    g.save(path, "Hut")


def knight(src, path):
    """Chess Set içinden beyaz at: düğüm konumu sıfırlanır (taş merkezi orijinde, tabanı y = 0)."""
    base = os.path.dirname(src)
    j = json.load(open(src))
    bins = [open(os.path.join(base, b["uri"]), "rb").read() for b in j["buffers"]]
    node = next(n for n in j["nodes"] if n.get("name") == "piece_knight_white_01")
    mesh = j["meshes"][node["mesh"]]
    out = {"data": bytearray(), "views": [], "accessors": []}

    def copy_acc(ai):
        a = dict(j["accessors"][ai])
        bv = j["bufferViews"][a.pop("bufferView")]
        off = bv.get("byteOffset", 0) + a.pop("byteOffset", 0)
        comp = {5126: 4, 5125: 4, 5123: 2, 5121: 1}[a["componentType"]]
        n = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[a["type"]]
        stride = bv.get("byteStride", comp * n)
        raw = b"".join(bins[bv["buffer"]][off + i * stride: off + i * stride + comp * n] for i in range(a["count"]))
        while len(out["data"]) % 4:
            out["data"].append(0)
        out["views"].append({"buffer": 0, "byteOffset": len(out["data"]), "byteLength": len(raw)})
        out["data"] += raw
        a["bufferView"] = len(out["views"]) - 1
        out["accessors"].append(a)
        return len(out["accessors"]) - 1
    prims, mats, tex_remap, images, textures = [], [], {}, [], []

    def copy_tex(ti):
        if ti not in tex_remap:
            img = j["images"][j["textures"][ti]["source"]]
            raw = open(os.path.join(base, img["uri"]), "rb").read()
            while len(out["data"]) % 4:
                out["data"].append(0)
            out["views"].append({"buffer": 0, "byteOffset": len(out["data"]), "byteLength": len(raw)})
            out["data"] += raw
            images.append({"bufferView": len(out["views"]) - 1, "mimeType": "image/jpeg"})
            textures.append({"source": len(images) - 1, "sampler": 0})
            tex_remap[ti] = len(textures) - 1
        return tex_remap[ti]
    for p in mesh["primitives"]:
        m = json.loads(json.dumps(j["materials"][p["material"]]))
        pbr = m.get("pbrMetallicRoughness", {})
        for key in ("baseColorTexture", "metallicRoughnessTexture"):
            if key in pbr:
                pbr[key] = {"index": copy_tex(pbr[key]["index"])}
        if "normalTexture" in m:
            m["normalTexture"] = {"index": copy_tex(m["normalTexture"]["index"])}
        mats.append(m)
        prims.append({"attributes": {k: copy_acc(v) for k, v in p["attributes"].items()},
                      "indices": copy_acc(p["indices"]), "material": len(mats) - 1})
    # Taş ölçeği: satranç taşı ~5 cm -> 1 m karo için x8 (kökte), tabanı y = 0
    pos_acc = out["accessors"][prims[0]["attributes"]["POSITION"]]
    lift = -pos_acc["min"][1]
    gj = {"asset": {"version": "2.0", "generator": "Conqueror make_board_props.py", "copyright": "Poly Haven, CC0"},
          "scene": 0, "scenes": [{"nodes": [0]}],
          "nodes": [{"name": "Root", "children": [1]},
                    {"name": "Knight", "children": [2], "matrix": [8, 0, 0, 0, 0, 8, 0, 0, 0, 0, 8, 0, 0, lift * 8, 0, 1]},
                    {"name": "piece_knight_white", "mesh": 0}],
          "meshes": [{"name": "Knight", "primitives": prims}], "materials": mats, "images": images, "textures": textures,
          "samplers": [{}], "accessors": out["accessors"], "bufferViews": out["views"]}
    write_glb(path, gj, out["data"])


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    pine(os.path.join(out, "Pine_A.glb"), 3)
    pine(os.path.join(out, "Pine_B.glb"), 11)
    round_tree(os.path.join(out, "Tree.glb"), 5)
    bush(os.path.join(out, "Bush.glb"), 7)
    rock(os.path.join(out, "Rock_A.glb"), 13, (128, 124, 118))
    rock(os.path.join(out, "Rock_B.glb"), 21, (104, 100, 96))
    peak(os.path.join(out, "Peak.glb"), 17)
    hut(os.path.join(out, "Hut.glb"))
    if len(sys.argv) > 2:
        knight(sys.argv[2], os.path.join(out, "Knight.glb"))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Poly Haven (CC0) glTF -> tek dosya .glb, isteğe bağlı kök dönüşümüyle (yatırma, tabana oturtma).

Kullanım: pack_prop.py <girdi .gltf> <çıktı .glb> [--rotx derece] [--ground] [--glass]
--ground: dönüşümden sonra en alt nokta y = 0 olur.
--glass: adı "glass" içeren malzemeler ince, koyu, çoğunlukla saydam cam olur (Poly Haven camı dokulu, alfasız
         BLEND olarak dışa aktarır; olduğu gibi kalırsa çerçevedeki resmi opak bir levha gibi örter).
"""
import json
import math
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_mannequin import Writer, accessor_bytes, find_image, load_gltf  # noqa: E402


def main():
    args = sys.argv[1:]
    src, out = args[0], args[1]
    rotx = float(args[args.index("--rotx") + 1]) if "--rotx" in args else 0.0
    j, bins, base = load_gltf(src)
    w = Writer()
    remap = {}

    def acc(ai):
        if ai not in remap:
            remap[ai] = w.accessor(j["accessors"][ai], accessor_bytes(j, bins, ai))
        return remap[ai]

    a = math.radians(rotx)
    rot = np.array([[1, 0, 0, 0], [0, math.cos(a), -math.sin(a), 0], [0, math.sin(a), math.cos(a), 0], [0, 0, 0, 1]])
    lift = 0.0
    if "--ground" in args:
        ys = []
        for m in j["meshes"]:
            for p in m["primitives"]:
                ac = j["accessors"][p["attributes"]["POSITION"]]
                lo, hi = np.array(ac["min"]), np.array(ac["max"])
                for cx in (lo[0], hi[0]):
                    for cy in (lo[1], hi[1]):
                        for cz in (lo[2], hi[2]):
                            ys.append((rot @ np.array([cx, cy, cz, 1.0]))[1])
        lift = -min(ys)
    root = rot.copy()
    root[1, 3] += lift
    meshes = []
    for m in j["meshes"]:
        m = json.loads(json.dumps(m))
        for p in m["primitives"]:
            p["attributes"] = {k: acc(v) for k, v in p["attributes"].items()}
            if "indices" in p:
                p["indices"] = acc(p["indices"])
        meshes.append(m)
    images = []
    for img in j.get("images", []):
        if "uri" in img:
            raw = open(find_image(base, img["uri"]), "rb").read()
            mime = "image/png" if img["uri"].lower().endswith(".png") else "image/jpeg"
        else:
            bv = j["bufferViews"][img["bufferView"]]
            raw = bins[bv["buffer"]][bv.get("byteOffset", 0):bv.get("byteOffset", 0) + bv["byteLength"]]
            mime = img.get("mimeType", "image/png")
        images.append({"bufferView": w.view(raw), "mimeType": mime})
    # Motorun yükleyicisi kök düğümün dönüşümünü uygulamaz: dönüşüm birim kökün altındaki düğümde
    nodes = [{"name": "Root", "children": [1]},
             {"name": "Transform", "matrix": [float(x) for x in root.T.reshape(-1)],
              "children": [i + 2 for i in j["scenes"][j.get("scene", 0)]["nodes"]]}]
    for n in j["nodes"]:
        n = dict(n)
        if "children" in n:
            n["children"] = [c + 2 for c in n["children"]]
        nodes.append(n)
    materials = j.get("materials", [])
    if "--glass" in args:
        for mat in materials:
            if "glass" in mat.get("name", "").lower():
                pbr = mat.setdefault("pbrMetallicRoughness", {})
                pbr.pop("baseColorTexture", None)
                pbr.pop("metallicRoughnessTexture", None)
                pbr["baseColorFactor"] = [0.02, 0.02, 0.02, 0.12]
                pbr["metallicFactor"] = 0.0
                pbr["roughnessFactor"] = 0.05
                mat.pop("normalTexture", None)
                mat["alphaMode"] = "BLEND"
    gj = {"asset": {"version": "2.0", "generator": "Conqueror pack_prop.py", "copyright": "Poly Haven, CC0"}, "scene": 0,
          "scenes": [{"nodes": [0]}], "nodes": nodes, "meshes": meshes, "materials": materials,
          "textures": j.get("textures", []), "images": images, "samplers": j.get("samplers", [{}]),
          "accessors": w.accessors, "bufferViews": w.views, "buffers": [{"byteLength": len(w.data)}]}
    js = json.dumps(gj, separators=(",", ":")).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    while len(w.data) % 4:
        w.data.append(0)
    with open(out, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(w.data)))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(w.data), 0x004E4942) + bytes(w.data))
    print(f"{os.path.basename(out)}: {len(w.data) / 1e6:.1f} MB, lift {lift:.3f}")


if __name__ == "__main__":
    main()

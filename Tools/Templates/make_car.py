#!/usr/bin/env python3
"""Khronos "CarConcept" (CC-BY 4.0) -> Conqueror araç modelleri.

Gövde ve dört tekerlek ayrı .glb olur (tekerlekler WheelCollider ile döner / direksiyon alır), boya
seçenekleri (KHR_materials_variants) ayrı gövde dosyalarına açılır. Motorun yükleyicisinin desteklemediği
ayrıntılar (ikinci UV kümesindeki AO, doku dönüşümlü ayrıntı normal haritaları, yanardönerlik) atılır.
Eksen: +Y yukarı, araç önü -Z (motorun ileri yönü), tekerlek eksenleri X.

Kullanım: make_car.py CarConcept.glb <çıktı klasörü>
"""
import json
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_mannequin import Writer, accessor_bytes, load_gltf  # noqa: E402

WHEELS = {"WheelFrontL": "FL", "WheelFrontR": "FR", "WheelRearL": "RL", "WheelRearR": "RR"}
VARIANTS = ["Carmine", "Pearl", "Graphite"]
# Fren kaliperi renkleri (doğrusal) ve jant kaplamaları (Rim2 = parlak jant yüzü, Rim1 = koyu iç kısım)
CALIPERS = {"Red": [0.65, 0.0, 0.0], "Yellow": [0.9, 0.55, 0.02], "Black": [0.02, 0.02, 0.022]}
RIMS = {"Chrome": {}, "Black": {"Rim2": {"baseColorFactor": [0.03, 0.03, 0.035, 1.0], "metallicFactor": 0.9, "roughnessFactor": 0.28}},
        "Bronze": {"Rim2": {"baseColorFactor": [0.5, 0.3, 0.12, 1.0], "metallicFactor": 1.0, "roughnessFactor": 0.22}}}
# Z yukarı -> Y yukarı, ardından 180° (ön -Z): yerel (x, y, z) -> dünya (-x, z, y)
ROOT = [-1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1]
KEEP_EXT = ("KHR_materials_emissive_strength", "KHR_materials_transmission")


def mat4(n):
    if "matrix" in n:
        return np.array(n["matrix"], np.float64).reshape(4, 4).T
    m = np.eye(4)
    if "translation" in n:
        m[:3, 3] = n["translation"]
    return m


def sanitize_material(m, plate_tex):
    m = json.loads(json.dumps(m))
    pbr = m.setdefault("pbrMetallicRoughness", {})
    ext = m.get("extensions", {})
    m.pop("occlusionTexture", None)                     # ikinci UV kümesi
    nt = m.get("normalTexture")
    if nt and ("extensions" in nt or nt.get("scale", 1.0) == 0.0):
        m.pop("normalTexture")                          # doku dönüşümlü ayrıntı normali
    bt = pbr.get("baseColorTexture")
    if bt and "extensions" in bt:
        pbr.pop("baseColorTexture")
    if bt and bt.get("index") == 3:
        bt["index"] = plate_tex                         # plaka
    et = m.get("emissiveTexture")
    if et and "KHR_materials_emissive_strength" not in ext:
        m.pop("emissiveTexture")
        m.pop("emissiveFactor", None)
    cc = ext.get("KHR_materials_clearcoat", {}).get("clearcoatFactor", 0.0)
    if cc > 0.0:
        pbr["roughnessFactor"] = min(pbr.get("roughnessFactor", 1.0), 0.18)
    if "KHR_materials_iridescence" in ext:
        pbr["roughnessFactor"] = min(pbr.get("roughnessFactor", 1.0), 0.22)
        pbr["metallicFactor"] = max(pbr.get("metallicFactor", 1.0), 0.6)
    if "KHR_materials_transmission" in ext:
        pbr["baseColorFactor"] = [0.015, 0.017, 0.02, 1.0]
        pbr["roughnessFactor"] = 0.02
        pbr["metallicFactor"] = 0.0
    m["extensions"] = {k: v for k, v in ext.items() if k in KEEP_EXT}
    if not m["extensions"]:
        m.pop("extensions")
    return m


def make_plate():
    """Avrupa tipi plaka: beyaz zemin, mavi şerit, siyah yazı."""
    from PIL import Image, ImageDraw, ImageFont
    img = Image.new("RGB", (512, 128), (236, 236, 232))
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, 511, 127], outline=(20, 20, 20), width=6)
    d.rectangle([6, 6, 62, 121], fill=(24, 60, 170))
    font = None
    for f in ("/usr/share/fonts/google-noto/NotoSans-Bold.ttf", "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf",
              "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"):
        if os.path.exists(f):
            font = ImageFont.truetype(f, 82)
            break
    d.text((290, 64), "CQ 527", fill=(15, 15, 15), anchor="mm", font=font or ImageFont.load_default())
    import io
    buf = io.BytesIO()
    img.save(buf, "PNG")
    return buf.getvalue()


def build(j, bins, root_children, out_path, variant=None, mesh_filter=None, mat_override=None):
    """root_children: [(node index, 4x4 matrix override or None)] -> ROOT altında."""
    w = Writer()
    acc_remap, mesh_remap, mat_remap, tex_remap = {}, {}, {}, {}
    images, textures = [], []
    meshes, materials, nodes = [], [], []

    def copy_acc(ai):
        if ai not in acc_remap:
            acc_remap[ai] = w.accessor(j["accessors"][ai], accessor_bytes(j, bins, ai))
        return acc_remap[ai]

    def copy_tex(ti):
        if ti not in tex_remap:
            if ti == "plate":
                raw, mime = make_plate(), "image/png"
            else:
                img = j["images"][j["textures"][ti]["source"]]
                bv = j["bufferViews"][img["bufferView"]]
                o = bv.get("byteOffset", 0)
                raw, mime = bins[bv["buffer"]][o:o + bv["byteLength"]], img.get("mimeType", "image/png")
            images.append({"bufferView": w.view(raw), "mimeType": mime})
            textures.append({"source": len(images) - 1, "sampler": 0})
            tex_remap[ti] = len(textures) - 1
        return tex_remap[ti]

    def copy_mat(mi):
        if mi not in mat_remap:
            m = sanitize_material(j["materials"][mi], "plate")
            # Varyant (kaliper rengi, jant kaplaması): malzeme adına göre PBR değerleri değişir
            if mat_override and j["materials"][mi].get("name") in mat_override:
                m["pbrMetallicRoughness"].update(mat_override[j["materials"][mi]["name"]])
            pbr = m["pbrMetallicRoughness"]
            for key in ("baseColorTexture", "metallicRoughnessTexture"):
                if key in pbr:
                    pbr[key] = {"index": copy_tex(pbr[key]["index"])}
            for key in ("normalTexture", "emissiveTexture"):
                if key in m:
                    t = {"index": copy_tex(m[key]["index"])}
                    if "scale" in m[key]:
                        t["scale"] = m[key]["scale"]
                    m[key] = t
            materials.append(m)
            mat_remap[mi] = len(materials) - 1
        return mat_remap[mi]

    def copy_mesh(mi):
        if mi not in mesh_remap:
            m = json.loads(json.dumps(j["meshes"][mi]))
            for p in m["primitives"]:
                mat = p.get("material")
                vm = p.get("extensions", {}).get("KHR_materials_variants")
                if vm and variant is not None:
                    for mp in vm["mappings"]:
                        if variant in mp["variants"]:
                            mat = mp["material"]
                p.pop("extensions", None)
                p["attributes"] = {k: copy_acc(v) for k, v in p["attributes"].items() if k in ("POSITION", "NORMAL", "TANGENT", "TEXCOORD_0")}
                if "indices" in p:
                    p["indices"] = copy_acc(p["indices"])
                if mat is not None:
                    p["material"] = copy_mat(mat)
            meshes.append(m)
            mesh_remap[mi] = len(meshes) - 1
        return mesh_remap[mi]

    def wanted(ni, skip):
        """Düğüm (ya da alt ağacı) seçilen ağları içeriyor mu?"""
        n = j["nodes"][ni]
        if "mesh" in n and (mesh_filter is None or mesh_filter(ni)):
            return True
        return any(wanted(c, skip) for c in n.get("children", []) if c not in skip)

    def copy_node(ni, matrix=None, skip=()):
        n = j["nodes"][ni]
        out = {"name": n.get("name", f"node{ni}")}
        m = mat4(n) if matrix is None else matrix
        out["matrix"] = [float(x) for x in m.T.reshape(-1)]
        idx = len(nodes)
        nodes.append(out)
        if "mesh" in n and (mesh_filter is None or mesh_filter(ni)):
            out["mesh"] = copy_mesh(n["mesh"])
        kids = []
        for c in n.get("children", []):
            if c in skip or not wanted(c, skip):
                continue
            kids.append(copy_node(c))
        if kids:
            out["children"] = kids
        return idx

    # Motorun yükleyicisi kök düğümün dönüşümünü uygulamaz: eksen çevirmesi birim kökün altındaki düğümde
    nodes.append({"name": "Root", "children": [1]})
    nodes.append({"name": "Axes", "matrix": ROOT})
    kids = [copy_node(ni, mtx, skip) for ni, mtx, skip in root_children]
    nodes[1]["children"] = kids

    used_ext = sorted({e for m in materials for e in m.get("extensions", {})})
    gj = {"asset": {"version": "2.0", "generator": "Conqueror make_car.py",
                    "copyright": "CarConcept (c) Khronos Group / Eric Chadwick, CC-BY 4.0"},
          "scene": 0, "scenes": [{"nodes": [0]}], "nodes": nodes, "meshes": meshes, "materials": materials,
          "accessors": w.accessors, "bufferViews": w.views, "buffers": [{"byteLength": len(w.data)}],
          "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}]}
    if used_ext:
        gj["extensionsUsed"] = used_ext
    if images:
        gj["images"], gj["textures"] = images, textures
    js = json.dumps(gj, separators=(",", ":")).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    while len(w.data) % 4:
        w.data.append(0)
    with open(out_path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(w.data)))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(w.data), 0x004E4942) + bytes(w.data))
    print(f"{os.path.basename(out_path)}: {len(meshes)} meshes, {len(materials)} materials, {len(w.data) / 1e6:.1f} MB")


def main():
    src, out = sys.argv[1:3]
    os.makedirs(out, exist_ok=True)
    j, bins, _ = load_gltf(src)
    names = {n.get("name"): i for i, n in enumerate(j["nodes"])}
    body = names["BodyUnderside"]
    wheel_nodes = [names[k] for k in WHEELS]
    info = {"wheels": {}}
    # Fren kaliperleri gövdede kalır (tekerlekle dönmez): tekerleğin yönü sıfırlanmış dünya konumuyla
    calipers = []
    for wn in wheel_nodes:
        wm = mat4(j["nodes"][wn])
        pos = np.eye(4)
        pos[:3, 3] = wm[:3, 3]
        for c in j["nodes"][wn].get("children", []):
            if "BrakePad" in j["nodes"][c].get("name", ""):
                calipers.append((c, pos @ mat4(j["nodes"][c])))
        # Dünya (ROOT sonrası) tekerlek merkezi
        r = np.array(ROOT, np.float64).reshape(4, 4).T
        p = r @ np.append(wm[:3, 3], 1.0)
        info["wheels"][WHEELS[j["nodes"][wn]["name"]]] = [round(float(x), 4) for x in p[:3]]
    def is_interior(ni):
        """İç döşeme ağı: boya varyantına göre malzemesi değişen iç parçalar (Interior 3 Carmine / Pearl / Graphite)."""
        mi = j["nodes"][ni].get("mesh")
        if mi is None:
            return False
        for p in j["meshes"][mi]["primitives"]:
            if p.get("material") == 6:
                return True
            for mp in p.get("extensions", {}).get("KHR_materials_variants", {}).get("mappings", []):
                if mp["material"] in (6, 26, 27):
                    return True
        return False

    def is_rim(ni):
        return "Rim" in j["nodes"][ni].get("name", "")

    body_parts = [(body, np.eye(4), set(wheel_nodes))]
    for vi, vname in enumerate(VARIANTS):
        # Gövde düğümü kendi Z-yukarı matrisini taşır: ROOT zaten çevirdiği için gövdeninki birim olmalı
        build(j, bins, body_parts, os.path.join(out, f"Body_{vname}.glb"), vi, mesh_filter=lambda ni: not is_interior(ni))
        build(j, bins, body_parts, os.path.join(out, f"Interior_{vname}.glb"), vi, mesh_filter=is_interior)
    for cname, color in CALIPERS.items():
        build(j, bins, [(c, m, ()) for c, m in calipers], os.path.join(out, f"Calipers_{cname}.glb"),
              mat_override={"Brake": {"baseColorFactor": color + [1.0], "roughnessFactor": 0.3}})
    for wn in wheel_nodes:
        n = j["nodes"][wn]
        skip = {c for c in n.get("children", []) if "BrakePad" in j["nodes"][c].get("name", "")}
        pos = WHEELS[n["name"]]
        build(j, bins, [(wn, np.eye(4), skip)], os.path.join(out, f"Wheel_{pos}.glb"), mesh_filter=lambda ni: not is_rim(ni))
        for fname, ov in RIMS.items():
            build(j, bins, [(wn, np.eye(4), skip)], os.path.join(out, f"Rim_{pos}_{fname}.glb"), mesh_filter=is_rim, mat_override=ov)
    # Tekerlek yarıçapı (lastik ağının tüm parçaları)
    tire = next(c for c in j["nodes"][wheel_nodes[0]]["children"] if "mesh" in j["nodes"][c] and not j["nodes"][c].get("name"))
    prims = j["meshes"][j["nodes"][tire]["mesh"]]["primitives"]
    pmin = np.min([j["accessors"][p["attributes"]["POSITION"]]["min"] for p in prims], axis=0)
    pmax = np.max([j["accessors"][p["attributes"]["POSITION"]]["max"] for p in prims], axis=0)
    info["wheelRadius"] = round(float(max(pmax[1] - pmin[1], pmax[2] - pmin[2]) / 2), 4)
    info["variants"] = VARIANTS
    with open(os.path.join(out, "car.json"), "w") as f:
        json.dump(info, f, indent=1)
    print(info)


if __name__ == "__main__":
    main()

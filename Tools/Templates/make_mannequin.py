#!/usr/bin/env python3
"""Builds the Conqueror Mannequin characters used by the project templates.

usage: make_mannequin.py <Universal Base Characters dir> <UAL1 .glb> <UAL2 .glb> <out dir>

Sources (both CC0, by Quaternius): "Universal Base Characters" (Superhero male / female) and the
"Universal Animation Library" 1 + 2. They share one UE-style skeleton, so animations are moved by bone name:
local rotations are copied as they are, bone lengths stay the character's own (translation tracks are dropped
except the pelvis, which is scaled by the hip-height ratio).

Writes, per body (Male / Female):
  Mannequin_<Body>.glb   faceless light-grey mannequin (normal map keeps the muscle detail), Unreal-template style
  Human_<Body>.glb       the textured character (skin, eyes, brows)
Each file contains the clips listed in CLIPS in that order (ClipIndex = position, 0-based),
plus clips.json with the names.
"""
import base64
import json
import os
import struct
import sys

import numpy as np

# (kaynak: 1 = UAL1, 2 = UAL2, kaynak adı, yeni ad)
CLIPS = [
    (1, "Idle_Loop", "idle"), (1, "Walk_Loop", "walk"), (1, "Jog_Fwd_Loop", "jog"), (1, "Sprint_Loop", "sprint"),
    (1, "Jump_Start", "jump_start"), (1, "Jump_Loop", "jump_loop"), (1, "Jump_Land", "jump_land"),
    (1, "Crouch_Idle_Loop", "crouch_idle"), (1, "Crouch_Fwd_Loop", "crouch_walk"), (1, "Roll", "roll"),
    (1, "Pistol_Idle_Loop", "pistol_idle"), (1, "Pistol_Aim_Neutral", "pistol_aim"), (1, "Pistol_Aim_Up", "pistol_aim_up"),
    (1, "Pistol_Aim_Down", "pistol_aim_down"), (1, "Pistol_Shoot", "pistol_shoot"), (1, "Pistol_Reload", "pistol_reload"),
    (1, "Hit_Chest", "hit"), (1, "Hit_Head", "hit_head"), (1, "Death01", "death"), (1, "Punch_Jab", "punch_jab"),
    (1, "Punch_Cross", "punch_cross"), (1, "Interact", "interact"), (1, "PickUp_Table", "pickup"),
    (1, "Driving_Loop", "driving"), (1, "Sitting_Idle_Loop", "sit_idle"), (1, "Sitting_Talking_Loop", "sit_talk"),
    (1, "Idle_Talking_Loop", "talk"), (1, "Walk_Formal_Loop", "walk_formal"), (1, "Dance_Loop", "dance"),
    (1, "Sword_Idle", "sword_idle"), (1, "Sword_Attack", "sword_attack"), (1, "Swim_Idle_Loop", "swim_idle"),
    (1, "Swim_Fwd_Loop", "swim"), (1, "Push_Loop", "push"), (1, "Fixing_Kneeling", "fixing"),
    (2, "Idle_FoldArms_Loop", "idle_arms_folded"), (2, "Idle_TalkingPhone_Loop", "phone"), (2, "ClimbUp_1m", "climb_up"),
    (2, "Slide_Start", "slide_start"), (2, "Slide_Loop", "slide"), (2, "Slide_Exit", "slide_exit"),
    (2, "Hit_Knockback", "knockback"), (2, "OverhandThrow", "throw"), (2, "Walk_Carry_Loop", "carry"),
    (2, "Zombie_Walk_Fwd_Loop", "zombie_walk"), (2, "Zombie_Idle_Loop", "zombie_idle"), (2, "Zombie_Scratch", "zombie_attack"),
    (2, "Yes", "yes"), (2, "Idle_No_Loop", "no"), (2, "Chest_Open", "open_chest"),
]

COMP = {5126: 4, 5123: 2, 5125: 4, 5121: 1, 5122: 2, 5120: 1}
NCOMP = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def load_gltf(path):
    if path.endswith(".glb"):
        d = open(path, "rb").read()
        jl = struct.unpack("<I", d[12:16])[0]
        j = json.loads(d[20:20 + jl])
        bl = struct.unpack("<I", d[20 + jl:24 + jl])[0]
        bins = [d[28 + jl:28 + jl + bl]]
        base = os.path.dirname(path)
    else:
        j = json.load(open(path))
        base = os.path.dirname(path)
        bins = []
        for b in j["buffers"]:
            uri = b["uri"]
            bins.append(base64.b64decode(uri.split(",", 1)[1]) if uri.startswith("data:") else open(os.path.join(base, uri), "rb").read())
    return j, bins, base


def accessor_bytes(j, bins, ai):
    a = j["accessors"][ai]
    bv = j["bufferViews"][a["bufferView"]]
    n = NCOMP[a["type"]] * COMP[a["componentType"]]
    stride = bv.get("byteStride", n)
    start = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    buf = bins[bv["buffer"]]
    if stride == n:
        return bytes(buf[start:start + n * a["count"]])
    return b"".join(buf[start + i * stride:start + i * stride + n] for i in range(a["count"]))


def find_image(base, uri):
    """Some image URIs in the pack point to renamed files ("X_png.png" -> "X.png", or under Textures/)."""
    cands = [uri, uri.replace("_png.png", ".png")]
    for d in (base, os.path.join(base, "..", "Textures"), os.path.join(base, "..", "Textures", "Normals Unity - Godot")):
        for c in cands:
            p = os.path.join(d, c)
            if os.path.exists(p):
                return p
    raise FileNotFoundError(uri)


ARM_BONES = ("upperarm", "lowerarm", "hand", "thumb", "index", "middle", "ring", "pinky")
DTYPE = {5121: np.uint8, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}


def read_array(j, bins, ai):
    a = j["accessors"][ai]
    arr = np.frombuffer(accessor_bytes(j, bins, ai), DTYPE[a["componentType"]])
    return arr.reshape(a["count"], NCOMP[a["type"]]) if NCOMP[a["type"]] > 1 else arr


def arm_indices(j, bins, w, prim, node):
    """First-person arms: keep the triangles whose three vertices are mostly driven by arm / hand bones."""
    joints = read_array(j, bins, prim["attributes"]["JOINTS_0"])
    weights = read_array(j, bins, prim["attributes"]["WEIGHTS_0"]).astype(np.float32)
    skin = j["skins"][node["skin"]]
    names = [j["nodes"][k]["name"] for k in skin["joints"]]
    is_arm = np.array([any(b in nm for b in ARM_BONES) for nm in names])
    arm_weight = (weights * is_arm[joints]).sum(axis=1) / np.maximum(weights.sum(axis=1), 1e-6)
    idx = read_array(j, bins, prim["indices"]).reshape(-1, 3)
    keep = idx[(arm_weight[idx] > 0.5).all(axis=1)].astype(np.uint32).reshape(-1)
    acc = {"componentType": 5125, "count": int(keep.size), "type": "SCALAR"}
    return w.accessor(acc, keep.tobytes())


class Writer:
    """Accumulates one binary buffer for the output .glb."""

    def __init__(self):
        self.data = bytearray()
        self.views = []
        self.accessors = []

    def view(self, raw, target=None):
        while len(self.data) % 4:
            self.data.append(0)
        v = {"buffer": 0, "byteOffset": len(self.data), "byteLength": len(raw)}
        if target:
            v["target"] = target
        self.data += raw
        self.views.append(v)
        return len(self.views) - 1

    def accessor(self, src, raw):
        a = {k: v for k, v in src.items() if k not in ("bufferView", "byteOffset", "sparse")}
        a["bufferView"] = self.view(raw)
        self.accessors.append(a)
        return len(self.accessors) - 1


def build(char_path, ual1, ual2, out_path, mannequin, arms_only=False):
    j, bins, base = load_gltf(char_path)
    w = Writer()
    remap = {}

    def copy_acc(ai):
        if ai not in remap:
            remap[ai] = w.accessor(j["accessors"][ai], accessor_bytes(j, bins, ai))
        return remap[ai]

    nodes = j["nodes"]
    keep_mesh_nodes = set()
    for i, n in enumerate(nodes):
        if "mesh" in n:
            body = "skin" in n and n["name"] not in ("Eyebrows", "Eyes")
            if not mannequin or body:
                keep_mesh_nodes.add(i)
    # Ağlar
    meshes = []
    mesh_remap = {}
    for i, n in enumerate(nodes):
        if "mesh" not in n:
            continue
        if i not in keep_mesh_nodes:
            del n["mesh"]
            n.pop("skin", None)
            continue
        mi = n["mesh"]
        if mi not in mesh_remap:
            m = json.loads(json.dumps(j["meshes"][mi]))
            for p in m["primitives"]:
                if arms_only and "indices" in p and "JOINTS_0" in p["attributes"]:
                    p["indices"] = arm_indices(j, bins, w, p, n)
                elif "indices" in p:
                    p["indices"] = copy_acc(p["indices"])
                p["attributes"] = {k: copy_acc(v) for k, v in p["attributes"].items()}
                p.pop("targets", None)
            meshes.append(m)
            mesh_remap[mi] = len(meshes) - 1
        n["mesh"] = mesh_remap[mi]
    skins = []
    for s in j.get("skins", []):
        s = dict(s)
        s["inverseBindMatrices"] = copy_acc(s["inverseBindMatrices"])
        skins.append(s)
    # Malzemeler ve dokular
    images, textures, samplers = [], [], j.get("samplers", [{}])
    tex_remap = {}

    def copy_tex(ti):
        if ti in tex_remap:
            return tex_remap[ti]
        t = j["textures"][ti]
        img = j["images"][t["source"]]
        if "uri" in img:
            raw = open(find_image(base, img["uri"]), "rb").read()
            mime = "image/png" if img["uri"].lower().endswith(".png") else "image/jpeg"
        else:
            bv = j["bufferViews"][img["bufferView"]]
            raw = bins[bv["buffer"]][bv.get("byteOffset", 0):bv.get("byteOffset", 0) + bv["byteLength"]]
            mime = img.get("mimeType", "image/png")
        images.append({"bufferView": w.view(raw), "mimeType": mime})
        textures.append({"source": len(images) - 1, "sampler": t.get("sampler", 0)})
        tex_remap[ti] = len(textures) - 1
        return tex_remap[ti]

    materials = []
    for m in j["materials"]:
        m = json.loads(json.dumps(m))
        pbr = m.setdefault("pbrMetallicRoughness", {})
        if mannequin:
            # Unreal şablon mankeni gibi: açık gri, hafif parlak; kas ayrıntısı normal haritasından gelir
            pbr.pop("baseColorTexture", None)
            pbr["baseColorFactor"] = [0.45, 0.45, 0.46, 1.0]   # doğrusal (≈ sRGB 0.70)
            pbr["metallicFactor"] = 0.1
            pbr["roughnessFactor"] = 0.45
            pbr.pop("metallicRoughnessTexture", None)
        for key in ("baseColorTexture", "metallicRoughnessTexture"):
            if key in pbr:
                pbr[key]["index"] = copy_tex(pbr[key]["index"])
        for key in ("normalTexture", "occlusionTexture", "emissiveTexture"):
            if key in m:
                m[key]["index"] = copy_tex(m[key]["index"])
        m.pop("extensions", None)
        materials.append(m)

    # Animasyonlar (ad eşleşmesiyle)
    name_to_node = {n["name"]: i for i, n in enumerate(nodes)}
    pelvis_h = nodes[name_to_node["pelvis"]]["translation"][2]
    animations = []
    sources = {}
    for src, sname, newname in CLIPS:
        path = ual1 if src == 1 else ual2
        if path not in sources:
            sources[path] = load_gltf(path)
        sj, sb, _ = sources[path]
        anim = next(a for a in sj["animations"] if a["name"] == sname)
        src_pelvis_h = next(n for n in sj["nodes"] if n["name"] == "pelvis")["translation"][2]
        out = {"name": newname, "channels": [], "samplers": []}
        for ch in anim["channels"]:
            bone = sj["nodes"][ch["target"]["node"]]["name"]
            path_ = ch["target"]["path"]
            if bone not in name_to_node or path_ == "scale":
                continue
            if path_ == "translation" and bone != "pelvis":
                continue
            s = anim["samplers"][ch["sampler"]]
            inp = w.accessor(sj["accessors"][s["input"]], accessor_bytes(sj, sb, s["input"]))
            raw = accessor_bytes(sj, sb, s["output"])
            oacc = dict(sj["accessors"][s["output"]])
            if path_ == "translation":
                v = np.frombuffer(raw, np.float32).reshape(-1, 3) * (pelvis_h / src_pelvis_h)
                raw = v.astype(np.float32).tobytes()
                oacc["min"] = v.min(axis=0).tolist()
                oacc["max"] = v.max(axis=0).tolist()
            outp = w.accessor(oacc, raw)
            out["samplers"].append({"input": inp, "output": outp, "interpolation": s.get("interpolation", "LINEAR")})
            out["channels"].append({"sampler": len(out["samplers"]) - 1, "target": {"node": name_to_node[bone], "path": path_}})
        animations.append(out)

    gj = {"asset": {"version": "2.0", "generator": "Conqueror make_mannequin.py"}, "scene": 0, "scenes": j["scenes"],
          "nodes": nodes, "meshes": meshes, "skins": skins, "materials": materials, "animations": animations,
          "accessors": w.accessors, "bufferViews": w.views, "buffers": [{"byteLength": len(w.data)}]}
    if images:
        gj["images"], gj["textures"], gj["samplers"] = images, textures, samplers
    js = json.dumps(gj, separators=(",", ":")).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    while len(w.data) % 4:
        w.data.append(0)
    total = 12 + 8 + len(js) + 8 + len(w.data)
    with open(out_path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(w.data), 0x004E4942) + bytes(w.data))
    print(f"{os.path.basename(out_path)}: {len(animations)} clips, {len(w.data) / 1e6:.1f} MB")


def main():
    ubc, ual1, ual2, out = sys.argv[1:5]
    os.makedirs(out, exist_ok=True)
    gd = os.path.join(ubc, "Base Characters", "Godot - UE")
    for body in ("Male", "Female"):
        src = os.path.join(gd, f"Superhero_{body}_FullBody.gltf")
        build(src, ual1, ual2, os.path.join(out, f"Mannequin_{body}.glb"), True)
        build(src, ual1, ual2, os.path.join(out, f"Human_{body}.glb"), False)
        build(src, ual1, ual2, os.path.join(out, f"Arms_{body}.glb"), True, arms_only=True)
    with open(os.path.join(out, "clips.json"), "w") as f:
        json.dump([c[2] for c in CLIPS], f, indent=0)   # ClipIndex = konum (0 tabanlı)


if __name__ == "__main__":
    main()

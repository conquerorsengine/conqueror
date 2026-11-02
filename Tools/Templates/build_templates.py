#!/usr/bin/env python3
"""Builds the scenes of the folder project templates (Resources/Templates/Projects/<id>).

usage: build_templates.py [template id ...]      (no ids = all)

For every template: a work project is assembled in ~/.cache/cq-build/templates/<id> from the template folder plus the
shared packages listed in its Template.yaml (Resources/Templates/Shared/<package> -> Assets/<package>). Each scene
builder Tools/Templates/<id>/<Scene>.cqs (prefixed with Tools/Templates/TemplateKit.cqs and any generated data files
Tools/Templates/<id>/*.inc) runs in the editor through
automation and the saved .cqscene files are copied back into the template folder. Shared packages are not copied
back: the launcher adds them when it creates a project.

Environment: CQ_EDITOR (default build/bin/ConquerorEngine). Headless runs need a compositor (see Docs/Testing.md);
CQ_RUNNER can name a wrapper script that takes <automation file> <project dir>.
"""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TEMPLATES = os.path.join(ROOT, "Resources", "Templates")
WORK = os.path.join(os.path.expanduser("~"), ".cache", "cq-build", "templates")

PROJECT = """Project:
  Name: {name}
  Organization: ""
  Version: 1.0.0
  TargetPlatform: ""
  StartScene: Assets/Scenes/Main.cqscene
  AssetDirectory: Assets
  ScriptModulePath: ""
"""


# Laser Tag şablonuna dokunulmaz (kullanıcı kararı)
NO_BAKE = {"lasertag"}


def read_meta(path):
    meta = {"Shared": [], "Scenes": []}
    key = None
    for line in open(path):
        s = line.rstrip()
        if not s or s.lstrip().startswith("#"):
            continue
        if s.startswith("  - ") and key:
            meta[key].append(s[4:].strip())
        elif ":" in s:
            k, v = s.split(":", 1)
            key = k.strip()
            v = v.strip()
            if v.startswith("["):
                meta[key] = [x.strip() for x in v.strip("[]").split(",") if x.strip()]
            elif v:
                meta[key] = v
            else:
                meta[key] = []
    return meta


def build(tid):
    src = os.path.join(TEMPLATES, "Projects", tid)
    meta = read_meta(os.path.join(src, "Template.yaml"))
    work = os.path.join(WORK, tid)
    if os.path.isdir(work):
        shutil.rmtree(work)
    shutil.copytree(src, work, ignore=shutil.ignore_patterns("Template.yaml"))
    for pkg in meta["Shared"]:
        shutil.copytree(os.path.join(TEMPLATES, "Shared", pkg), os.path.join(work, "Assets", pkg), dirs_exist_ok=True)
    # Eklentiler (Template.yaml "Addons"): hub da projeye Packages/<eklenti> olarak kurar
    for addon in meta.get("Addons", []):
        shutil.copytree(os.path.join(ROOT, "Resources", "Addons", addon), os.path.join(work, "Packages", addon), dirs_exist_ok=True)
    os.makedirs(os.path.join(work, "Assets", "Scenes"), exist_ok=True)
    with open(os.path.join(work, "Template.cqproj"), "w") as f:
        f.write(PROJECT.format(name=tid))
    kit = open(os.path.join(HERE, "TemplateKit.cqs"), encoding="utf-8").read()
    # Üretilmiş veri dosyaları (ör. make_track.py -> Track.inc) her sahne kurucunun önüne eklenir
    for inc in sorted(n for n in os.listdir(os.path.join(HERE, tid)) if n.endswith(".inc")):
        kit += "\n" + open(os.path.join(HERE, tid, inc), encoding="utf-8").read()
    auto = ["wait 30"]
    builders = sorted(n for n in os.listdir(os.path.join(HERE, tid)) if n.endswith(".cqs"))
    for b in builders:
        scene = b[:-4]
        code = kit + "\n" + open(os.path.join(HERE, tid, b), encoding="utf-8").read().replace("using nativebridge;", "") + \
            '\nCreateEntity("BuildSceneDone");\n'
        combined = os.path.join(work, f"_build_{scene}.cqs")
        with open(combined, "w", encoding="utf-8") as f:
            f.write(code)
        out = os.path.join(work, "Assets", "Scenes", scene + ".cqscene")
        auto += ["new_scene", f'run_cqs_file "{combined}"', "wait 5", "entity expect_active BuildSceneDone true",
                 "entity delete BuildSceneDone"]
        # Kurucudaki "// @EditorCamera <uzaklık> <eğim> <yön> <odak x y z>" satırı: proje açıldığında editör görünümü
        # (sahneyle kaydedilir; yoksa varsayılan 10 m bakış kalır)
        for line in open(os.path.join(HERE, tid, b), encoding="utf-8"):
            if line.strip().startswith("// @EditorCamera "):
                auto.append("camera " + " ".join(line.strip()[len("// @EditorCamera "):].split()[:6]))
        # Gömülü (Instance) malzemeler -> Assets/Materials/*.cqmat (Mesh Renderer yalnız malzeme dosyasına başvurur)
        if tid not in NO_BAKE:
            auto.append("material_bake Assets/Materials")
        auto.append(f'save_scene_as "{out}"')
    auto.append("quit")
    script = os.path.join(work, "_build.txt")
    with open(script, "w") as f:
        f.write("\n".join(auto) + "\n")
    runner = os.environ.get("CQ_RUNNER")
    if runner:
        subprocess.run(["bash", runner, script, work], check=False)
    else:
        env = dict(os.environ, CQ_AUTOMATION=script)
        subprocess.run([os.environ.get("CQ_EDITOR", os.path.join(ROOT, "build", "bin", "ConquerorEngine")), work], env=env,
                       cwd=os.path.join(ROOT, "build", "bin"), check=False)
    result = open(script + ".result.txt").read() if os.path.exists(script + ".result.txt") else ""
    ok = "DONE failures=0" in result
    dst = os.path.join(src, "Assets", "Scenes")
    os.makedirs(dst, exist_ok=True)
    for b in builders:
        sc = os.path.join(work, "Assets", "Scenes", b[:-4] + ".cqscene")
        # Sahneye kurucunun yapamadığı veriyi ekleyen son adım (ör. post_Offroad.py: arazi yükseklik haritası)
        post = os.path.join(HERE, tid, "post_" + b[:-4] + ".py")
        if os.path.exists(sc) and os.path.exists(post):
            ok = subprocess.run([sys.executable, post, sc], check=False).returncode == 0 and ok
        if os.path.exists(sc):
            shutil.copy(sc, os.path.join(dst, b[:-4] + ".cqscene"))
    mats = os.path.join(work, "Assets", "Materials")
    if os.path.isdir(mats):
        dst_mats = os.path.join(src, "Assets", "Materials")
        if os.path.isdir(dst_mats):
            shutil.rmtree(dst_mats)
        shutil.copytree(mats, dst_mats)
    print(f"{tid}: {'OK' if ok else 'FAILED'} ({len(builders)} scenes)")
    if not ok:
        print("\n".join(l for l in result.splitlines() if "FAIL" in l or "ERROR" in l))
    return ok


def main():
    ids = sys.argv[1:] or sorted(d for d in os.listdir(os.path.join(TEMPLATES, "Projects"))
                                 if os.path.exists(os.path.join(TEMPLATES, "Projects", d, "Template.yaml")))
    results = [build(t) for t in ids]
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()

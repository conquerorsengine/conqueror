#!/usr/bin/env python3
"""Hub template card pictures: Resources/Templates/Thumbnails/<id>.jpg (640 x 360).

usage: make_thumbnails.py [template id ...]      (no ids = every sample template)

For every template a fresh project is created with the hub ("hub create"), the editor opens it, presses Play, waits
until the scene has settled, hides the UI canvases (HUD, hints) and takes a screenshot. The game view part of the
screenshot is cropped and scaled to 640 x 360. Templates without a picture keep the gradient + icon card art.

Environment (headless runs need a compositor, see Docs/Testing.md):
  CQ_RUNNER      wrapper "<automation file> <project dir>" that runs the editor (same as build_templates.py)
  CQ_HUB_RUNNER  wrapper "<automation file>" that runs the hub (ConquerorLauncher)
  CQ_THUMB_RECT  "x0 y0 x1 y1": game view rectangle in the screenshot. Default: the editor's default layout at
                 1920 x 1080 (game view right of the hierarchy, above the content browser).
"""
import os
import shutil
import subprocess
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
BIN = os.path.join(ROOT, "build", "bin")
OUT = os.path.join(ROOT, "Resources", "Templates", "Thumbnails")
WORK = os.path.join(os.path.expanduser("~"), ".cache", "cq-build", "thumbnails")
RECT = tuple(int(v) for v in os.environ.get("CQ_THUMB_RECT", "405 185 1437 770").split())

# Şablon başına oynatma süresi (kare) ve isteğe bağlı ek otomasyon satırları (ör. kamera / tuş). Çekirdek şablonlar
# (empty, basic3d, basic2d) resimsiz kalır: kartlarında degrade + ikon.
SHOTS = {
    "platformer2d": {"frames": 150},
    "fps": {"frames": 120},
    "arenashooter": {"frames": 1000, "aim": "fps", "sturdy": True},   # botlar oyuncuyu arayıp yaklaşır
    "tpsshooter": {"frames": 360, "aim": "tps"},
    "lasertag": {"frames": 260},
    "boardgame": {"frames": 240},
    "thirdperson": {"frames": 150},
    "topdown": {"frames": 150},
    "vehicle": {"frames": 360},
    "virtualproduction": {"frames": 420},      # ikinci çekim (iki kişilik plan)
    "archviz": {"frames": 160},
    "configurator": {"frames": 200},
    "crowdsim": {"frames": 360},
    "vr": {"frames": 150},
    "ar": {"frames": 420},                     # düzlemler bulunmuş olsun
    "featuretest": {"frames": 240},
}

# Nişancılar: oyuncu doğduğu yerde siper duvarına bakar; çekimden önce bakış görüş hattındaki en yakın bota çevrilir
# (FirstPersonShooter yaw / pitch, MannequinCharacter controlYaw / controlPitch). Görünen bot yoksa arena ortasına.
AIM = """using nativebridge;
var player = FindEntity("Player");
var eye = EntityGetWorldPosition(player);
eye = {x: eye.x, y: eye.y + 1.6, z: eye.z};
var best = null;
var bestD = 1000.0;
foreach (var bot in FindEntitiesWithScript("ShooterAI"))
{
    var p = EntityGetWorldPosition(bot);
    var t = {x: p.x, y: p.y + 1.1, z: p.z};
    var dx = t.x - eye.x;
    var dy = t.y - eye.y;
    var dz = t.z - eye.z;
    var d = MathSqrt(dx * dx + dy * dy + dz * dz);
    if (d < 2.0 || d > bestD) { continue; }
    var hit = Raycast(eye.x + dx / d * 0.6, eye.y + dy / d * 0.6, eye.z + dz / d * 0.6, dx / d, dy / d, dz / d, d - 0.8, -1, false);
    if (hit == null) { best = t; bestD = d; }
}
if (best == null) { best = {x: 0.0, y: 1.0, z: 0.0}; }
var ax = best.x - eye.x;
var ay = best.y - eye.y;
var az = best.z - eye.z;
var al = MathMax(0.001, MathSqrt(ax * ax + ay * ay + az * az));
var yaw = MathRadToDeg(MathAtan2(-ax, -az));
var pitch = MathRadToDeg(MathAsin(ay / al));
if (MODE == "fps") { SetScriptField(player, "yaw", yaw); SetScriptField(player, "pitch", pitch); }
else { SetScriptField(player, "controlYaw", yaw); SetScriptField(player, "controlPitch", pitch - 8.0); }
LogInfo("THUMB aim " + MODE + " yaw=" + yaw + " pitch=" + pitch + " target_distance=" + bestD);
"""

HIDE_UI = """using nativebridge;
// Küçük resim: arayüz katmanları (HUD, ipuçları) görünmesin
foreach (var c in FindEntitiesWithComponent("Canvas")) { EntitySetActive(c, false); }
"""


def run_hub(script):
    runner = os.environ.get("CQ_HUB_RUNNER")
    if runner:
        subprocess.run(["bash", runner, script], check=False)
    else:
        subprocess.run([os.path.join(BIN, "ConquerorLauncher")], env=dict(os.environ, CQ_AUTOMATION=script), cwd=BIN, check=False)


def run_editor(script, project):
    runner = os.environ.get("CQ_RUNNER")
    if runner:
        subprocess.run(["bash", runner, script, project], check=False)
    else:
        subprocess.run([os.path.join(BIN, "ConquerorEngine"), project], env=dict(os.environ, CQ_AUTOMATION=script), cwd=BIN, check=False)


def shoot(tid):
    spec = SHOTS[tid]
    os.makedirs(WORK, exist_ok=True)
    name = "Thumb_" + tid
    project = os.path.join(WORK, name)
    if os.path.isdir(project):
        shutil.rmtree(project)
    hub = os.path.join(WORK, f"_hub_{tid}.txt")
    with open(hub, "w") as f:
        f.write(f'wait 20\nhub create {tid} {name} "{WORK}"\nwait 5\nquit\n')
    run_hub(hub)
    if not os.path.isdir(project):
        print(f"{tid}: project was not created")
        return False
    hide = os.path.join(WORK, "_hide_ui.cqs")
    with open(hide, "w") as f:
        f.write(HIDE_UI)
    shot = os.path.join(WORK, f"{tid}.png")
    if os.path.exists(shot):
        os.remove(shot)
    extra = list(spec.get("extra", []))
    pre = []
    if spec.get("sturdy"):
        # Çekim bitene kadar oyuncu ölmesin (botlar onu arayıp görüş alanına gelsin)
        sturdy = os.path.join(WORK, "_sturdy.cqs")
        with open(sturdy, "w") as f:
            f.write('using nativebridge;\nvar p = FindEntity("Player");\nSetScriptField(p, "maxHealth", 1000000.0);\n'
                    'SetScriptField(p, "health", 1000000.0);\n')
        pre = ["wait 20", f'run_cqs_file "{sturdy}"']
    if spec.get("aim"):
        aim = os.path.join(WORK, f"_aim_{tid}.cqs")
        with open(aim, "w") as f:
            f.write(AIM.replace("MODE", '"' + spec["aim"] + '"'))
        # FPS bakışı aynı karede uygulanır (bot kaçmadan çek); TPS kamerası yay kolla yumuşak döner
        extra += [f'run_cqs_file "{aim}"', "wait 3" if spec["aim"] == "fps" else "wait 30"]
    lines = ["wait 30", "focus Game", "play"] + pre + [f"wait {spec['frames']}"] + extra + \
            [f'run_cqs_file "{hide}"', "wait 4", f'screenshot "{shot}"', "stop", "quit"]
    auto = os.path.join(WORK, f"_shot_{tid}.txt")
    with open(auto, "w") as f:
        f.write("\n".join(lines) + "\n")
    run_editor(auto, project)
    if not os.path.exists(shot):
        print(f"{tid}: no screenshot")
        return False
    img = Image.open(shot).convert("RGB").crop(RECT)
    # 16:9 ortadan kırp, 640 x 360
    w, h = img.size
    tw, th = (w, round(w * 9 / 16)) if w * 9 / 16 <= h else (round(h * 16 / 9), h)
    x0, y0 = (w - tw) // 2, (h - th) // 2
    img = img.crop((x0, y0, x0 + tw, y0 + th)).resize((640, 360), Image.LANCZOS)
    os.makedirs(OUT, exist_ok=True)
    img.save(os.path.join(OUT, tid + ".jpg"), quality=88, optimize=True, progressive=True)
    print(f"{tid}: OK")
    return True


def main():
    ids = sys.argv[1:] or sorted(SHOTS)
    unknown = [t for t in ids if t not in SHOTS]
    if unknown:
        sys.exit("no thumbnail settings for: " + " ".join(unknown))
    results = [shoot(t) for t in ids]
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()

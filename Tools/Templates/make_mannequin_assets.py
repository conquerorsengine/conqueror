#!/usr/bin/env python3
"""Animation controller + montages for the Conqueror Mannequin (Resources/Templates/Shared/Mannequin).

usage: make_mannequin_assets.py [Mannequin dir]

Mannequin.cqcont parameters (set by Scripts/MannequinAnimation.cqs):
  Speed (float, m/s)  Grounded (bool)  Crouch (bool)  Jump (trigger)  Aim (float 0..1, upper-body pistol layer)
  Dead (bool)
Montages (upper body from spine_01 unless noted): PistolShoot, PistolReload, HitReact, Punch, Interact, Throw.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
DIR = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "Resources", "Templates", "Shared", "Mannequin")


def main():
    clips = json.load(open(os.path.join(DIR, "Models", "clips.json")))
    idx = {n: i for i, n in enumerate(clips)}

    def st(name, clip, loop=True, x=0, y=0, speed=1.0):
        return (f"        - Name: {name}\n          ClipIndex: {idx[clip]}\n          ClipName: {clip}\n          Loop: {str(loop).lower()}\n"
                f"          Speed: {speed}\n          EditorPosX: {x}\n          EditorPosY: {y}\n")

    def tree(name, param, children, x, y):
        s = f"        - Name: {name}\n          Type: BlendTree\n          EditorPosX: {x}\n          EditorPosY: {y}\n          BlendType: 0\n          BlendParameter: {param}\n          BlendChildren:\n"
        for clip, th, *ts in children:
            s += f"            - ClipIndex: {idx[clip]}\n              ClipName: {clip}\n              Threshold: {th}\n"
            if ts:
                s += f"              TimeScale: {ts[0]}\n"
        return s

    def tr(a, b, dur, conds=(), exit_time=None):
        s = f"        - From: {a}\n          To: {b}\n          HasExitTime: {'true' if exit_time is not None else 'false'}\n"
        if exit_time is not None:
            s += f"          ExitTime: {exit_time}\n"
        s += f"          Duration: {dur}\n"
        if conds:
            s += "          Conditions:\n"
            for p, mode, *th in conds:
                s += f"            - Parameter: {p}\n              Mode: {mode}\n"
                if th:
                    s += f"              Threshold: {th[0]}\n"
        return s

    base = ("    - Name: Base Layer\n      Weight: 1\n      DefaultState: Locomotion\n      States:\n"
            # Eşik = klibin ayakları kaymadan oynadığı hız: yere basan ayağın gövdeye göre geriye süpürülme hızı (ileri
            # kinematikle ölçülür, oyunda GetBonePosition ile doğrulandı). Yürüme 1.03, koşu 6.2 (uzun sıçramalı koşu),
            # depar 9.6, çömelik yürüme 0.79 m/s. Koşu klibi zaman ölçeğiyle hızlandırılırsa adım uzunluğu değişmez,
            # yalnız ayak daha çok kayar; bu yüzden 4 m/s'de yürüme ile koşu karışır (adım kısalır, ayak kaymaz).
            + tree("Locomotion", "Speed", [("idle", 0), ("walk", 1.03), ("jog", 6.2), ("sprint", 9.6)], 260, 170)
            + tree("Crouch", "Speed", [("crouch_idle", 0), ("crouch_walk", 1.25, 1.58)], 260, 330)
            + st("JumpStart", "jump_start", False, 520, 60)
            + st("Falling", "jump_loop", True, 760, 60)
            + st("Land", "jump_land", False, 760, 200, 1.4)
            + st("Death", "death", False, 520, 420)
            + "      Transitions:\n"
            + tr("Any State", "Death", 0.15, [("Dead", "If")])
            + tr("Locomotion", "JumpStart", 0.08, [("Jump", "If")])
            + tr("Crouch", "JumpStart", 0.1, [("Jump", "If")])
            + tr("JumpStart", "Falling", 0.15, exit_time=0.8)
            + tr("Locomotion", "Falling", 0.2, [("Grounded", "IfNot")])
            + tr("Crouch", "Falling", 0.2, [("Grounded", "IfNot")])
            + tr("Falling", "Land", 0.08, [("Grounded", "If")])
            + tr("JumpStart", "Land", 0.08, [("Grounded", "If")])
            + tr("Land", "Locomotion", 0.2, exit_time=0.45)
            + tr("Land", "Locomotion", 0.12, [("Speed", "Greater", 0.5)])
            + tr("Locomotion", "Crouch", 0.2, [("Crouch", "If")])
            + tr("Crouch", "Locomotion", 0.2, [("Crouch", "IfNot")])
            + tr("Death", "Locomotion", 0.3, [("Dead", "IfNot")]))
    upper = ("    - Name: Upper Body\n      Weight: 1\n      Blending: 0\n      FromBone: spine_01\n      WeightParameter: Aim\n"
             "      DefaultState: PistolAim\n      States:\n" + st("PistolAim", "pistol_aim", True, 260, 170) + "      Transitions: []\n")
    params = [("Speed", "Float", 0), ("Grounded", "Bool", 1), ("Crouch", "Bool", 0), ("Jump", "Trigger", 0),
              ("Aim", "Float", 0), ("Dead", "Bool", 0)]
    out = "AnimationController:\n  Version: 2\n  Layers:\n" + base + upper + "  Parameters:\n"
    for n, t, d in params:
        out += f"    - Name: {n}\n      Type: {t}\n      Default: {d}\n"
    os.makedirs(os.path.join(DIR, "Animations"), exist_ok=True)
    open(os.path.join(DIR, "Animations", "Mannequin.cqcont"), "w").write(out)

    # Birinci şahıs kolları: hep nişan pozunda (silah görüş alanında); ateş / şarjör montajları üstte
    fp = ("AnimationController:\n  Version: 2\n  Layers:\n    - Name: Base Layer\n      Weight: 1\n      DefaultState: Aim\n      States:\n"
          + st("Aim", "pistol_aim", True, 260, 170) + "      Transitions: []\n  Parameters: []\n")
    open(os.path.join(DIR, "Animations", "FPArms.cqcont"), "w").write(fp)

    montages = [("PistolShoot", "pistol_shoot", "spine_01", 1.4, 0.05, 0.15), ("PistolReload", "pistol_reload", "spine_01", 1.0, 0.15, 0.25),
                ("HitReact", "hit", "spine_01", 1.2, 0.05, 0.2), ("Punch", "punch_jab", "spine_01", 1.3, 0.08, 0.2),
                ("Interact", "interact", "", 1.0, 0.2, 0.25), ("Throw", "throw", "spine_01", 1.1, 0.12, 0.2)]
    for name, clip, bone, rate, bi, bo in montages:
        open(os.path.join(DIR, "Animations", name + ".cqmontage"), "w").write(
            f"AnimMontage:\n  Name: {name}\n  ClipName: {clip}\n  ClipIndex: {idx[clip]}\n  Rate: {rate}\n  BlendIn: {bi}\n"
            f"  BlendOut: {bo}\n  FromBone: \"{bone}\"\n  Sections: []\n  Notifies: []\n")
    # Tek klipli döngüler (sahne figüranları: oturma, konuşma, dans ...)
    loops = os.path.join(DIR, "Animations", "Loops")
    os.makedirs(loops, exist_ok=True)
    for clip in clips:
        open(os.path.join(loops, f"{clip}.cqcont"), "w").write(
            "AnimationController:\n  Version: 2\n  Layers:\n    - Name: Base Layer\n      Weight: 1\n      DefaultState: " + clip +
            "\n      States:\n        - Name: " + clip + "\n          ClipIndex: " + str(idx[clip]) + "\n          ClipName: " + clip +
            "\n  Parameters: []\n")
    print("controller + %d montages + %d loops" % (len(montages), len(clips)))


if __name__ == "__main__":
    main()

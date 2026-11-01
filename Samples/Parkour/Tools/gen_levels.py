#!/usr/bin/env python3
"""Generates the themed Parkour levels and the level-select menu.

usage: gen_levels.py            -> writes Tools/Levels/<Scene>.cqs and Tools/Levels/build_levels.txt

Each level builder is BuildScene.cqs with the lighting, course and scenery sections replaced:
the helper functions, player, cameras and UI stay identical, so every level plays and looks like the
original Skyline Run. Courses are assembled from pieces whose geometry is copied from the original course
(wall-runs, moving platforms, the hurdle/slide/jump-pad runway, the double-jump gap, the finish), joined by
jump platforms. Each piece also emits its autopilot route and its tutorial hints, so every level can be
completed by the autopilot (the automated tests do exactly that).

Then, in the editor (or headless):
    CQ_AUTOMATION=Tools/Levels/build_levels.txt ConquerorEngine Samples/Parkour
"""
import json
import os
import random

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "Tools")
OUT = os.path.join(TOOLS, "Levels")
TIMES = os.path.join(TOOLS, "level_times.json")   # autopilot finish times measured by the tests
SEP = "// ------------------------------------------------------------------ "


def f(v):
    s = f"{v:.2f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def rgb(c):
    return "{r: %s, g: %s, b: %s}" % (f(c[0]), f(c[1]), f(c[2]))


def rgba(c):
    return "{r: %s, g: %s, b: %s, a: 1}" % (f(c[0]), f(c[1]), f(c[2]))


# ----------------------------------------------------------------------------- themes

THEMES = {
    "Skyline": dict(
        title="SKYLINE", sun=(1.0, 0.70, 0.46), sun_int=3.4, sun_rot=(-15, 235), sky="Procedural", sky_int=1.0,
        fog=(0.70, 0.52, 0.55), fog_d=0.006, exposure=0.2, temp=0.14, sat=1.12, contrast=1.06,
        accents=[(0.2, 0.85, 1.0), (1.0, 0.5, 0.15), (1.0, 0.25, 0.75), (0.45, 1.0, 0.3)],
        plat=None, scenery="towers"),
    "Neon": dict(
        title="NEON NIGHT", sun=(0.55, 0.62, 1.0), sun_int=0.9, sun_rot=(-40, 200), sky="SolidColor", sky_int=0.6,
        background=(0.015, 0.012, 0.04), fog=(0.10, 0.04, 0.16), fog_d=0.010, exposure=0.55, temp=-0.05, sat=1.25,
        contrast=1.1, accents=[(1.0, 0.2, 0.8), (0.2, 0.9, 1.0), (0.6, 0.3, 1.0), (1.0, 0.85, 0.2)],
        plat=dict(color=(0.85, 0.88, 1.0), tex="panel.png", metal=0.35, rough=0.35, wall=(0.55, 0.55, 0.7), walltex="metal.png",
                  leg=(0.12, 0.12, 0.18), legtex="metal.png"),
        scenery="neon"),
    "Desert": dict(
        title="DESERT RUINS", sun=(1.0, 0.90, 0.74), sun_int=4.2, sun_rot=(-48, 215), sky="Procedural", sky_int=1.1,
        fog=(0.88, 0.74, 0.58), fog_d=0.0045, exposure=0.05, temp=0.22, sat=1.08, contrast=1.05,
        accents=[(1.0, 0.55, 0.15), (0.25, 0.8, 1.0), (1.0, 0.3, 0.3), (1.0, 0.85, 0.3)],
        plat=dict(color=(1.0, 0.97, 0.92), tex="sand.png", metal=0.0, rough=0.9, wall=(0.95, 0.85, 0.72), walltex="sand.png",
                  leg=(0.72, 0.55, 0.40), legtex="sand.png"),
        scenery="mesas"),
    "Arctic": dict(
        title="ARCTIC PEAKS", sun=(0.86, 0.92, 1.0), sun_int=3.0, sun_rot=(-24, 160), sky="Procedural", sky_int=1.2,
        fog=(0.80, 0.87, 0.96), fog_d=0.0085, exposure=0.0, temp=-0.22, sat=0.95, contrast=1.04,
        accents=[(0.3, 0.8, 1.0), (1.0, 0.45, 0.2), (0.7, 0.4, 1.0), (0.4, 1.0, 0.8)],
        plat=dict(color=(0.92, 0.97, 1.0), tex="ice.png", metal=0.0, rough=0.22, wall=(0.80, 0.90, 1.0), walltex="ice.png",
                  leg=(0.55, 0.66, 0.80), legtex="ice.png"),
        scenery="ice", snow=True),
    "Industrial": dict(
        title="FOUNDRY", sun=(1.0, 0.60, 0.34), sun_int=2.8, sun_rot=(-12, 250), sky="Procedural", sky_int=0.8,
        fog=(0.46, 0.34, 0.26), fog_d=0.011, exposure=0.35, temp=0.2, sat=1.0, contrast=1.1,
        accents=[(1.0, 0.55, 0.1), (1.0, 0.85, 0.2), (0.3, 0.8, 1.0), (1.0, 0.25, 0.2)],
        plat=dict(color=(1.0, 1.0, 1.0), tex="rust.png", metal=0.55, rough=0.55, wall=(0.85, 0.85, 0.9), walltex="rust.png",
                  leg=(0.25, 0.22, 0.20), legtex="metal.png"),
        scenery="factory"),
    "Jungle": dict(
        title="JUNGLE TEMPLE", sun=(1.0, 0.95, 0.80), sun_int=3.0, sun_rot=(-55, 140), sky="Procedural", sky_int=1.0,
        fog=(0.48, 0.62, 0.45), fog_d=0.011, exposure=0.25, temp=0.05, sat=1.15, contrast=1.06,
        accents=[(0.4, 1.0, 0.35), (1.0, 0.75, 0.2), (0.2, 0.9, 1.0), (1.0, 0.4, 0.6)],
        plat=dict(color=(1.0, 1.0, 1.0), tex="moss.png", metal=0.0, rough=0.9, wall=(0.85, 0.85, 0.8), walltex="moss.png",
                  leg=(0.36, 0.26, 0.17), legtex="concrete.png"),
        scenery="trees"),
}

GOLD = (1.0, 0.78, 0.25)

# Level designs (Skyline 4 is the original Main.cqscene). Tokens:
#   h hop, hu hop up, hd hop down, hx hop with a long sideways step, d double-jump gap, b narrow beam,
#   wA+/wA- first wall-run (wall right / left), wB+/wB- second wall-run, m moving platforms (once per level),
#   r runway (hurdles + slide tunnel + jump pad), rh hurdles only, rp jump pad only, c checkpoint
LEVELS = {
    "Skyline": [
        ("FIRST STEPS", "h h hu c h hx h c d h"),
        ("ROOFTOP DASH", "h h c wA+ h c hu d h"),
        ("CRANE HOPS", "h hx c m h c wB- h d"),
        None,
    ],
    "Neon": [
        ("NEON ALLEY", "h h c wA+ h wA- c h d"),
        ("LIGHT TRAILS", "h hu c wB- h wB+ c h d h"),
        ("SIGNAL BOOST", "h c r c wB- h hu c m h"),
        ("OVERDRIVE", "h hx c wA+ h wB- c m h c r c wB+ h d"),
    ],
    "Desert": [
        ("DUNE HOPPER", "h h hd c h hx h c d"),
        ("MESA CROSSING", "h d c h hu d c hd h d"),
        ("SANDSTORM", "h c wA- h d c rh h hx"),
        ("SUN TEMPLE", "h hx d h c wB+ h c rp h d c hu h wA- h d"),
    ],
    "Arctic": [
        ("FROST LINE", "h b c h hu c d h"),
        ("DRIFTING FLOES", "h c m h c b h d"),
        ("ICE BRIDGES", "h b c h wA+ h b c hd d"),
        ("GLACIER SUMMIT", "h b c m h c wB- h b c r c d hu h"),
    ],
    "Industrial": [
        ("FIRST SHIFT", "h rh c h hu c d"),
        ("CONVEYOR LINE", "h c r c h m h"),
        ("SMELTER", "h c rp h wA+ h c rh h d"),
        ("BLAST FURNACE", "h c r c wB+ h m h c wA- h c rh h d"),
    ],
    "Jungle": [
        ("CANOPY TRAIL", "h hx c b h d c h"),
        ("VINE WALLS", "h c wA- h wA+ c hu d h"),
        ("RIVER CROSSING", "h c m h c rp h b c d"),
        ("LOST TEMPLE", "h hx c wA+ h b c m h c r c wB- h c d hu h"),
    ],
}

CATEGORIES = list(LEVELS.keys())


def scene_name(cat, i):
    return "Main" if (cat == "Skyline" and i == 3) else f"{cat}{i + 1}"


def level_id(cat, i):
    return f"{cat.lower()}-{i + 1}"


# ----------------------------------------------------------------------------- course builder

class Course:
    def __init__(self, theme, seed):
        self.t = theme
        self.rnd = random.Random(seed)
        self.out = []
        self.route = []
        self.hints = []
        self.n = 0
        self.cp = 0
        self.pads = 0
        self.hurdles = 0
        self.walls = 0
        self.movers = False
        self.xs = [0.0]
        self.cur = None

    def acc(self, i):
        return "ACC%d" % (i % 4)

    def emit(self, line):
        self.out.append(line)

    def step(self, *items):
        self.route.append(",".join(str(x) if isinstance(x, str) else f(x) for x in items))

    def hint(self, z0, z1, text, wall=""):
        self.hints.append(f"{f(z0)},{f(z1)},{text}" + (f",{wall}" if wall else ""))

    def platform(self, x, top, z, sx, sz, edge, pillars=True, name=None):
        if name is None:
            self.n += 1
            name = f"P{self.n}"
        self.emit(f'Platform("{name}", {f(x)}, {f(top)}, {f(z)}, {f(sx)}, {f(sz)}, {edge}, {"true" if pillars else "false"});')
        self.xs.append(x)
        return dict(name=name, x=x, top=top, z=z, sx=sx, sz=sz)

    @staticmethod
    def north(p):
        return p["z"] - p["sz"] / 2.0

    # ---- start
    def start(self):
        self.cur = self.platform(0, 0, 0, 14, 14, self.acc(0), name="Start")
        self.hint(9, -6, "SPACE TO JUMP   /   HOLD SHIFT TO SPRINT")

    # ---- jump to a new platform
    def hop(self, kind, size):
        p = self.cur
        r = self.rnd
        sx, sz = size if size else (r.choice([6, 7, 8]), r.choice([6, 7, 8]))
        dy = {"h": r.uniform(-0.2, 0.5), "hu": r.uniform(0.7, 0.9), "hd": r.uniform(-2.0, -1.4), "hx": r.uniform(-0.2, 0.3)}[kind]
        gap = {"h": r.uniform(4.6, 5.6), "hu": r.uniform(4.2, 4.8), "hd": r.uniform(5.5, 6.5), "hx": r.uniform(4.4, 5.0)}[kind]
        lateral = r.uniform(3.0, 4.0) if kind == "hx" else r.uniform(0.0, 2.2)
        # Sola / sağa: parkur ±6 m koridorda kalır
        side = -1 if p["x"] > 1.5 else (1 if p["x"] < -1.5 else r.choice([-1, 1]))
        x = p["x"] + side * lateral
        top = p["top"] + dy
        z = self.north(p) - gap - sz / 2.0
        aim = p["x"] + max(-(p["sx"] / 2 - 0.9), min(p["sx"] / 2 - 0.9, 0.5 * (x - p["x"])))
        self.step("go", p["x"], min(p["z"] + 1, p["z"] + p["sz"] / 2 - 1.5))
        self.step("jump", aim, self.north(p) + 0.6)
        self.cur = self.platform(x, top, z, sx, sz, self.acc(0))

    # ---- narrow beam bridge
    def beam(self):
        p = self.cur
        length = self.rnd.uniform(12, 16)
        n = self.north(p)
        b = f"beam{self.n}"
        self.emit(f'var {b} = Solid(Mesh("Beam{self.n}", "Cube", {f(p["x"])}, {f(p["top"] - 0.3)}, {f(n - length / 2)}, 2.0, 0.6, {f(length)}, platColor));')
        self.emit(f'Surface({b}, platTex, 1, {f(length / 3)}, platMetal, platRough);')
        self.emit(f'Strip({b}, "Beam{self.n} EdgeL", {f(p["x"] - 0.96)}, {f(p["top"] + 0.02)}, {f(n - length / 2)}, 0.08, 0.06, {f(length)}, ACC2, 6);')
        self.emit(f'Strip({b}, "Beam{self.n} EdgeR", {f(p["x"] + 0.96)}, {f(p["top"] + 0.02)}, {f(n - length / 2)}, 0.08, 0.06, {f(length)}, ACC2, 6);')
        self.step("go", p["x"], n + 0.8)
        self.step("go", p["x"], n - length - 3.5)
        self.hint(n + 4, n - 2, "STAY ON THE BEAM")
        self.cur = self.platform(p["x"], p["top"], n - length - 4, 8, 8, self.acc(2))

    # ---- double jump gap (F1 -> F2 of the original course)
    def double(self):
        p = self.cur
        x, z, top = p["x"], p["z"], p["top"]
        n = self.north(p)
        self.step("go", x, n + 0.6)
        self.step("jump", x, n + 0.6)
        self.step("go", x, n - 3.5)
        self.step("jump", x, n - 3.5)
        sz = 6
        self.hint(n + 5, n - 4, "DOUBLE JUMP: PRESS SPACE AGAIN IN THE AIR")
        self.cur = self.platform(x, top + 1, n - 8 - sz / 2.0, 6, sz, self.acc(0))

    # ---- wall-run A (A4 -> WallRunA -> B1); entry 10 x 8, side +1 = wall on the right
    def wall_a(self, s):
        p = self.cur
        x, z, top = p["x"], p["z"], p["top"]
        self.walls += 1
        self.emit(f'Wall("WallRun{self.walls}", {f(x + 6 * s)}, {f(top - 2)}, {f(z - 11.5)}, 1.0, 9, 17, ACC1);')
        if not self.route[-1].startswith("cp"):
            self.step("go", *self.approach("wA+" if s > 0 else "wA-"))
        self.step("go", x + 4.6 * s, z - 1.5)
        self.step("jump", x + 4.6 * s, z - 3.5)
        self.step("go", x + 4.9 * s, z - 13.5)
        self.step("jump", x + 4.9 * s, z - 13.5)
        self.step("go", x + 3 * s, z - 22)
        side = "RIGHT" if s > 0 else "LEFT"
        self.hint(z + 3, z - 4.5, f"JUMP AT THE WALL ON THE {side} AND HOLD W TO WALL-RUN")
        self.hint(z - 4.5, z - 19, "", "PRESS SPACE TO JUMP OFF THE WALL")
        self.cur = self.platform(x + 3 * s, top, z - 23, 8, 8, self.acc(1))

    # ---- wall-run B (E2 -> WallRunB -> F1); entry 8 x 8, side -1 = wall on the left
    def wall_b(self, s):
        p = self.cur
        x, z, top = p["x"], p["z"], p["top"]
        self.walls += 1
        self.emit(f'Wall("WallRun{self.walls}", {f(x + 5 * s)}, {f(top - 2)}, {f(z - 12)}, 1.0, 9, 14, ACC1);')
        self.step("go", x + 3.3 * s, z - 2.5)
        self.step("go", x + 3.4 * s, z - 3.4)
        self.step("jump", x + 3.4 * s, z - 3.4)
        self.step("go", x + 3.9 * s, z - 12.5)
        self.step("jump", x + 3.9 * s, z - 12.5)
        self.step("go", x + 1.5 * s, z - 22)
        side = "RIGHT" if s > 0 else "LEFT"
        self.hint(z + 3, z - 4.5, f"WALL-RUN ON THE {side}, THEN JUMP")
        self.hint(z - 4.5, z - 19, "", "PRESS SPACE TO JUMP OFF THE WALL")
        self.cur = self.platform(x + 2 * s, top + 0.5, z - 24, 8, 8, self.acc(1))

    # ---- moving platforms (B1 -> Mover1..3 -> C1); entry 8 x 8
    def movers_piece(self):
        assert not self.movers, "one mover section per level"
        self.movers = True
        p = self.cur
        x, z, top = p["x"], p["z"], p["top"]
        dy = top - 2.0
        for i, (zz, yy) in enumerate(((-11, 0.0), (-20, 0.7), (-29, 1.4))):
            self.emit(f'var mv{i + 1} = Platform("Mover{i + 1}", {f(x)}, {f(top + yy)}, {f(z + zz)}, 4, 4, ACC2, false);')
        self.emit("foreach (var mv in [[mv1, \"x\", 3.5, 5.0, 0.0], [mv2, \"x\", 3.5, 4.4, 1.1], [mv3, \"y\", 1.4, 3.6, 0.4]])")
        self.emit("{")
        self.emit('    EntitySetComponentField(mv[0], "Rigidbody", "BodyType", "Kinematic");')
        self.emit('    AddScript(mv[0], "Assets/Scripts/MovingPlatform.cqs");')
        self.emit('    SetScriptField(mv[0], "axis", mv[1]);')
        self.emit('    SetScriptField(mv[0], "amplitude", mv[2]);')
        self.emit('    SetScriptField(mv[0], "period", mv[3]);')
        self.emit('    SetScriptField(mv[0], "phase", mv[4]);')
        self.emit("}")
        c = f"@{f(x)}@{f(dy)}"
        self.step("stop", x, z - 1.6)
        self.step("wait", "m1" + c)
        self.step("leap", "Mover1", z - 3.6, z - 11)
        self.step("wait", "m2" + c)
        self.step("leap", "Mover2", z - 12.6, z - 20)
        self.step("wait", "m3low" + c)
        self.step("leap", "Mover3", z - 21.6, z - 29)
        self.step("wait", "m3high" + c)
        self.step("go", x, z - 30.6)
        self.step("jump", x, z - 30.6)
        self.step("go", x, z - 33.5)
        self.step("jump", x, z - 33.5)
        self.step("go", x, z - 37)
        self.hint(z + 3, z - 6, "WAIT FOR THE PLATFORM, THEN JUMP")
        self.cur = self.platform(x, top + 2, z - 38.5, 8, 7, self.acc(2))

    # ---- runway (C1 -> hurdles, slide tunnel -> E1 jump pad -> E2)
    def runway(self, hurdles, tunnel, pad):
        p = self.cur
        x, top = p["x"], p["top"]
        e = self.north(p)
        hz = []
        length = 33 if (hurdles and tunnel) else 20
        self.emit(f'Platform("Runway{self.n}", {f(x)}, {f(top)}, {f(e - length / 2)}, 6, {f(length)}, ACC3, true);')
        if hurdles:
            for dz in (-7, -13):
                self.hurdles += 1
                self.emit(f'Hurdle("Hurdle{self.hurdles}", {f(x)}, {f(top)}, {f(e + dz)});')
                hz.append(e + dz)
        tz = None
        if tunnel:
            tz = e - 27 if hurdles else e - 12
            self.emit(f'Tunnel("Tunnel{self.n}", {f(x)}, {f(top)}, {f(tz)});')
        end = e - length
        self.step("go", x, e + 0.5)
        for k, h in enumerate(hz):
            self.step("go", x, h + (3.6 if k == 0 else 1.6))
            self.step("jump", x, h + (3.6 if k == 0 else 1.6))
        if tunnel:
            self.step("go", x, tz + 6.6)
            self.step("slide", x, tz + 6.6)
        txt = "JUMP THE HURDLES" if hurdles else ""
        if tunnel:
            txt = (txt + "   /   " if txt else "") + "CTRL TO SLIDE UNDER THE TUNNEL"
        if txt:
            self.hint(e + 4, e - 4, txt)
        if pad:
            self.pads += 1
            e1 = self.platform(x, top, end - 4, 8, 8, self.acc(3))
            self.emit(f'JumpPad("JumpPad{self.pads}", {f(x)}, {f(top)}, {f(end - 4)});')
            self.step("go", x, end)
            self.step("walk", x, end - 4)
            self.step("walk", x, end - 13)
            self.hint(end + 5, end - 8, "STEP ON THE JUMP PAD")
            self.cur = self.platform(x, top + 6, end - 13, 8, 8, self.acc(3))
        else:
            self.step("go", x, end - 3)
            self.cur = self.platform(x, top, end - 4, 8, 8, self.acc(3))

    # Koşulacak ilk nokta (kontrol noktasından önce): ilk duvar koşusu platformun yan tarafından başlar
    def approach(self, nxt):
        p = self.cur
        if nxt in ("wA+", "wA-"):
            return p["x"] + (3.5 if nxt == "wA+" else -3.5), p["z"] + 2
        return p["x"], p["z"] + (1 if p["sz"] >= 8 else 0.5) + 0.5

    # ---- checkpoint on the current platform
    def checkpoint(self, nxt):
        p = self.cur
        self.cp += 1
        gz = p["z"] + (1 if p["sz"] >= 8 else 0.5)
        self.emit(f'Gate("Checkpoint{self.cp}", {f(p["x"])}, {f(p["top"])}, {f(gz)}, {f(p["sx"] - 2)}, {self.acc(self.cp)});')
        self.step("go", *self.approach(nxt))
        self.step("cp", self.cp)

    # ---- finish (F2 -> Finish)
    def finish(self):
        p = self.cur
        x, top = p["x"], p["top"]
        n = self.north(p)
        self.step("go", x, n + 0.6)
        self.step("jump", x, n + 0.6)
        self.step("go", x, n - 3)
        self.step("jump", x, n - 3)
        self.step("go", x, n - 14)
        self.platform(x, top + 0.5, n - 11, 12, 10, "GOLD", name="Finish")
        self.emit(f'Gate("FinishGate", {f(x)}, {f(top + 0.5)}, {f(n - 9)}, 8, GOLD);')
        self.emit(f'Confetti({f(x)}, {f(top + 4.7)}, {f(n - 9)});')
        return -(n - 16)


# Entry size each fixed piece needs (the preceding jump platform is made this size)
ENTRY = {"wA+": (10, 8), "wA-": (10, 8), "wB+": (8, 8), "wB-": (8, 8), "m": (8, 8), "d": (8, 8), "b": (8, 8)}
FIXED = set(ENTRY) | {"r", "rh", "rp"}


def build_course(theme, design, seed):
    c = Course(theme, seed)
    c.start()
    toks = design.split()
    for i, t in enumerate(toks):
        nxt = None
        for u in toks[i + 1:]:
            if u != "c":
                nxt = u
                break
        if t in ("h", "hu", "hd", "hx"):
            size = ENTRY.get(nxt) if nxt else (6, 6)
            if size is None and i + 1 < len(toks) and toks[i + 1] == "c":
                size = (8, 8)   # kontrol noktası kapısı için geniş platform
            c.hop(t, size)
        elif t == "c":
            c.checkpoint(nxt)
        elif t == "d":
            c.double()
        elif t == "b":
            c.beam()
        elif t in ("wA+", "wA-"):
            c.wall_a(1 if t.endswith("+") else -1)
        elif t in ("wB+", "wB-"):
            c.wall_b(1 if t.endswith("+") else -1)
        elif t == "m":
            c.movers_piece()
        elif t == "r":
            c.runway(True, True, True)
        elif t == "rh":
            c.runway(True, False, False)
        elif t == "rp":
            c.runway(False, False, True)
        else:
            raise ValueError(t)
        # Sabit parçanın çıkış platformu sonrakinin girişine uymalı (uymuyorsa araya zıplama gerekir)
        if t in FIXED and nxt in ENTRY and (c.cur["sx"], c.cur["sz"]) != ENTRY[nxt]:
            raise ValueError(f"{design}: '{t}' followed by '{nxt}' needs a hop between")
    length = c.finish()
    return c, length


# ----------------------------------------------------------------------------- CQS sections

def lighting(t):
    L = [SEP + "Işık ve atmosfer (tema: %s)" % t["title"],
         'var sun = CreateEntity("Sun");',
         'EntityAddComponent(sun, "DirectionalLight");',
         f'EntitySetComponentField(sun, "DirectionalLight", "Color", {rgb(t["sun"])});',
         f'EntitySetComponentField(sun, "DirectionalLight", "Intensity", {f(t["sun_int"])});',
         'EntitySetComponentField(sun, "DirectionalLight", "CastShadows", true);',
         f'EntitySetRotation(sun, {f(t["sun_rot"][0])}, {f(t["sun_rot"][1])}, 0);',
         f'RenderSettingSet("Environment.Sky", "{t["sky"]}");',
         f'RenderSettingSet("Environment.SkyboxIntensity", {f(t["sky_int"])});']
    if "background" in t:
        L.append(f'RenderSettingSet("Environment.BackgroundColor", {rgb(t["background"])});')
    L += ['RenderSettingSet("Fog.Enabled", true);',
          'RenderSettingSet("Fog.Mode", "Exponential");',
          f'RenderSettingSet("Fog.Color", {rgb(t["fog"])});',
          f'RenderSettingSet("Fog.Density", {t["fog_d"]});',
          'RenderSettingSet("Fog.HeightFalloff", 0.035);',
          'RenderSettingSet("Fog.BaseHeight", -12);',
          'RenderSettingSet("Shadows.Enabled", true);',
          'RenderSettingSet("Shadows.Quality", "Soft");',
          'RenderSettingSet("Shadows.Distance", 120);',
          'RenderSettingSet("Shadows.CascadeCount", 3);',
          'RenderSettingSet("Post.Tonemap", "ACES");',
          f'RenderSettingSet("Post.Exposure", {f(t["exposure"])});',
          'RenderSettingSet("Post.Bloom", true);',
          'RenderSettingSet("Post.BloomThreshold", 1.0);',
          'RenderSettingSet("Post.BloomIntensity", 0.6);',
          'RenderSettingSet("Post.AmbientOcclusion", true);',
          'RenderSettingSet("Post.AORadius", 0.7);',
          'RenderSettingSet("Post.Vignette", true);',
          'RenderSettingSet("Post.VignetteIntensity", 0.24);',
          'RenderSettingSet("Post.ColorGrading", true);',
          f'RenderSettingSet("Post.Contrast", {f(t["contrast"])});',
          f'RenderSettingSet("Post.Saturation", {f(t["sat"])});',
          f'RenderSettingSet("Post.Temperature", {f(t["temp"])});',
          'RenderSettingSet("Post.AntiAliasing", "SMAA");', ""]
    return "\n".join(L)


def materials(t):
    L = []
    for i, a in enumerate(t["accents"]):
        L.append(f"var ACC{i} = {rgb(a)};")
    L.append(f"var GOLD = {rgb(GOLD)};")
    m = t["plat"]
    if m:
        L += [f"platColor = {rgba(m['color'])};",
              f'platTex = "Assets/Textures/{m["tex"]}";',
              f"platMetal = {f(m['metal'])};",
              f"platRough = {f(m['rough'])};",
              f"wallColor = {rgba(m['wall'])};",
              f'wallTex = "Assets/Textures/{m["walltex"]}";',
              f"legColor = {rgba(m['leg'])};",
              f'legTex = "Assets/Textures/{m["legtex"]}";']
    return "\n".join(L) + "\n"


PIECE_HELPERS = r'''
// Engel: koyu metal gövde, üstte neon şerit, önde amber uyarı bandı
func Hurdle(name, x, top, z)
{
    var hb = Solid(Mesh(name, "Cube", x, top + 0.45, z, 6, 0.9, 0.4, {r: 0.62, g: 0.64, b: 0.72, a: 1}));
    Surface(hb, "Assets/Textures/metal.png", 3, 0.5, 0.6, 0.45);
    Strip(hb, name + " Top", x, top + 0.92, z, 6.04, 0.05, 0.44, ACC3, 3.5);
    Strip(hb, name + " Band", x, top + 0.5, z + 0.21, 6.0, 0.08, 0.02, {r: 1.0, g: 0.7, b: 0.15}, 2.2);
    return hb;
}

// Kayma tüneli (z = tünelin ortası, 8 m uzunluk)
func Tunnel(name, x, top, z)
{
    var tn = Solid(Mesh(name, "Cube", x, top + 2.3, z, 6.8, 2.0, 8, {r: 0.52, g: 0.54, b: 0.60, a: 1}));
    Surface(tn, "Assets/Textures/metal.png", 3, 4, 0.6, 0.45);
    Strip(tn, name + " Sign", x, top + 1.36, z + 4.03, 6.8, 0.1, 0.06, {r: 1.0, g: 0.85, b: 0.2}, 8);
    Solid(Mesh(name + " WallL", "Cube", x - 3.2, top + 1.65, z, 0.4, 3.3, 8, {r: 0.42, g: 0.44, b: 0.50, a: 1}));
    Solid(Mesh(name + " WallR", "Cube", x + 3.2, top + 1.65, z, 0.4, 3.3, 8, {r: 0.42, g: 0.44, b: 0.50, a: 1}));
    return tn;
}

func JumpPad(name, x, top, z)
{
    var pad = Solid(Mesh(name, "Cylinder", x, top + 0.06, z, 2.6, 0.12, 2.6, {r: 0.25, g: 0.1, b: 0.25, a: 1}));
    Glow(pad, {r: 1.0, g: 0.25, b: 0.85}, 5);
    var fx = CreateEntity(name + " FX");
    EntitySetPosition(fx, x, top + 0.15, z);
    EntityAddComponent(fx, "ParticleSystem");
    var ps = {
        PlayOnAwake: true, Looping: true, Prewarm: true, RateOverTime: 26, Duration: 3,
        StartLifetime: {x: 0.5, y: 1.1}, StartSpeed: {x: 4, y: 7}, StartSize: {x: 0.08, y: 0.22},
        StartColorA: {r: 1, g: 0.3, b: 0.85, a: 1}, StartColorB: {r: 1, g: 0.8, b: 1, a: 1},
        Shape: "Cone", Radius: 1.1, Angle: 4, GravityModifier: 0, SimulationSpace: "World", RandomizeDirection: 0.03,
        Blend: "Additive", Texture: "Assets/Textures/spark.png", EmissiveIntensity: 3, MaxParticles: 400,
        ColorOverLifetimeEnabled: true, SizeOverLifetimeEnabled: true
    };
    foreach (var k, v in ps) { EntitySetComponentField(fx, "ParticleSystem", k, v); }
    EntitySetParent(fx, level);
    return pad;
}

func Confetti(x, y, z)
{
    var confetti = CreateEntity("FinishConfetti");
    EntitySetPosition(confetti, x, y, z);
    EntityAddComponent(confetti, "ParticleSystem");
    var cps = {
        PlayOnAwake: false, Looping: false, EmissionEnabled: false, RateOverTime: 0,
        StartLifetime: {x: 1.6, y: 3.2}, StartSpeed: {x: 5, y: 12}, StartSize: {x: 0.12, y: 0.3},
        StartColorA: {r: 1, g: 0.8, b: 0.2, a: 1}, StartColorB: {r: 0.3, g: 0.9, b: 1, a: 1},
        Shape: "Sphere", Radius: 0.5, GravityModifier: 0.45, SimulationSpace: "World", Drag: 0.8,
        Blend: "Additive", Texture: "Assets/Textures/spark.png", EmissiveIntensity: 2.5, MaxParticles: 1500,
        ColorOverLifetimeEnabled: true
    };
    foreach (var k, v in cps) { EntitySetComponentField(confetti, "ParticleSystem", k, v); }
    EntitySetParent(confetti, level);
}

// Toz: oyuncu sert inişte, kayarken ve duvarda koşarken yayar (Player.cqs ParticleEmitAt)
func Dust()
{
    var dust = CreateEntity("FX_Dust");
    EntityAddComponent(dust, "ParticleSystem");
    var dps = {
        PlayOnAwake: false, Looping: false, EmissionEnabled: false, RateOverTime: 0,
        StartLifetime: {x: 0.35, y: 0.75}, StartSpeed: {x: 0.6, y: 2.4}, StartSize: {x: 0.18, y: 0.42},
        StartColorA: {r: 0.92, g: 0.86, b: 0.80, a: 0.55}, StartColorB: {r: 1.0, g: 0.95, b: 0.9, a: 0.4},
        Shape: "Circle", Radius: 0.3, GravityModifier: -0.04, Drag: 3.0, SimulationSpace: "World",
        Blend: "Alpha", Texture: "Assets/Textures/glow.png", MaxParticles: 600,
        ColorOverLifetimeEnabled: true, ColorOverLifetimeKeys: "0:1,1,1,1; 0.6:1,1,1,0.6; 1:1,1,1,0",
        SizeOverLifetimeEnabled: true, SizeOverLifetimeKeys: "0:0.6; 1:1.8"
    };
    foreach (var k, v in dps) { EntitySetComponentField(dust, "ParticleSystem", k, v); }
    EntitySetParent(dust, level);
}
'''


def scenery(t, length, xmin, xmax):
    """Distant set dressing outside a clear corridor around the course."""
    kind = t["scenery"]
    mid = -length / 2.0
    zfar = -(length + 50)
    L = [SEP + "Çevre",
         f"var corridorL = {f(xmin - 24)};",
         f"var corridorR = {f(xmax + 24)};",
         "// Parkurun çevresinde boş bir koridor kalır (oyuncu görüşü ve başlık kamerası)",
         "func Place(w)",
         "{",
         "    var ang = Rand() * 6.2831;",
         f"    var dist = 70 + Rand() * {f(max(170, length * 0.75))};",
         "    var cx = MathCos(ang) * dist * 0.8;",
         f"    var cz = {f(mid)} + MathSin(ang) * dist;",
         f"    if (cz < 40 && cz > {f(zfar)} && cx + w / 2.0 > corridorL && cx - w / 2.0 < corridorR)",
         "    {",
         "        if (cx < (corridorL + corridorR) / 2.0) { cx = corridorL - w / 2.0 - Rand() * 25; }",
         "        else { cx = corridorR + w / 2.0 + Rand() * 25; }",
         "    }",
         "    return {x: cx, z: cz};",
         "}"]
    count = int(48 * max(1.0, length / 220.0))
    if kind in ("towers", "neon"):
        emis = 2.2 if kind == "towers" else 3.2
        L += [f"for (var i = 0; i < {count}; i++)", "{",
              "    var w = 10 + Rand() * 18;",
              "    var d = 10 + Rand() * 18;",
              "    var c = Place(w);",
              "    var hgt = 40 + Rand() * 110;",
              "    var b = Mesh(\"Tower\" + i, \"Cube\", c.x, -40 + hgt / 2.0, c.z, w, hgt, d, {r: 0.92, g: 0.94, b: 1.0, a: 1});",
              "    Surface(b, \"Assets/Textures/windows.png\", MathMax(w, d) / 12.0, hgt / 24.0, 0.4, 0.3);",
              "    EntitySetComponentField(b, \"MeshRenderer\", \"EmissionColor\", {r: 1, g: 1, b: 1});",
              f"    EntitySetComponentField(b, \"MeshRenderer\", \"EmissionStrength\", {emis});",
              "    EntitySetComponentField(b, \"MeshRenderer\", \"EmissionFromTexture\", true);",
              "    EntitySetComponentField(b, \"MeshRenderer\", \"CastShadows\", false);"]
        if kind == "towers":
            L += ["    if (Rand() < 0.5)", "    {",
                  "        Glow(Mesh(\"Tower\" + i + \" Beacon\", \"Sphere\", c.x, -40 + hgt + 1.0, c.z, 1.2, 1.2, 1.2, {r: 0.2, g: 0.02, b: 0.02, a: 1}), {r: 1.0, g: 0.1, b: 0.1}, 9);",
                  "    }"]
        else:
            # Neon: köşelerde dikey ışık şeritleri ve çatı tabelaları
            L += ["    var nc = [ACC0, ACC1, ACC2][int(Rand() * 2.99)];",
                  "    Glow(Mesh(\"Tower\" + i + \" NeonL\", \"Cube\", c.x - w / 2.0, -40 + hgt / 2.0, c.z + d / 2.0, 0.4, hgt, 0.4, dark), nc, 6);",
                  "    Glow(Mesh(\"Tower\" + i + \" NeonR\", \"Cube\", c.x + w / 2.0, -40 + hgt / 2.0, c.z + d / 2.0, 0.4, hgt, 0.4, dark), nc, 6);",
                  "    if (Rand() < 0.45)", "    {",
                  "        Glow(Mesh(\"Tower\" + i + \" Sign\", \"Cube\", c.x, -40 + hgt + 2.5, c.z, w * 0.7, 3.0, 0.5, dark), [ACC0, ACC1, ACC3][int(Rand() * 2.99)], 7);",
                  "    }"]
        L.append("}")
    elif kind == "mesas":
        L += [f"for (var i = 0; i < {count}; i++)", "{",
              "    var w = 18 + Rand() * 30;",
              "    var d = 18 + Rand() * 30;",
              "    var c = Place(w);",
              "    var hgt = 25 + Rand() * 60;",
              "    var b = Mesh(\"Mesa\" + i, \"Cube\", c.x, -40 + hgt / 2.0, c.z, w, hgt, d, {r: 0.95, g: 0.78, b: 0.62, a: 1});",
              "    Surface(b, \"Assets/Textures/sand.png\", w / 8.0, hgt / 8.0, 0.0, 0.95);",
              "    EntitySetRotation(b, 0, Rand() * 90, 0);",
              "    EntitySetComponentField(b, \"MeshRenderer\", \"CastShadows\", false);",
              "    // Kademeli tepe",
              "    var cap = Mesh(\"Mesa\" + i + \" Cap\", \"Cube\", c.x, -40 + hgt + 3.0, c.z, w * 0.6, 6, d * 0.6, {r: 0.98, g: 0.82, b: 0.66, a: 1});",
              "    Surface(cap, \"Assets/Textures/sand.png\", w / 12.0, 1, 0.0, 0.95);",
              "    EntitySetComponentField(cap, \"MeshRenderer\", \"CastShadows\", false);",
              "    if (Rand() < 0.3)", "    {",
              "        var ob = Mesh(\"Obelisk\" + i, \"Cube\", c.x, -40 + hgt + 14, c.z, 2.2, 22, 2.2, {r: 0.9, g: 0.75, b: 0.55, a: 1});",
              "        Surface(ob, \"Assets/Textures/sand.png\", 1, 4, 0.0, 0.9);",
              "        Glow(Mesh(\"Obelisk\" + i + \" Tip\", \"Sphere\", c.x, -40 + hgt + 25.5, c.z, 1.4, 1.4, 1.4, dark), ACC0, 6);",
              "    }",
              "}"]
    elif kind == "ice":
        L += [f"for (var i = 0; i < {count}; i++)", "{",
              "    var w = 14 + Rand() * 26;",
              "    var c = Place(w);",
              "    var hgt = 30 + Rand() * 90;",
              "    var b = Mesh(\"Berg\" + i, \"Cube\", c.x, -40 + hgt / 2.0, c.z, w, hgt, w * (0.7 + Rand() * 0.5), {r: 0.86, g: 0.93, b: 1.0, a: 1});",
              "    Surface(b, \"Assets/Textures/ice.png\", w / 10.0, hgt / 10.0, 0.0, 0.3);",
              "    EntitySetRotation(b, (Rand() - 0.5) * 10, Rand() * 90, (Rand() - 0.5) * 10);",
              "    EntitySetComponentField(b, \"MeshRenderer\", \"CastShadows\", false);",
              "    // Karlı tepe",
              "    var cap = Mesh(\"Berg\" + i + \" Snow\", \"Sphere\", c.x, -40 + hgt, c.z, w * 0.9, w * 0.35, w * 0.9, {r: 1.0, g: 1.0, b: 1.0, a: 1});",
              "    EntitySetComponentField(cap, \"MeshRenderer\", \"Roughness\", 0.8);",
              "    EntitySetComponentField(cap, \"MeshRenderer\", \"CastShadows\", false);",
              "}"]
    elif kind == "factory":
        L += [f"for (var i = 0; i < {count}; i++)", "{",
              "    var w = 6 + Rand() * 8;",
              "    var c = Place(w * 3);",
              "    var hgt = 50 + Rand() * 90;",
              "    var b = Mesh(\"Chimney\" + i, \"Cylinder\", c.x, -40 + hgt / 2.0, c.z, w, hgt, w, {r: 0.85, g: 0.8, b: 0.78, a: 1});",
              "    Surface(b, \"Assets/Textures/rust.png\", 2, hgt / 6.0, 0.5, 0.6);",
              "    EntitySetComponentField(b, \"MeshRenderer\", \"CastShadows\", false);",
              "    Glow(Mesh(\"Chimney\" + i + \" Ring\", \"Cylinder\", c.x, -40 + hgt - 3, c.z, w * 1.08, 0.6, w * 1.08, dark), ACC0, 4);",
              "    // Yanında fabrika binası",
              "    var hall = Mesh(\"Hall\" + i, \"Cube\", c.x + w * 1.6, -40 + hgt * 0.25, c.z, w * 2.2, hgt * 0.5, w * 3, {r: 0.55, g: 0.52, b: 0.5, a: 1});",
              "    Surface(hall, \"Assets/Textures/metal.png\", 3, hgt / 12.0, 0.6, 0.5);",
              "    EntitySetComponentField(hall, \"MeshRenderer\", \"CastShadows\", false);",
              "    if (Rand() < 0.5)", "    {",
              "        var sm = CreateEntity(\"Smoke\" + i);",
              "        EntitySetPosition(sm, c.x, -40 + hgt + 0.5, c.z);",
              "        EntityAddComponent(sm, \"ParticleSystem\");",
              "        var sps = {PlayOnAwake: true, Looping: true, Prewarm: true, RateOverTime: 6, Duration: 6,",
              "            StartLifetime: {x: 6, y: 10}, StartSpeed: {x: 2, y: 4}, StartSize: {x: 4, y: 8},",
              "            StartColorA: {r: 0.35, g: 0.32, b: 0.3, a: 0.45}, StartColorB: {r: 0.5, g: 0.45, b: 0.4, a: 0.3},",
              "            Shape: \"Cone\", Radius: w * 0.3, Angle: 12, GravityModifier: -0.02, SimulationSpace: \"World\",",
              "            Blend: \"Alpha\", Texture: \"Assets/Textures/glow.png\", MaxParticles: 80,",
              "            ColorOverLifetimeEnabled: true, ColorOverLifetimeKeys: \"0:1,1,1,0; 0.15:1,1,1,1; 1:1,1,1,0\",",
              "            SizeOverLifetimeEnabled: true, SizeOverLifetimeKeys: \"0:0.5; 1:2.5\"};",
              "        foreach (var k, v in sps) { EntitySetComponentField(sm, \"ParticleSystem\", k, v); }",
              "        EntitySetParent(sm, level);",
              "    }",
              "}"]
    elif kind == "trees":
        L += [f"for (var i = 0; i < {count * 2}; i++)", "{",
              "    var w = 2 + Rand() * 3;",
              "    var c = Place(w * 6);",
              "    var hgt = 50 + Rand() * 60;",
              "    var b = Mesh(\"Tree\" + i, \"Cylinder\", c.x, -40 + hgt / 2.0, c.z, w, hgt, w, {r: 0.42, g: 0.31, b: 0.22, a: 1});",
              "    Surface(b, \"Assets/Textures/moss.png\", 1, hgt / 8.0, 0.0, 0.95);",
              "    EntitySetComponentField(b, \"MeshRenderer\", \"CastShadows\", false);",
              "    var cr = w * (4 + Rand() * 3);",
              "    var crown = Mesh(\"Tree\" + i + \" Crown\", \"Sphere\", c.x, -40 + hgt, c.z, cr * 2, cr * 1.1, cr * 2, {r: 0.16 + Rand() * 0.08, g: 0.38 + Rand() * 0.14, b: 0.12, a: 1});",
              "    EntitySetComponentField(crown, \"MeshRenderer\", \"Roughness\", 0.9);",
              "    EntitySetComponentField(crown, \"MeshRenderer\", \"CastShadows\", false);",
              "}",
              "// Ateş böcekleri",
              f"Sparkle(\"Fireflies\", 0, 0, {f(mid)}, 60, {f(length + 40)}, {{r: 0.8, g: 1.0, b: 0.4}}, 30);"]
    L.append("")
    return "\n".join(L)


def post_player(t):
    if not t.get("snow"):
        return ""
    return "\n".join([
        "// Kar: oyuncunun çevresindeki zerreler yavaşça düşer",
        'EntitySetComponentField(motes, "ParticleSystem", "StartColorA", {r: 1, g: 1, b: 1, a: 0.9});',
        'EntitySetComponentField(motes, "ParticleSystem", "StartColorB", {r: 0.85, g: 0.92, b: 1, a: 0.7});',
        'EntitySetComponentField(motes, "ParticleSystem", "StartSize", {x: 0.05, y: 0.12});',
        'EntitySetComponentField(motes, "ParticleSystem", "GravityModifier", 0.03);',
        'EntitySetComponentField(motes, "ParticleSystem", "RateOverTime", 120);',
        'EntitySetComponentField(motes, "ParticleSystem", "Blend", "Alpha");',
        'EntitySetComponentField(motes, "ParticleSystem", "Texture", "Assets/Textures/glow.png");',
        ""])


def game_fields(fields):
    L = []
    for k, v in fields.items():
        if isinstance(v, str):
            L.append(f'SetScriptField(game, "{k}", "{v}");')
        else:
            L.append(f'SetScriptField(game, "{k}", {f(v)});')
    return "\n".join(L) + "\n"


def split_sections(text):
    parts = text.split("\n" + SEP)
    head = parts[0]
    sec = {}
    for p in parts[1:]:
        title, _, body = p.partition("\n")
        sec[title.strip()] = SEP + p
    return head, sec


def medal_times(cat, i, est):
    times = {}
    if os.path.exists(TIMES):
        with open(TIMES) as fp:
            times = json.load(fp)
    # level_times.json: otopilot süreleri (testlerden); altın ≈ otopilot + %15 (özgün parkurdaki oran)
    t = times.get(level_id(cat, i), est)
    gold = int(t * 1.15 + 1.5)
    return gold, int(gold * 1.3 + 0.5), int(gold * 1.85 + 0.5)


def build_menu(head, sec, data):
    """Menu.cqscene: skyline backdrop with a short course, orbiting camera, level-select canvas."""
    t = THEMES["Skyline"]
    ui = sec["Arayüz"]
    helpers = ui[ui.index("var white"):ui.index("var hud =")]
    L = ["// Generated by Tools/gen_levels.py - level select menu. Edit the generator, not this file.\n",
         head, "\n", materials(t), PIECE_HELPERS, "\n", lighting(t), "\n", SEP + "Arka plan\n"]
    c = Course(t, 7)
    c.start()
    for tok in ("h", "hx", "c", "wA+", "h", "d"):
        nxt = {"h": "c", "hx": "c"}.get(tok)
        if tok in ("h", "hx"):
            c.hop(tok, (10, 8) if tok == "hx" else (8, 8))
        elif tok == "c":
            c.checkpoint("wA+")
        elif tok == "wA+":
            c.wall_a(1)
        elif tok == "d":
            c.double()
    length = c.finish()
    L += ["\n".join(c.out), "\n\n", scenery(t, length, min(c.xs) - 6, max(c.xs) + 6), "\n"]
    L.append("""var mcam = CreateEntity("MenuCamera");
EntityAddComponent(mcam, "Camera");
EntitySetComponentField(mcam, "Camera", "FieldOfView", 55);
EntitySetComponentField(mcam, "Camera", "FarClip", 1500);
EntitySetComponentField(mcam, "Camera", "Primary", true);
EntitySetPosition(mcam, 0, 16, 6);

var menu = CreateEntity("Menu");
EntityAddComponent(menu, "AudioSource");
EntitySetComponentField(menu, "AudioSource", "Clip", "Assets/Music/theme.mp3");
EntitySetComponentField(menu, "AudioSource", "Loop", true);
EntitySetComponentField(menu, "AudioSource", "PlayOnAwake", true);
EntitySetComponentField(menu, "AudioSource", "Is3D", false);
EntitySetComponentField(menu, "AudioSource", "Volume", 0.6);
EntitySetComponentField(menu, "AudioSource", "OutputGroup", "Music");
AddScript(menu, "Assets/Scripts/ParkourMenu.cqs");
""")
    L.append(f'SetScriptField(menu, "levelData", "{data}");\n\n')
    L.append(SEP + "Arayüz\n")
    L.append("""var canvas = UICreateCanvas("MenuCanvas");
EntitySetComponentField(canvas, "CanvasScaler", "ReferenceResolution", {x: 1920, y: 1080});
EntitySetComponentField(canvas, "CanvasScaler", "MatchWidthOrHeight", 0.5);
""")
    L.append(helpers)
    L.append("""var root = Layer("MenuRoot", canvas, 0.35);
Anchor(Txt("MenuTitle", root, "PARKOUR", 0, -120, 1600, 150, 120, white, "Center", FONT), 0.5, 1.0);
Anchor(Txt("MenuSub", root, "SELECT A LEVEL", 0, -215, 1000, 50, 30, amber, "Center", FONT), 0.5, 1.0);
var cy = 170;
for (var i = 0; i < CATEGORY_COUNT; i++)
{
    Btn("CatBtn" + i, root, "", -520, cy - i * 82, 440);
}
Txt("CategoryTitle", root, "", 200, 250, 760, 60, 40, amber, "Left", FONT);
for (var i = 0; i < 4; i++)
{
    var lb = Btn("LvlBtn" + i, root, "", 200, 160 - i * 112, 760);
    UISetSize(lb, 760, 92);
    EntitySetComponentField(EntityGetChildren(lb)[0], "UIText", "AlignH", "Left");
    EntitySetComponentField(EntityGetChildren(lb)[0], "UIText", "FontSize", 28);
    Txt("LvlInfo" + i, root, "", 360, 160 - i * 112, 400, 50, 24, dim, "Right", NUMFONT);
}
Anchor(Txt("MedalText", root, "", 0, 150, 1400, 44, 26, dim, "Center", NUMFONT), 0.5, 0.0);
Anchor(Txt("MenuHint", root, "ARROWS / ENTER  OR  MOUSE      ESC BACK", 0, 90, 1600, 36, 20, dim, "Center", FONT), 0.5, 0.0);
var quit = Anchor(Btn("QuitBtn", root, "QUIT", -150, 70, 220), 1.0, 0.0);

var fade = UICreateElement("Image", canvas, "Fade");
EntitySetComponentField(fade, "RectTransform", "AnchorMin", {x: 0, y: 0});
EntitySetComponentField(fade, "RectTransform", "AnchorMax", {x: 1, y: 1});
EntitySetComponentField(fade, "RectTransform", "SizeDelta", {x: 0, y: 0});
EntitySetComponentField(fade, "UIImage", "CornerRadius", 0);
EntitySetColor(fade, 0, 0, 0, 1);
EntitySetComponentField(fade, "UIImage", "RaycastTarget", false);
CreateEntity("BuildSceneDone");
""".replace("CATEGORY_COUNT", str(len(CATEGORIES))))
    return "".join(L)


def main():
    with open(os.path.join(TOOLS, "BuildScene.cqs"), encoding="utf-8") as fp:
        base = fp.read()
    head, sec = split_sections(base)
    os.makedirs(OUT, exist_ok=True)
    proj = os.path.relpath(ROOT, os.getcwd())
    order = [(c, i) for c in CATEGORIES for i in range(4)]
    auto = ["# Generated by Tools/gen_levels.py: builds every level scene (run in the editor with CQ_AUTOMATION)", "wait 30"]
    summary = []
    for idx, (cat, i) in enumerate(order):
        nxt = scene_name(*order[idx + 1]) if idx + 1 < len(order) else ""
        if LEVELS[cat][i] is None:
            continue
        name, design = LEVELS[cat][i]
        t = THEMES[cat]
        course, length = build_course(t, design, seed=1000 + idx * 37)
        est = length / 7.5 + (14 if course.movers else 0)
        gold, silver, bronze = medal_times(cat, i, est)
        fields = dict(levelId=level_id(cat, i), levelName=name, category=t["title"], nextLevel=nxt,
                      courseLength=length, routeData=";".join(course.route), hintData="|".join(course.hints),
                      goldTime=gold, silverTime=silver, bronzeTime=bronze)
        src = [f"// Generated by Tools/gen_levels.py - {t['title']} / {name}. Edit the generator, not this file.\n",
               head.replace("var seed = 1337;", f"var seed = {1337 + idx * 101};"), "\n", materials(t), PIECE_HELPERS, "\n",
               lighting(t), "\n", SEP + "Parkur\n", "\n".join(course.out), "\nDust();\n\n",
               scenery(t, length, min(course.xs) - 6, max(course.xs) + 6), "\n",
               sec["Oyuncu"].rstrip() + "\n", post_player(t), game_fields(fields), "\n",
               sec["Arayüz"]]
        out = "".join(src)
        out = out.replace('Txt("TitleSub", title, "SKYLINE RUN"', f'Txt("TitleSub", title, "{name}"')
        out = out.replace('Txt("CheckpointText", hud, "0 / 3"', f'Txt("CheckpointText", hud, "0 / {course.cp}"')
        scene = scene_name(cat, i)
        path = os.path.join(OUT, scene + ".cqs")
        with open(path, "w", encoding="utf-8") as fp:
            fp.write(out)
        auto += ["new_scene", f'run_cqs_file "{path}"', "wait 3", "entity expect_active BuildSceneDone true",
                 "entity delete BuildSceneDone", f'save_scene_as "{os.path.join(ROOT, "Assets", "Scenes", scene + ".cqscene")}"']
        summary.append(f"{scene:12s} {name:16s} cp={course.cp} len={length:.0f} gold={gold}")
    data = ";".join(THEMES[c]["title"] + ":" + ",".join(
        f"{scene_name(c, i)}|{level_id(c, i)}|{(LEVELS[c][i] or ('SKYLINE RUN',))[0]}" for i in range(4)) for c in CATEGORIES)
    path = os.path.join(OUT, "Menu.cqs")
    with open(path, "w", encoding="utf-8") as fp:
        fp.write(build_menu(head, sec, data))
    auto += ["new_scene", f'run_cqs_file "{path}"', "wait 3", "entity expect_active BuildSceneDone true",
             "entity delete BuildSceneDone", f'save_scene_as "{os.path.join(ROOT, "Assets", "Scenes", "Menu.cqscene")}"']
    auto.append("quit")
    with open(os.path.join(OUT, "build_levels.txt"), "w", encoding="utf-8") as fp:
        fp.write("\n".join(auto) + "\n")
    with open(os.path.join(OUT, "levels.json"), "w", encoding="utf-8") as fp:
        json.dump([{"category": THEMES[c]["title"], "levels": [
            {"scene": scene_name(c, i), "id": level_id(c, i), "name": (LEVELS[c][i] or ("SKYLINE RUN",))[0]} for i in range(4)]}
            for c in CATEGORIES], fp, indent=1)
    print("\n".join(summary))
    _ = proj


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Short Film template data: "The Lantern", a one-minute cinematic told entirely with the Timeline.

Writes (all original, CC0):
  Resources/Templates/Projects/shortfilm/Assets/Film/TheLantern.cqtimeline   camera cuts, actors, lights, titles, music
  Resources/Templates/Projects/shortfilm/Assets/Film/Score.ogg              the score, synthesized here (no samples)
  Tools/Templates/shortfilm/Film.inc                                         start poses for the scene builder (Main.cqs)

Story: at dusk Leo crosses the plaza carrying a small box and waits on a bench by the fountain. Mira comes. He gives her
the box; she opens it and a paper lantern rises out of it. As it climbs, the plaza lamps light up one by one, Mira dances,
and the lantern drifts away into the evening sky.

Coordinates are the shared Plaza set (Tools/Templates/TemplateKit.cqs, PlazaSet): fountain at the origin, benches on a
6.4 m ring facing it. The engine's conventions: cameras look down -Z, characters face +Z, Euler angles in degrees
(pitch about X first, then yaw about Y).

usage: make_film.py [--no-score]
"""
import math
import os
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
FILM = os.path.join(ROOT, "Resources", "Templates", "Projects", "shortfilm", "Assets", "Film")

FPS = 30.0
DURATION = 68.0
FRAME = 1.0 / FPS

# Animation clip order inside Human_Male.glb / Human_Female.glb (Shared/Mannequin/Models/clips.json)
CLIPS = ['idle', 'walk', 'jog', 'sprint', 'jump_start', 'jump_loop', 'jump_land', 'crouch_idle', 'crouch_walk', 'roll',
         'pistol_idle', 'pistol_aim', 'pistol_aim_up', 'pistol_aim_down', 'pistol_shoot', 'pistol_reload', 'hit', 'hit_head',
         'death', 'punch_jab', 'punch_cross', 'interact', 'pickup', 'driving', 'sit_idle', 'sit_talk', 'talk', 'walk_formal',
         'dance', 'sword_idle', 'sword_attack', 'swim_idle', 'swim', 'push', 'fixing', 'idle_arms_folded', 'phone', 'climb_up',
         'slide_start', 'slide', 'slide_exit', 'knockback', 'throw', 'carry', 'zombie_walk', 'zombie_idle', 'zombie_attack',
         'yes', 'no', 'open_chest']
WALK_SPEED = 1.03          # m/s at which the walk clips do not slide (Mannequin blend tree threshold)


# ------------------------------------------------------------------------------------------------ math helpers
def v3(x, y, z):
    return np.array([x, y, z], dtype=float)


def yaw_facing(dx, dz):
    """Yaw (deg) that turns a character (+Z forward) towards (dx, dz)."""
    return math.degrees(math.atan2(dx, dz))


def look_rotation(eye, target):
    """(pitch, yaw) in degrees for a camera at eye looking at target (-Z forward, pitch then yaw)."""
    d = target - eye
    d = d / max(np.linalg.norm(d), 1e-9)
    yaw = math.degrees(math.atan2(-d[0], -d[2]))
    pitch = math.degrees(math.asin(max(-1.0, min(1.0, d[1]))))
    return pitch, yaw


def unwrap(prev, angle):
    """angle (deg) moved by multiples of 360 to be closest to prev (keys must not spin the long way round)."""
    while angle - prev > 180.0:
        angle -= 360.0
    while angle - prev < -180.0:
        angle += 360.0
    return angle


def lerp(a, b, t):
    return a + (b - a) * t


def smooth(t):
    t = min(1.0, max(0.0, t))
    return t * t * (3.0 - 2.0 * t)


# ------------------------------------------------------------------------------------------------ the set
BENCH_ANGLE = 67.5                                   # Bench 2 in PlazaBenches (east-north-east of the fountain)
_r = math.radians(BENCH_ANGLE)
BENCH = v3(math.sin(_r) * 6.4, 0.0, math.cos(_r) * 6.4)
BENCH_YAW = BENCH_ANGLE + 180.0
_yr = math.radians(BENCH_YAW)
BENCH_FRONT = v3(math.sin(_yr), 0.0, math.cos(_yr))     # towards the fountain
BENCH_SIDE = v3(math.cos(_yr), 0.0, -math.sin(_yr))     # local +X


def seat(s):
    """Seat point of PlazaBenches: local (s, 0, 0.27)."""
    return BENCH + BENCH_SIDE * s + BENCH_FRONT * 0.27


SEAT_LEO = seat(0.36)                 # north seat
SEAT_BOX = seat(-0.36)                # south seat: the gift box
BOX_ON_BENCH = SEAT_BOX + v3(0, 0.45, 0) - BENCH_FRONT * 0.06      # bench seat surface ~0.45 m
LAMPS = [v3(x * 8.52, 4.2, z * 8.52) for x, z in ((1, 1), (-1, 1), (1, -1), (-1, -1))]   # PlazaTreesAndLamps order


# ------------------------------------------------------------------------------------------------ timeline model
SMOOTH, LINEAR, CONSTANT = 0, 1, 2


class Track:
    def __init__(self, name, kind, component="", field="", post=0):
        self.name, self.kind, self.component, self.field, self.post = name, kind, component, field, post
        self.channels = {}
        self.clips = []

    def key(self, channel, t, value, interp=SMOOTH):
        vals = list(value) + [0.0] * (4 - len(value))
        self.channels.setdefault(channel, []).append((round(t, 4), vals[:4], interp))

    def clip(self, start, duration, name="", anim=None, loop=True, speed=1.0, clip_in=0.0, asset="", volume=1.0):
        c = {"Start": round(start, 4), "Duration": round(duration, 4)}
        if name: c["Name"] = name
        if asset: c["Asset"] = asset
        if volume != 1.0: c["Volume"] = volume
        if speed != 1.0: c["Speed"] = speed
        if clip_in: c["ClipIn"] = clip_in
        if anim is not None: c["AnimationIndex"] = CLIPS.index(anim)
        if not loop: c["Loop"] = False
        self.clips.append(c)


def fmt(x):
    s = f"{x:.4f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def write_timeline(path, tracks):
    out = ["Timeline:", f"  Duration: {fmt(DURATION)}", f"  FrameRate: {fmt(FPS)}", "  Tracks:"]
    for t in tracks:
        out.append(f"    - Name: {t.name}")
        out.append(f"      Type: {t.kind}")
        if t.kind == "Activation":
            out.append(f"      PostPlayback: {t.post}")
        if t.kind == "Property":
            out.append(f"      Component: {t.component}")
            out.append(f"      Field: {t.field}")
        if t.channels:
            out.append("      Channels:")
            for name, keys in t.channels.items():
                keys = sorted(keys, key=lambda k: k[0])
                out.append(f"        - Name: {name}")
                out.append("          Keys:")
                for (tm, v, it) in keys:
                    extra = f", I: {it}" if it != SMOOTH else ""
                    out.append(f"            - {{T: {fmt(tm)}, V: [{', '.join(fmt(x) for x in v)}]{extra}}}")
        if t.clips:
            out.append("      Clips:")
            for c in sorted(t.clips, key=lambda c: c["Start"]):
                first = True
                for k, v in c.items():
                    val = ("false" if v is False else "true") if isinstance(v, bool) else (fmt(v) if isinstance(v, float) else v)
                    out.append(("        - " if first else "          ") + f"{k}: {val}")
                    first = False
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")


# ------------------------------------------------------------------------------------------------ actors
class Path:
    """Piecewise-linear walk at constant speed: (time, position) samples, yaw along the direction of travel."""
    def __init__(self, t0, points, speed=WALK_SPEED):
        self.t0, self.points, self.speed = t0, [np.asarray(p, float) for p in points], speed
        self.lengths = [np.linalg.norm(self.points[i + 1] - self.points[i]) for i in range(len(self.points) - 1)]
        self.t1 = t0 + sum(self.lengths) / speed

    def at(self, t):
        d = max(0.0, (t - self.t0) * self.speed)
        for i, L in enumerate(self.lengths):
            if d <= L or i == len(self.lengths) - 1:
                u = min(1.0, d / max(L, 1e-9))
                return lerp(self.points[i], self.points[i + 1], u), self.points[i + 1] - self.points[i]
            d -= L
        return self.points[-1], self.points[-1] - self.points[-2]


def walk_keys(track, path, step=0.5, corner=0.6):
    """Position (linear, constant speed) and yaw (smooth) keys along a path; yaw turns ahead of each corner."""
    t = path.t0
    yaw_prev = None
    while True:
        p, d = path.at(t)
        # look a little ahead so the body turns into corners instead of snapping
        pa, da = path.at(min(path.t1, t + corner))
        heading = (pa - p) if np.linalg.norm(pa - p) > 1e-3 else d
        yaw = yaw_facing(heading[0], heading[2])
        yaw = yaw if yaw_prev is None else unwrap(yaw_prev, yaw)
        track.key("Position", t, p, LINEAR)
        track.key("Rotation", t, (0.0, yaw, 0.0))
        yaw_prev = yaw
        if t >= path.t1 - 1e-6:
            break
        t = min(path.t1, t + step)
    return yaw_prev


def hold(track, t, pos, yaw, interp=SMOOTH):
    track.key("Position", t, pos, interp)
    track.key("Rotation", t, (0.0, yaw, 0.0), interp)


# ------------------------------------------------------------------------------------------------ the film
def build():
    tracks = []

    # ---------------------------------------------------------------- Leo: carries the box across the plaza
    leo = Track("Leo", "Transform")
    leo_anim = Track("Leo Animation", "Animation")
    front_of_leo_seat = SEAT_LEO + BENCH_FRONT * 0.55
    leo_walk = Path(6.3, [v3(4.9, 0, 13.6), v3(4.15, 0, 7.4), front_of_leo_seat])
    leo_start_yaw = yaw_facing(*(leo_walk.points[1] - leo_walk.points[0])[[0, 2]])
    hold(leo, 0.0, leo_walk.points[0], leo_start_yaw)
    yaw = walk_keys(leo, leo_walk)
    # stops (hidden by the cut to Mira), sets the box down, turns and sits
    seated_yaw = unwrap(yaw, BENCH_YAW)
    hold(leo, leo_walk.t1 + 0.6, front_of_leo_seat, unwrap(yaw, yaw_facing(*(-BENCH_FRONT)[[0, 2]])))
    hold(leo, leo_walk.t1 + 1.6, front_of_leo_seat, seated_yaw)
    hold(leo, leo_walk.t1 + 2.4, SEAT_LEO, seated_yaw)
    t_sat = leo_walk.t1 + 2.4
    leo_anim.clip(0.0, leo_walk.t1 + 0.4, "carry", "carry")
    leo_anim.clip(leo_walk.t1, 2.0, "idle", "idle")
    leo_anim.clip(leo_walk.t1 + 1.6, 34.6 - (leo_walk.t1 + 1.6), "sit", "sit_idle")

    # ---------------------------------------------------------------- Mira: walks in from the café side
    mira = Track("Mira", "Transform")
    mira_anim = Track("Mira Animation", "Animation")
    mira_spot = SEAT_LEO + BENCH_FRONT * 1.45                    # in front of Leo
    mira_walk = Path(14.6, [v3(4.55, 0, -11.2), v3(4.1, 0, -2.4), mira_spot])
    hold(mira, 0.0, mira_walk.points[0], yaw_facing(*(mira_walk.points[1] - mira_walk.points[0])[[0, 2]]))
    yaw_m = walk_keys(mira, mira_walk)
    to_leo = SEAT_LEO - mira_spot
    face_leo = unwrap(yaw_m, yaw_facing(to_leo[0], to_leo[2]))
    hold(mira, mira_walk.t1 + 0.7, mira_spot, face_leo)
    mira_anim.clip(0.0, mira_walk.t1 + 0.5, "walk", "walk_formal")
    mira_anim.clip(mira_walk.t1, 1.4, "idle", "idle")
    t_talk = mira_walk.t1 + 1.0
    mira_anim.clip(t_talk, 34.9 - t_talk, "talk", "talk")
    leo_anim.clip(t_talk - 0.6, 34.6 - (t_talk - 0.6), "sit talk", "sit_talk")

    # ---------------------------------------------------------------- the box: Leo gives it, Mira opens it
    box_spot = SEAT_BOX + BENCH_FRONT * 0.62                     # where Mira stands to open it
    t_step = 34.6
    to_box = box_spot - mira_spot
    hold(mira, t_step, mira_spot, face_leo)
    step_dir_yaw = unwrap(face_leo, yaw_facing(to_box[0], to_box[2]))
    hold(mira, t_step + 0.5, lerp(mira_spot, box_spot, 0.25), step_dir_yaw)
    hold(mira, t_step + 1.4, box_spot, unwrap(step_dir_yaw, yaw_facing(*(-BENCH_FRONT)[[0, 2]])))
    mira_anim.clip(t_step - 0.3, 1.9, "walk", "walk_formal")
    t_open = t_step + 1.5
    mira_anim.clip(t_open, 3.4, "open", "open_chest", loop=False)
    t_lid = t_open + 0.55                                          # hands on the lid
    mira_anim.clip(t_open + 2.6, 9.0, "idle", "idle")

    # Leo stands up during the cut to the lantern and steps beside her
    leo_stand = SEAT_LEO + BENCH_FRONT * 0.6 + BENCH_SIDE * 0.15
    hold(leo, 41.6, SEAT_LEO, seated_yaw)
    hold(leo, 42.2, leo_stand, seated_yaw)
    leo_anim.clip(34.6 - 0.8, 41.9 - 33.8, "sit", "sit_idle")
    leo_anim.clip(41.9, 9.0, "idle", "idle")

    box_lid = Track("Gift Box Lid", "Transform")
    box_lid.key("Rotation", 0.0, (0, 0, 0), CONSTANT)
    box_lid.key("Rotation", t_lid, (0, 0, 0))
    box_lid.key("Rotation", t_lid + 0.8, (-112, 0, 0))
    box_glow = Track("Gift Box Glow", "Property", "PointLightComponent", "Intensity")
    box_glow.key("Value", 0.0, (0,), CONSTANT)
    box_glow.key("Value", t_lid + 0.2, (0,))
    box_glow.key("Value", t_lid + 1.6, (6.0,))
    box_glow.key("Value", 44.0, (3.0,))
    box_glow.key("Value", 48.0, (0.6,))
    carried = Track("Gift Box Carried", "Activation", post=2)
    carried.clip(0.0, leo_walk.t1 + 0.9, "in Leo's hands")
    placed = Track("Gift Box", "Activation", post=1)
    placed.clip(leo_walk.t1 + 0.9, DURATION - (leo_walk.t1 + 0.9), "on the bench")

    # ---------------------------------------------------------------- the sky lantern
    t_rise = t_lid + 1.4
    lantern = Track("Sky Lantern", "Transform")
    lantern_on = Track("Sky Lantern Active", "Activation", post=1)
    lantern_on.clip(t_rise - 0.2, DURATION - t_rise + 0.2, "lit")
    start = BOX_ON_BENCH + v3(0, 0.05, 0)
    route = [(t_rise, start), (t_rise + 2.5, start + v3(-0.05, 0.85, 0.02)), (t_rise + 6.0, start + v3(-0.25, 2.7, -0.1)),
             (50.0, start + v3(-0.6, 4.6, -0.35)), (54.0, start + v3(-1.2, 7.2, -0.9)), (58.0, start + v3(-2.2, 10.5, -1.8)),
             (63.0, start + v3(-4.0, 16.0, -3.6)), (DURATION, start + v3(-6.0, 22.5, -5.6))]
    for i, (tm, p) in enumerate(route):
        sway = 0.0 if i == 0 else 7.0 * math.sin(tm * 1.3)
        lantern.key("Position", tm, p)
        lantern.key("Rotation", tm, (sway * 0.6, tm * 9.0, sway))
    lantern.key("Scale", t_rise - 0.2, (0.2, 0.2, 0.2))
    lantern.key("Scale", t_rise + 0.9, (1, 1, 1))
    lantern_paper = Track("Sky Lantern Glow", "Property", "MeshRendererComponent", "EmissionStrength")
    lantern_paper.key("Value", 0.0, (0.9,))
    lantern_paper.key("Value", 46.0, (1.0,))
    lantern_paper.key("Value", 56.0, (3.0,))
    lantern_paper.key("Value", DURATION, (6.0,))
    lantern_light = Track("Sky Lantern Light", "Property", "PointLightComponent", "Intensity")
    lantern_light.key("Value", t_rise - 0.2, (0,))
    lantern_light.key("Value", t_rise + 1.2, (6.0,))
    lantern_light.key("Value", 60.0, (6.0,))
    lantern_light.key("Value", DURATION, (4.0,))

    # ---------------------------------------------------------------- plaza lights come on, one by one
    lamp_times = [50.6, 51.3, 52.0, 52.7]
    lamp_tracks = []
    for i, tl in enumerate(lamp_times):
        tr = Track(f"Street Lamp {i + 1} Light", "Property", "PointLightComponent", "Intensity")
        tr.key("Value", 0.0, (0,), CONSTANT)
        tr.key("Value", tl, (0,))
        tr.key("Value", tl + 0.35, (24.0,))
        tr.key("Value", tl + 0.9, (18.0,))
        em = Track(f"Street Lamp {i + 1} Glass", "Property", "MeshRendererComponent", "EmissionStrength")
        em.key("Value", 0.0, (0.3,), CONSTANT)
        em.key("Value", tl, (0.3,))
        em.key("Value", tl + 0.35, (9.0,))
        em.key("Value", tl + 0.9, (6.0,))
        lamp_tracks += [tr, em]
    wall_times = [53.2 + 0.22 * i for i in range(11)]      # PlazaShopfronts: 4 north, 3 south, 4 on the sides
    for i, tl in enumerate(wall_times):
        tr = Track(f"Wall Lantern {i + 1} Light", "Property", "PointLightComponent", "Intensity")
        tr.key("Value", 0.0, (0,), CONSTANT)
        tr.key("Value", tl, (0,))
        tr.key("Value", tl + 0.5, (7.0,))
        lamp_tracks.append(tr)

    # Mira dances under the lights, Leo nods
    mira_anim.clip(50.4, 17.6, "dance", "dance")
    leo_anim.clip(51.4, 2.2, "yes", "yes", loop=False)
    leo_anim.clip(53.2, 14.8, "idle", "idle")

    # ---------------------------------------------------------------- dusk: the sun sets during the film
    sun = Track("Sun", "Transform")
    # golden hour: the sun is high enough to reach the bench over the west rooftops, then sinks behind them
    # (the plaza falls into shade just as the lamps come on)
    sun.key("Rotation", 0.0, (-25.0, 290.0, 0))
    sun.key("Rotation", 42.0, (-14.0, 296.0, 0))
    sun.key("Rotation", 52.0, (-7.0, 299.0, 0))
    sun.key("Rotation", DURATION, (-2.0, 302.0, 0))
    sun_light = Track("Sun Light", "Property", "DirectionalLightComponent", "Intensity")
    sun_light.key("Value", 0.0, (3.0,))
    sun_light.key("Value", 40.0, (2.4,))
    sun_light.key("Value", 52.0, (1.0,))
    sun_light.key("Value", DURATION, (0.25,))
    exposure = Track("Exposure", "Property", "PostProcessVolumeComponent", "Exposure")
    exposure.key("Value", 0.0, (0.3,))
    exposure.key("Value", 45.0, (0.4,))
    exposure.key("Value", DURATION, (0.6,))

    # ---------------------------------------------------------------- camera: shots, cuts, lens and focus
    cam = Track("Film Camera", "Transform")
    fov = Track("Film Camera Lens", "Property", "CameraComponent", "FieldOfView")
    focus = Track("Focus", "Property", "PostProcessVolumeComponent", "FocusDistance")
    aperture = Track("Aperture", "Property", "PostProcessVolumeComponent", "Aperture")

    def leo_at(t, h=1.45):
        return np.asarray(evaluate_position(leo, t)) + v3(0, h, 0)

    def mira_at(t, h=1.5):
        return np.asarray(evaluate_position(mira, t)) + v3(0, h, 0)

    shots = []

    def shot(t0, t1, eyes, targets, lens, focus_on=None, fstop=4.0, ease=False):
        """eyes / targets: callables of t or fixed points; keys every 0.5 s, last key Constant (cut)."""
        shots.append((t0, t1))
        n = max(2, int(round((t1 - t0) / 0.5)) + 1)
        yaw_prev = None
        for i in range(n):
            u = i / (n - 1)
            uu = smooth(u) if ease else u
            t = lerp(t0, t1 - FRAME * 0.5, u)
            eye = eyes(t, uu) if callable(eyes) else eyes
            tgt = targets(t, uu) if callable(targets) else targets
            pitch, yaw = look_rotation(eye, tgt)
            yaw = yaw if yaw_prev is None else unwrap(yaw_prev, yaw)
            yaw_prev = yaw
            interp = CONSTANT if i == n - 1 else SMOOTH
            cam.key("Position", t, eye, interp)
            cam.key("Rotation", t, (pitch, yaw, 0.0), interp)
            f = focus_on(t) if focus_on else tgt
            focus.key("Value", t, (float(np.linalg.norm(f - eye)),), interp)
        lens0, lens1 = lens if isinstance(lens, tuple) else (lens, lens)
        fov.key("Value", t0, (lens0,))
        fov.key("Value", t1 - FRAME * 0.5, (lens1,), CONSTANT)
        aperture.key("Value", t0, (fstop,))
        aperture.key("Value", t1 - FRAME * 0.5, (fstop,), CONSTANT)

    LEFT = BENCH_SIDE                         # seated Leo's left (the bench's local +X); his right is -LEFT
    leo_seated = SEAT_LEO + v3(0, 1.02, 0)
    # 1  establishing: over the rooftops of the south street, crane down into the plaza at golden hour
    shot(0.0, 6.9,
         lambda t, u: lerp(v3(5.0, 19.0, 31.0), v3(5.6, 3.2, 15.2), u),
         lambda t, u: lerp(v3(3.0, 1.0, -2.0), v3(4.6, 1.3, 6.0), u), (48.0, 40.0), fstop=11.0, ease=True)
    # 2  Leo comes in from the street and walks past the camera
    shot(6.9, 11.6,
         lambda t, u: lerp(v3(6.7, 1.45, 6.6), v3(6.6, 1.5, 6.9), u),
         lambda t, u: leo_at(t, 1.35), (30.0, 34.0), fstop=2.8)
    # 3  tracking beside him across the stones
    shot(11.6, 16.6,
         lambda t, u: leo_at(t, 1.35) + v3(2.2, 0.0, 0.4),
         lambda t, u: leo_at(t, 1.3) + v3(0, 0, -0.6), 32.0, fstop=2.8)
    # 4  Mira, from the café side, walking towards the fountain
    shot(16.6, 21.0,
         lambda t, u: mira_at(t, 1.5) + v3(-0.9, 0.0, 2.6),
         lambda t, u: mira_at(t, 1.45), 34.0, fstop=2.4)
    # 5  Leo waits on the bench, Mira comes up behind him (focus pulls from Leo to her)
    shot(21.0, 27.2,
         lambda t, u: SEAT_LEO + BENCH_FRONT * 2.2 + LEFT * 1.6 + v3(0, 1.12, 0) + LEFT * 0.25 * u,
         lambda t, u: lerp(leo_seated, mira_at(t, 1.35), 0.45 * smooth(u)), 36.0,
         focus_on=lambda t: leo_seated if t < 24.3 else mira_at(t, 1.4), fstop=2.0)
    # 6  conversation: over Mira's shoulder onto Leo, slow push in
    shot(27.2, 31.2,
         lambda t, u: mira_spot + v3(0, 1.55, 0) - (SEAT_LEO - mira_spot) * 0.55 - LEFT * 0.35 + (SEAT_LEO - mira_spot) * 0.12 * u,
         lambda t, u: leo_seated + v3(0, 0.15, 0), 30.0, fstop=2.0)
    # 6b reverse: Mira, seen past Leo's shoulder
    shot(31.2, 34.6,
         lambda t, u: SEAT_LEO + v3(0, 1.28, 0) + LEFT * 0.5 - BENCH_FRONT * 0.1 + BENCH_FRONT * 0.1 * u,
         lambda t, u: mira_at(t, 1.5), 30.0, fstop=2.0)
    # 7  the box, from behind the bench: Mira bends to open it, warm light spills out. Cuts before the lantern appears:
    #    from here it would rise right in front of Leo's face
    t_cut8 = t_rise - 0.35
    shot(34.6, t_cut8,
         lambda t, u: BOX_ON_BENCH - LEFT * 1.55 + BENCH_FRONT * 0.75 + v3(0, 0.5, 0) + LEFT * 0.2 * u,
         lambda t, u: lerp(BOX_ON_BENCH + v3(0, 0.15, 0), mira_at(t, 1.3), 0.42) + v3(0, 0.3 * smooth((t - 38.0) / 3.0), 0), 40.0,
         focus_on=lambda t: lerp(BOX_ON_BENCH, mira_at(t, 1.2), 0.5), fstop=2.4)
    # 8  low angle from the fountain side: the lantern rises between them
    shot(t_cut8, 50.2,
         lambda t, u: BOX_ON_BENCH + BENCH_FRONT * 2.7 + LEFT * 0.6 + v3(0, 0.05, 0) + v3(0, 0.3, 0) * u,
         lambda t, u: np.asarray(evaluate_position(lantern, t)) * 0.5 + (BOX_ON_BENCH + v3(0, 1.3, 0)) * 0.5, (50.0, 54.0),
         focus_on=lambda t: np.asarray(evaluate_position(lantern, t)), fstop=2.8, ease=True)
    # 9  wide: the plaza lights up one lamp after another, Mira dances
    #    (camera outside the fountain basin: it used to sit under the upper bowl, in the spray)
    shot(50.2, 58.4,
         lambda t, u: lerp(BENCH + BENCH_FRONT * 2.6 + LEFT * 4.4 + v3(0, 2.3, 0), BENCH + BENCH_FRONT * 3.0 + LEFT * 3.6 + v3(0, 2.8, 0), u),
         lambda t, u: lerp(BENCH + BENCH_FRONT * 0.2 - LEFT * 0.8 + v3(0, 1.6, 0), BENCH + BENCH_FRONT * 0.2 - LEFT * 0.8 + v3(0, 2.1, 0), u), 50.0,
         focus_on=lambda t: BENCH + BENCH_FRONT * 0.6 + v3(0, 1.2, 0), fstop=4.0, ease=True)
    # 10 the lantern drifts away: crane up from behind them into the evening sky
    shot(58.4, DURATION + FRAME,
         lambda t, u: lerp(BENCH + v3(3.0, 1.6, 2.2), BENCH + v3(5.5, 5.0, 6.0), u),
         lambda t, u: lerp(BENCH + v3(-4.0, 1.5, -2.0), np.asarray(evaluate_position(lantern, t)), 0.15 + 0.45 * u),
         (50.0, 56.0), focus_on=lambda t: BENCH + v3(-3.0, 2.0, -1.5), fstop=8.0, ease=True)

    # ---------------------------------------------------------------- titles and fades (Canvas Group alpha)
    title = Track("Title", "Property", "UICanvasGroupComponent", "Alpha")
    for (tm, a) in ((0.0, 0), (1.2, 0), (2.4, 1), (5.2, 1), (6.4, 0)):
        title.key("Value", tm, (a,))
    credit = Track("End Card", "Property", "UICanvasGroupComponent", "Alpha")
    for (tm, a) in ((0.0, 0), (60.8, 0), (62.2, 1), (65.6, 1), (66.8, 0)):
        credit.key("Value", tm, (a,))
    fade = Track("Fade", "Property", "UIImageComponent", "Color")
    for (tm, a) in ((0.0, 1.0), (0.9, 0.0), (65.8, 0.0), (DURATION - 0.15, 1.0)):
        fade.key("Value", tm, (0, 0, 0, a))

    # ---------------------------------------------------------------- music
    music = Track("Score", "Audio")
    music.clip(0.0, DURATION, "The Lantern (score)", asset="Assets/Film/Score.ogg", volume=0.9)

    tracks += [cam, fov, focus, aperture, leo, leo_anim, mira, mira_anim, carried, placed, box_lid, box_glow, lantern,
               lantern_on, lantern_light, lantern_paper, sun, sun_light, exposure] + lamp_tracks + [title, credit, fade, music]
    events = {"lamps": lamp_times, "walls": wall_times, "lid": t_lid, "rise": t_rise, "open": t_open, "sat": t_sat,
              "talk": t_talk, "dance": 50.4, "shots": shots}
    starts = {"leo": (leo_walk.points[0], leo_start_yaw),
              "mira": (mira_walk.points[0], yaw_facing(*(mira_walk.points[1] - mira_walk.points[0])[[0, 2]])),
              "box": BOX_ON_BENCH, "lantern": start}
    return tracks, events, starts


def evaluate_position(track, t):
    """Linear interpolation of a Transform track's Position keys (for aiming the camera at actors)."""
    keys = sorted(track.channels.get("Position", []), key=lambda k: k[0])
    if not keys:
        return v3(0, 0, 0)
    if t <= keys[0][0]:
        return np.array(keys[0][1][:3])
    for a, b in zip(keys, keys[1:]):
        if a[0] <= t <= b[0]:
            u = (t - a[0]) / max(b[0] - a[0], 1e-9)
            if a[2] == CONSTANT:
                u = 0.0
            return np.array(a[1][:3]) * (1 - u) + np.array(b[1][:3]) * u
    return np.array(keys[-1][1][:3])


# ------------------------------------------------------------------------------------------------ score
SR = 48000


def env_adsr(n, a, d, s, r, sr=SR):
    t = np.arange(n) / sr
    e = np.where(t < a, t / max(a, 1e-6), 1.0)
    e = np.where(t >= a, s + (1 - s) * np.exp(-(t - a) / max(d, 1e-6)), e)
    rel = n / sr - r
    e = np.where(t > rel, e * np.clip((n / sr - t) / max(r, 1e-6), 0, 1), e)
    return e


def piano(freq, dur, vel=0.6):
    n = int((dur + 2.5) * SR)
    t = np.arange(n) / SR
    out = np.zeros(n)
    for k in range(1, 9):
        f = freq * k * math.sqrt(1 + 0.00035 * k * k)                     # slight string inharmonicity
        if f > SR / 2.2:
            break
        amp = vel / (k ** 1.15) * (1.0 if k == 1 else 0.7)
        decay = 2.8 / (1 + 0.45 * k)
        out += amp * np.sin(2 * math.pi * f * t + 0.3 * k) * np.exp(-t / decay)
    hammer = np.random.default_rng(int(freq)).normal(0, 1, n) * np.exp(-t / 0.004) * 0.05 * vel
    out += hammer
    out *= np.clip((dur + 0.25 - t) / 0.25, 0, 1) * 0.6 + 0.4 * np.exp(-np.clip(t - dur, 0, None) / 0.6)
    return out


def pad(freq, dur, vel=0.25):
    n = int((dur + 1.5) * SR)
    t = np.arange(n) / SR
    out = np.zeros(n)
    for det in (-0.12, 0.0, 0.11):
        f0 = freq * (2 ** (det / 12.0))
        for k in range(1, 7):
            out += (vel / (k ** 1.4)) * np.sin(2 * math.pi * f0 * k * t + det * 7 + k)
    return out * env_adsr(n, 1.4, 2.0, 0.85, 1.4) / 3.0


def bell(freq, dur=3.5, vel=0.4):
    n = int(dur * SR)
    t = np.arange(n) / SR
    mod = np.sin(2 * math.pi * freq * 3.5 * t) * 2.2 * np.exp(-t / 0.9)
    tone = np.sin(2 * math.pi * freq * t + mod) * np.exp(-t / 1.6)
    tone += 0.35 * np.sin(2 * math.pi * freq * 2.76 * t) * np.exp(-t / 0.7)
    return vel * tone


def bass(freq, dur, vel=0.35):
    n = int((dur + 0.6) * SR)
    t = np.arange(n) / SR
    out = vel * (np.sin(2 * math.pi * freq * t) + 0.25 * np.sin(4 * math.pi * freq * t))
    return out * env_adsr(n, 0.05, 1.5, 0.7, 0.6)


def note(name):
    names = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}
    pitch, octave = name[:-1], int(name[-1])
    return 440.0 * 2 ** ((names[pitch] + 12 * (octave + 1) - 69) / 12.0)


def place(buf, sig, t, pan=0.0):
    i = int(t * SR)
    if i >= buf.shape[1]:
        return
    sig = sig[: buf.shape[1] - i]
    l, r = math.cos((pan + 1) * math.pi / 4), math.sin((pan + 1) * math.pi / 4)
    buf[0, i:i + len(sig)] += sig * l
    buf[1, i:i + len(sig)] += sig * r


def reverb(x, seconds=2.6, mix=0.32):
    rng = np.random.default_rng(7)
    n = int(seconds * SR)
    t = np.arange(n) / SR
    out = np.zeros_like(x)
    for ch in range(2):
        ir = rng.normal(0, 1, n) * np.exp(-t * 6.9 / seconds)
        ir[: int(0.012 * SR)] = 0
        ir /= np.sqrt(np.sum(ir ** 2))
        # FFT convolution
        m = len(x[ch]) + n
        size = 1 << (m - 1).bit_length()
        y = np.fft.irfft(np.fft.rfft(x[ch], size) * np.fft.rfft(ir, size), size)[: len(x[ch])]
        out[ch] = y
    return x * (1 - mix) + out * mix


def score(events, path_ogg):
    total = DURATION + 3.0
    buf = np.zeros((2, int(total * SR)))
    beat = 60.0 / 70.0                        # 70 bpm
    bar = beat * 4
    # D major: D  Bm  G  A | D  F#m  G  A | Bm  G  D  A | G  A  Bm  D ...
    prog = ["D", "Bm", "G", "A", "D", "F#m", "G", "A", "Bm", "G", "D", "A", "G", "A", "Bm", "D", "G", "A", "D", "D"]
    chords = {"D": ["D3", "F#3", "A3"], "Bm": ["B2", "D3", "F#3"], "G": ["G2", "B2", "D3"], "A": ["A2", "C#3", "E3"],
              "F#m": ["F#2", "A2", "C#3"]}
    roots = {"D": "D2", "Bm": "B1", "G": "G1", "A": "A1", "F#m": "F#1"}
    melody = {  # bar index -> (beat, note, length in beats)
        2: [(0, "D5", 1.5), (1.5, "E5", 0.5), (2, "F#5", 2)], 3: [(0, "E5", 2), (2, "C#5", 2)],
        4: [(0, "D5", 1), (1, "A4", 1), (2, "F#4", 1.5), (3.5, "A4", 0.5)], 5: [(0, "C#5", 3), (3, "A4", 1)],
        6: [(0, "B4", 1.5), (1.5, "D5", 0.5), (2, "G5", 2)], 7: [(0, "F#5", 1), (1, "E5", 1), (2, "C#5", 2)],
        10: [(0, "F#5", 1), (1, "A5", 1), (2, "D6", 2)], 11: [(0, "C#6", 2), (2, "A5", 2)],
        12: [(0, "B5", 1.5), (1.5, "A5", 0.5), (2, "G5", 2)], 13: [(0, "A5", 2), (2, "E5", 2)],
        14: [(0, "F#5", 1), (1, "D5", 1), (2, "B4", 2)], 15: [(0, "D5", 1), (1, "E5", 1), (2, "F#5", 2)],
        16: [(0, "G5", 1.5), (1.5, "F#5", 0.5), (2, "E5", 2)], 17: [(0, "A5", 2), (2, "C#6", 2)],
        18: [(0, "D6", 4)],
    }
    for i, ch in enumerate(prog):
        t0 = 0.6 + i * bar
        if t0 > DURATION:
            break
        quiet = 8 <= i <= 9                   # the conversation: just the pad and soft arpeggios
        for n_ in chords[ch]:
            place(buf, pad(note(n_), bar, 0.16 if not quiet else 0.11), t0, pan=-0.3)
            place(buf, pad(note(n_) * 2, bar, 0.06), t0, pan=0.35)
        place(buf, bass(note(roots[ch]), bar * 0.95, 0.30 if i >= 2 else 0.18), t0)
        # broken-chord piano: 8th notes
        arp = chords[ch] + [chords[ch][1][:-1] + str(int(chords[ch][1][-1]) + 1)]
        for k in range(8):
            nm = arp[k % 4]
            f = note(nm) * 2
            vel = (0.20 if k % 2 == 0 else 0.13) * (0.7 if quiet else 1.0) * (0.55 if i < 2 else 1.0)
            place(buf, piano(f, beat * 0.45, vel), t0 + k * beat * 0.5, pan=0.15 * math.sin(k))
        for (b, nm, ln) in melody.get(i, []):
            place(buf, piano(note(nm), ln * beat, 0.42), t0 + b * beat, pan=-0.1)
    # celesta glints: the lid, the lantern, every lamp
    place(buf, bell(note("A5"), vel=0.30), events["lid"], pan=0.2)
    place(buf, bell(note("D6"), vel=0.28), events["rise"], pan=-0.2)
    place(buf, bell(note("F#6"), vel=0.22), events["rise"] + 0.9, pan=0.3)
    lamp_notes = ["D6", "F#6", "A6", "D7"]
    for i, tm in enumerate(events["lamps"]):
        place(buf, bell(note(lamp_notes[i]), vel=0.26), tm, pan=[-0.5, 0.5, -0.3, 0.3][i])
    wall_notes = ["A5", "B5", "D6", "E6", "F#6", "A6", "B6"]
    for i, tm in enumerate(events["walls"]):
        place(buf, bell(note(wall_notes[i % 7]), 2.5, vel=0.10), tm, pan=math.sin(i * 1.7) * 0.6)
    buf = reverb(buf)
    # master: gentle fade in and out, normalize
    n = buf.shape[1]
    t = np.arange(n) / SR
    buf *= np.clip(t / 1.2, 0, 1) * np.clip((DURATION + 0.5 - t) / 3.0, 0, 1)
    buf /= max(1e-6, np.max(np.abs(buf))) / 0.89
    pcm = (np.clip(buf.T, -1, 1) * 32767).astype("<i2")
    with tempfile.TemporaryDirectory() as tmp:
        wav = os.path.join(tmp, "score.wav")
        import wave
        with wave.open(wav, "wb") as w:
            w.setnchannels(2)
            w.setsampwidth(2)
            w.setframerate(SR)
            w.writeframes(pcm.tobytes())
        os.makedirs(os.path.dirname(path_ogg), exist_ok=True)
        subprocess.run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", wav, "-c:a", "libvorbis", "-q:a", "5",
                        path_ogg], check=True)


# ------------------------------------------------------------------------------------------------ costumes
# The Mannequin package characters are plain grey figures. Their clothes are painted into a base color texture in the
# mesh's own UV space (Character Customizer > Outfit Texture): every texel gets the bind-pose position interpolated from
# its triangle and a region rule picks the cloth (shoes, trousers, sweater / dress, sleeves, skin, hair).
MANNEQUIN = os.path.join(ROOT, "Resources", "Templates", "Shared", "Mannequin", "Models")
ACTORS = os.path.join(FILM, "Actors")


def read_glb_mesh(path):
    import json
    import struct
    b = open(path, "rb").read()
    n = struct.unpack("<I", b[12:16])[0]
    j = json.loads(b[20:20 + n])
    base = 20 + n + 8

    def acc(i):
        a = j["accessors"][i]
        bv = j["bufferViews"][a["bufferView"]]
        comp = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[a["type"]]
        dt = {5126: np.float32, 5125: np.uint32, 5123: np.uint16, 5121: np.uint8}[a["componentType"]]
        off = base + bv.get("byteOffset", 0) + a.get("byteOffset", 0)
        size = np.dtype(dt).itemsize * comp
        stride = bv.get("byteStride", size)
        raw = np.frombuffer(b, np.uint8, stride * (a["count"] - 1) + size, off)
        rows = np.lib.stride_tricks.as_strided(raw, (a["count"], size), (stride, 1))
        return np.frombuffer(rows.tobytes(), dt).reshape(a["count"], comp)

    prim = j["meshes"][0]["primitives"][0]
    return acc(prim["attributes"]["POSITION"]).astype(float), acc(prim["attributes"]["TEXCOORD_0"]).astype(float), \
        acc(prim["indices"]).reshape(-1, 3)


def paint_outfit(model, rule, out_png, size=1024):
    from PIL import Image
    P, UV, T = read_glb_mesh(os.path.join(MANNEQUIN, model + ".glb"))
    height = P[:, 1].max()
    img = np.zeros((size, size, 3), np.float32)
    filled = np.zeros((size, size), bool)
    rng = np.random.default_rng(3)
    for tri in T:
        uv = UV[tri] * size
        uv[:, 1] = uv[:, 1]                      # glTF UV origin is the top-left texel: no flip
        x0, y0 = np.floor(uv.min(0)).astype(int)
        x1, y1 = np.ceil(uv.max(0)).astype(int)
        x0, y0 = max(x0, 0), max(y0, 0)
        x1, y1 = min(x1, size - 1), min(y1, size - 1)
        if x1 < x0 or y1 < y0:
            continue
        xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        a, b_, c = uv
        den = (b_[1] - c[1]) * (a[0] - c[0]) + (c[0] - b_[0]) * (a[1] - c[1])
        if abs(den) < 1e-12:
            continue
        w0 = ((b_[1] - c[1]) * (xs - c[0]) + (c[0] - b_[0]) * (ys - c[1])) / den
        w1 = ((c[1] - a[1]) * (xs - c[0]) + (a[0] - c[0]) * (ys - c[1])) / den
        w2 = 1 - w0 - w1
        inside = (w0 >= -0.02) & (w1 >= -0.02) & (w2 >= -0.02)
        if not inside.any():
            continue
        pos = (w0[..., None] * P[tri[0]] + w1[..., None] * P[tri[1]] + w2[..., None] * P[tri[2]])[inside]
        cols = rule(pos[:, 0], pos[:, 1] / height, pos[:, 2])
        yy, xx = np.nonzero(inside)
        img[yy + y0, xx + x0] = cols
        filled[yy + y0, xx + x0] = True
    # gentle fabric grain, then fill the gaps between UV islands so mip levels do not bleed black seams
    img *= (0.94 + 0.06 * rng.random((size, size, 1))).astype(np.float32)
    for _ in range(6):
        grow = ~filled
        acc = np.zeros_like(img)
        cnt = np.zeros((size, size), np.float32)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            sh = np.roll(np.roll(img, dy, 0), dx, 1)
            sf = np.roll(np.roll(filled, dy, 0), dx, 1)
            acc += sh * sf[..., None]
            cnt += sf
        newly = grow & (cnt > 0)
        img[newly] = acc[newly] / cnt[newly][:, None]
        filled |= newly
    srgb = np.clip(img, 0, 1) ** (1 / 2.2)
    os.makedirs(os.path.dirname(out_png), exist_ok=True)
    Image.fromarray((srgb * 255 + 0.5).astype(np.uint8)).save(out_png, optimize=True)


def lin(r, g, b):
    """sRGB (0..1) -> linear for the paint buffer."""
    return np.array([r, g, b]) ** 2.2


def leo_rule(x, h, z):
    ax = np.abs(x)
    c = np.tile(lin(0.80, 0.60, 0.47), (len(h), 1))                     # skin
    arm = (ax > 0.2) & (h > 0.62)                                       # T-pose arms at shoulder height
    sweater = (~arm & (h >= 0.535) & (h < 0.80)) | (arm & (ax < 0.69) & (h > 0.70))
    knit = lin(0.13, 0.22, 0.42) * (1.0 + 0.06 * np.sin(ax * 400.0))[:, None]   # navy sweater (ochre read as skin)
    c = np.where(sweater[:, None], knit, c)
    c = np.where((~arm & (h >= 0.80) & (h < 0.815) & (z > -0.02))[:, None], lin(0.09, 0.15, 0.30), c)    # collar
    c = np.where((~arm & (h >= 0.52) & (h < 0.535))[:, None], lin(0.20, 0.13, 0.08), c)                  # belt
    c = np.where((~arm & (h >= 0.085) & (h < 0.52))[:, None], lin(0.36, 0.29, 0.20), c)                  # trousers (khaki)
    c = np.where((h < 0.085)[:, None], lin(0.24, 0.15, 0.09), c)                                        # shoes
    c = np.where((h < 0.012)[:, None], lin(0.08, 0.07, 0.07), c)                                        # soles
    hair = (~arm & (h > 0.968)) | (~arm & (h > 0.95) & (z < 0.04)) | (~arm & (h > 0.87) & (z < -0.045))
    c = np.where(hair[:, None], lin(0.17, 0.11, 0.07), c)
    return c


def mira_rule(x, h, z):
    ax = np.abs(x)
    c = np.tile(lin(0.86, 0.66, 0.55), (len(h), 1))                     # skin
    arm = (ax > 0.19) & (h > 0.62)
    dress = (~arm & (h >= 0.30) & (h < 0.79)) | (arm & (ax < 0.36) & (h > 0.70))
    c = np.where(dress[:, None], lin(0.12, 0.42, 0.45), c)
    c = np.where((~arm & (h >= 0.555) & (h < 0.57))[:, None], lin(0.85, 0.72, 0.42), c)                  # sash
    c = np.where((~arm & (h >= 0.06) & (h < 0.30))[:, None], lin(0.13, 0.12, 0.14), c)                   # tights
    c = np.where((h < 0.06)[:, None], lin(0.55, 0.12, 0.12), c)                                         # shoes
    hair = (~arm & (h > 0.962)) | (~arm & (h > 0.93) & (z < 0.03)) | (~arm & (h > 0.75) & (z < -0.045) & (ax < 0.12))
    c = np.where(hair[:, None], lin(0.42, 0.20, 0.10), c)
    return c


# ------------------------------------------------------------------------------------------------ builder data
def write_inc(path, starts, events):
    leo_p, leo_y = starts["leo"]
    mira_p, mira_y = starts["mira"]
    box = starts["box"]
    lan = starts["lantern"]
    lines = [
        "// make_film.py tarafından üretildi: oyuncuların ve nesnelerin başlangıç pozları (zaman çizelgesiyle aynı veri)",
        f"var FILM_LEO = {{x: {fmt(leo_p[0])}, y: 0, z: {fmt(leo_p[2])}, yaw: {fmt(leo_y)}}};",
        f"var FILM_MIRA = {{x: {fmt(mira_p[0])}, y: 0, z: {fmt(mira_p[2])}, yaw: {fmt(mira_y)}}};",
        f"var FILM_BOX = {{x: {fmt(box[0])}, y: {fmt(box[1])}, z: {fmt(box[2])}, yaw: {fmt(BENCH_YAW)}}};",
        f"var FILM_LANTERN = {{x: {fmt(lan[0])}, y: {fmt(lan[1])}, z: {fmt(lan[2])}}};",
        f"var FILM_DURATION = {fmt(DURATION)};",
    ]
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def main():
    tracks, events, starts = build()
    write_timeline(os.path.join(FILM, "TheLantern.cqtimeline"), tracks)
    write_inc(os.path.join(HERE, "Film.inc"), starts, events)
    paint_outfit("Mannequin_Male", leo_rule, os.path.join(ACTORS, "leo_outfit.png"))
    paint_outfit("Mannequin_Female", mira_rule, os.path.join(ACTORS, "mira_outfit.png"))
    if "--no-score" not in sys.argv:
        score(events, os.path.join(FILM, "Score.ogg"))
    print("shots:", ", ".join(f"{a:.1f}-{b:.1f}" for a, b in events["shots"]))
    print("events:", {k: (round(v, 2) if isinstance(v, float) else v) for k, v in events.items() if k != "shots"})


if __name__ == "__main__":
    main()

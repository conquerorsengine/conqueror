"""Ember Trail physics - a line-by-line mirror of the simulation in Assets/Scripts/EmberGame.cqs.

Both sides step at a fixed 1/60 s with the same operations in the same order on doubles and use no
transcendental functions (moving things follow polynomial ping-pong curves), so a recorded input sequence
replays identically in the engine. Tools/gen_levels.py uses this to search a route through every level.

Units: 1 tile = 1 world unit. Tile (i, j) covers x in [i, i+1], y in [j, j+1]; j grows upwards.
"""
import math

DT = 1.0 / 60.0
HW, HH = 0.3, 0.4            # oyuncu yarı boyutları
RUN_MAX = 8.0
ACC_GROUND, DEC_GROUND, ACC_AIR = 70.0, 80.0, 45.0
JUMP_V = 20.0
G_UP, G_DOWN, MAX_FALL = 55.0, 95.0, 20.0
COYOTE, BUFFER = 0.1, 0.12
DASH_SPEED, DASH_TIME, DASH_COOLDOWN = 18.0, 0.16, 0.35
SPRING_V = 28.0
STOMP_V, STOMP_V_HELD = 12.0, 18.0
CRUMBLE_DELAY, CRUMBLE_GONE = 0.45, 3.0
SLUG_SPEED, THORN_SPEED = 1.6, 2.2
PLAT_AMP_H, PLAT_AMP_V, PLAT_PERIOD = 3.0, 3.0, 4.0
MOTH_AMP_X, MOTH_AMP_Y, MOTH_PERIOD_X, MOTH_PERIOD_Y = 2.5, 0.6, 4.0, 1.3

SOLID = "#X"
ONEWAY = "="


def pingpong(t, period):
    """0..1..0 over one period, smoothstep eased (polynomial, deterministic)."""
    u = t / period
    u = u - math.floor(u)
    p = u * 2.0 if u < 0.5 else 2.0 - u * 2.0
    return p * p * (3.0 - 2.0 * p)


class Level:
    def __init__(self, rows):
        self.rows = rows
        self.h = len(rows)
        self.w = max(len(r) for r in rows)
        self.grid = [list(r.ljust(self.w, ".")) for r in rows]
        self.spawn = (1.5, 2.0)
        self.slugs, self.moths, self.springs, self.plats = [], [], [], []
        self.checkpoints, self.goal = [], None
        self.crumbles = []
        self.embers, self.shards = [], []
        for r, row in enumerate(self.grid):
            j = self.h - 1 - r
            for i, ch in enumerate(row):
                if ch == "P":
                    self.spawn = (i + 0.5, j + HH)
                elif ch in "SK":
                    self.slugs.append((i + 0.5, j + 0.25, ch))
                elif ch == "M":
                    self.moths.append((i + 0.5, j + 0.5))
                elif ch == "B":
                    self.springs.append((i, j))
                elif ch in "HV":
                    self.plats.append((i, j, ch))
                elif ch == "C":
                    self.checkpoints.append((i, j))
                elif ch == "G":
                    self.goal = (i, j)
                elif ch == "X":
                    self.crumbles.append((i, j))
                elif ch == "e":
                    self.embers.append((i, j))
                elif ch == "*":
                    self.shards.append((i, j))
                if ch not in "#X=^":
                    row[i] = "."
        self.checkpoints.sort()     # soldan sağa (sıra = ilerleme)
        self.crumble_index = {c: k for k, c in enumerate(self.crumbles)}

    def tile(self, i, j):
        if i < 0 or i >= self.w:
            return "#"          # dünya kenarı duvardır
        if j < 0 or j >= self.h:
            return "."
        return self.grid[self.h - 1 - j][i]


class Enemy:
    __slots__ = ("x", "y", "dir", "kind", "alive", "ax", "ay", "hw", "hh")


class World:
    """Everything except the player. Enemies and platforms are functions of the tick (plus kills)."""

    def __init__(self, level):
        self.lv = level
        self.enemies = []
        for (x, y, k) in level.slugs:
            e = Enemy()
            e.x, e.y, e.dir, e.kind, e.alive, e.ax, e.ay = x, y, -1.0, k, True, x, y
            e.hw, e.hh = 0.4, 0.25
            self.enemies.append(e)
        for (x, y) in level.moths:
            e = Enemy()
            e.x, e.y, e.dir, e.kind, e.alive, e.ax, e.ay = x, y, 1.0, "M", True, x, y
            e.hw, e.hh = 0.4, 0.35
            self.enemies.append(e)
        self.crumble = [0.0] * len(level.crumbles)    # >0: çökme sayacı; <0: yok olma süresi
        self.tick = 0

    def copy(self):
        w = World.__new__(World)
        w.lv = self.lv
        w.enemies = []
        for e in self.enemies:
            n = Enemy()
            n.x, n.y, n.dir, n.kind, n.alive, n.ax, n.ay, n.hw, n.hh = e.x, e.y, e.dir, e.kind, e.alive, e.ax, e.ay, e.hw, e.hh
            w.enemies.append(n)
        w.crumble = list(self.crumble)
        w.tick = self.tick
        return w

    def solid(self, i, j):
        t = self.lv.tile(i, j)
        if t == "X":
            k = self.lv.crumble_index.get((i, j))
            return k is None or self.crumble[k] >= 0.0
        return t == "#"

    def oneway(self, i, j):
        return self.lv.tile(i, j) == "="

    def plat_pos(self, k, tick):
        i, j, kind = self.lv.plats[k]
        p = pingpong(tick / 60.0, PLAT_PERIOD)
        if kind == "H":
            return i + 1.5 + PLAT_AMP_H * (p * 2.0 - 1.0), j + 1.0
        return i + 1.5, j + 1.0 + PLAT_AMP_V * p      # tepe noktası (üst yüzey)

    def step(self):
        """Advance enemies and crumble timers by one tick (before the player moves)."""
        self.tick += 1
        for e in self.enemies:
            if not e.alive:
                continue
            if e.kind == "M":
                t = self.tick / 60.0
                e.x = e.ax + MOTH_AMP_X * (pingpong(t, MOTH_PERIOD_X) * 2.0 - 1.0)
                e.y = e.ay + MOTH_AMP_Y * (pingpong(t, MOTH_PERIOD_Y) * 2.0 - 1.0)
                continue
            speed = SLUG_SPEED if e.kind == "S" else THORN_SPEED
            nx = e.x + e.dir * speed * DT
            front = nx + e.dir * e.hw
            fi = math.floor(front)
            fj = math.floor(e.y)
            below = math.floor(e.y - e.hh - 0.1)
            if self.solid(fi, fj) or not (self.solid(fi, below) or self.oneway(fi, below)):
                e.dir = -e.dir
            else:
                e.x = nx
        for k in range(len(self.crumble)):
            c = self.crumble[k]
            if c > 0.0:
                c -= DT
                self.crumble[k] = -CRUMBLE_GONE if c <= 0.0 else c
            elif c < 0.0:
                c += DT
                self.crumble[k] = 0.0 if c >= 0.0 else c


class Player:
    __slots__ = ("x", "y", "vx", "vy", "ground", "coyote", "buf", "dash", "cool", "dashdir", "dashok", "facing", "ride",
                 "jumpprev", "dead", "won", "cp")

    def __init__(self, x, y):
        self.x, self.y, self.vx, self.vy = x, y, 0.0, 0.0
        self.ground, self.coyote, self.buf = False, 0.0, 0.0
        self.dash, self.cool, self.dashdir, self.dashok, self.facing = 0.0, 0.0, 1.0, True, 1.0
        self.ride = -1
        self.jumpprev = False
        self.dead, self.won, self.cp = False, False, -1

    def copy(self):
        p = Player.__new__(Player)
        for k in Player.__slots__:
            setattr(p, k, getattr(self, k))
        return p


def overlap(ax, ay, ahw, ahh, bx, by, bhw, bhh):
    return abs(ax - bx) < ahw + bhw and abs(ay - by) < ahh + bhh


def step_player(p, w, move, jump, dash, events=None):
    """One fixed step. move -1/0/1, jump held (bool), dash pressed this tick (bool)."""
    lv = w.lv
    jump_pressed = jump and not p.jumpprev
    p.jumpprev = jump
    # Zamanlayıcılar
    p.buf = BUFFER if jump_pressed else p.buf - DT
    p.coyote = COYOTE if p.ground else p.coyote - DT
    if p.cool > 0.0:
        p.cool -= DT
    if dash and p.dashok and p.dash <= 0.0 and p.cool <= 0.0:
        p.dash = DASH_TIME
        p.dashok = False
        p.dashdir = move if move != 0 else p.facing
        p.vy = 0.0
        if events is not None:
            events.append("dash")
    if move != 0:
        p.facing = float(move)
    # Yatay
    if p.dash > 0.0:
        p.vx = p.dashdir * DASH_SPEED
        p.dash -= DT
        if p.dash <= 0.0:
            p.vx = p.dashdir * RUN_MAX
            p.cool = DASH_COOLDOWN
    else:
        target = move * RUN_MAX
        if p.ground:
            a = ACC_GROUND if move != 0 else DEC_GROUND
        else:
            a = ACC_AIR
        dv = a * DT
        if p.vx < target:
            p.vx = min(p.vx + dv, target)
        elif p.vx > target:
            p.vx = max(p.vx - dv, target)
    # Zıplama
    if p.buf > 0.0 and p.coyote > 0.0 and p.dash <= 0.0:
        p.vy = JUMP_V
        p.buf = 0.0
        p.coyote = 0.0
        p.ground = False
        p.ride = -1
        if events is not None:
            events.append("jump")
    # Yerçekimi
    if p.dash > 0.0:
        p.vy = 0.0
    else:
        g = G_UP if (p.vy > 0.0 and jump) else G_DOWN
        p.vy = max(p.vy - g * DT, -MAX_FALL)
    # Taşıyan platform
    if p.ride >= 0:
        ox, oy = w.plat_pos(p.ride, w.tick - 1)
        nx, ny = w.plat_pos(p.ride, w.tick)
        p.x += nx - ox
        p.y += ny - oy
    # X ekseni
    p.x += p.vx * DT
    j0 = math.floor(p.y - HH + 0.001)
    j1 = math.floor(p.y + HH - 0.001)
    if p.vx > 0.0:
        i = math.floor(p.x + HW)
        for j in range(j0, j1 + 1):
            if w.solid(i, j):
                p.x = i - HW - 0.0001
                p.vx = 0.0
                break
    elif p.vx < 0.0:
        i = math.floor(p.x - HW)
        for j in range(j0, j1 + 1):
            if w.solid(i, j):
                p.x = i + 1 + HW + 0.0001
                p.vx = 0.0
                break
    # Y ekseni
    prev_bottom = p.y - HH
    p.y += p.vy * DT
    i0 = math.floor(p.x - HW + 0.001)
    i1 = math.floor(p.x + HW - 0.001)
    was_ground = p.ground
    p.ground = False
    p.ride = -1
    if p.vy > 0.0:
        j = math.floor(p.y + HH)
        for i in range(i0, i1 + 1):
            if w.solid(i, j):
                p.y = j - HH - 0.0001
                p.vy = 0.0
                break
    else:
        bottom = p.y - HH
        j = math.floor(bottom)
        support = -1.0e9
        for i in range(i0, i1 + 1):
            if w.solid(i, j) or (w.oneway(i, j) and prev_bottom >= j + 1 - 0.001):
                support = max(support, j + 1.0)
        for k in range(len(lv.plats)):
            px, py = w.plat_pos(k, w.tick)
            if abs(p.x - px) < HW + 1.5 and prev_bottom >= py - 0.001 - max(0.0, py - w.plat_pos(k, w.tick - 1)[1]) and bottom <= py:
                if py > support:
                    support = py
                    p.ride = k
        if support > -1.0e8 and bottom <= support:
            p.y = support + HH
            p.vy = 0.0
            p.ground = True
            if p.ride < 0:
                # Yalnızca karo desteği: çöken bloklar
                for i in range(i0, i1 + 1):
                    k = lv.crumble_index.get((i, int(support) - 1))
                    if k is not None and w.crumble[k] == 0.0:
                        w.crumble[k] = CRUMBLE_DELAY
            if not was_ground and events is not None:
                events.append("land")
        else:
            p.ride = -1
    if p.ground:
        p.dashok = True
    # Yaylar
    for (si, sj) in lv.springs:
        if p.vy <= 0.0 and overlap(p.x, p.y, HW, HH, si + 0.5, sj + 0.25, 0.4, 0.25):
            p.vy = SPRING_V
            p.ground = False
            p.ride = -1
            p.dashok = True
            p.dash = 0.0
            if events is not None:
                events.append("spring")
    # Dikenler ve düşme
    ci0, ci1 = math.floor(p.x - HW), math.floor(p.x + HW)
    cj0, cj1 = math.floor(p.y - HH), math.floor(p.y + HH)
    for j in range(cj0, cj1 + 1):
        for i in range(ci0, ci1 + 1):
            if lv.tile(i, j) == "^" and overlap(p.x, p.y, HW, HH, i + 0.5, j + 0.25, 0.4, 0.25):
                p.dead = True
    if p.y < -2.0:
        p.dead = True
    # Düşmanlar
    for e in w.enemies:
        if not e.alive or not overlap(p.x, p.y, HW, HH, e.x, e.y, e.hw, e.hh):
            continue
        if p.dash > 0.0:
            e.alive = False
            if events is not None:
                events.append("kill")
        elif p.vy < 0.0 and p.y - HH > e.y and e.kind != "K":
            e.alive = False
            p.vy = STOMP_V_HELD if jump else STOMP_V
            p.dashok = True
            if events is not None:
                events.append("stomp")
        else:
            p.dead = True
    # Kontrol noktaları ve hedef
    for k, (ci, cj) in enumerate(lv.checkpoints):
        if k > p.cp and overlap(p.x, p.y, HW, HH, ci + 0.5, cj + 1.0, 0.5, 1.0):
            p.cp = k
            if events is not None:
                events.append("checkpoint")
    if lv.goal and overlap(p.x, p.y, HW, HH, lv.goal[0] + 1.0, lv.goal[1] + 1.5, 1.0, 1.5):
        p.won = True


def tick(p, w, move, jump, dash, events=None):
    w.step()
    step_player(p, w, move, jump, dash, events)

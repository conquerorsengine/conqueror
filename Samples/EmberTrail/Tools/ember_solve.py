"""Beam search for an input sequence that finishes a level (used to prove levels are beatable and to
record the autopilot / attract-mode demo). Inputs are chosen per chunk of CHUNK ticks."""
import collections
import math

import ember_sim as sim

CHUNK = 6
PER_CELL = 3
ACTIONS = [(m, j, d) for m in (1, 0, -1) for j in (1, 0) for d in (0, 1)]


def encode(actions):
    # Karakter: 'a' + (yön+1) + 3*zıplama + 6*atılma
    return "".join(chr(ord("a") + (m + 1) + 3 * j + 6 * d) for (m, j, d) in actions)


def decode(s):
    out = []
    for ch in s:
        v = ord(ch) - ord("a")
        out.append((v % 3 - 1, (v // 3) % 2, v // 6))
    return out


def flow_field(lv):
    """BFS distance (in tiles) from the goal through empty cells, 4-neighbourhood."""
    gx, gy = lv.goal
    dist = {}
    q = collections.deque()
    for dx in range(2):
        for dy in range(3):
            dist[(gx + dx, gy + dy)] = 0
            q.append((gx + dx, gy + dy))
    while q:
        c = q.popleft()
        for d in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (c[0] + d[0], c[1] + d[1])
            if n in dist or n[0] < 0 or n[0] >= lv.w or n[1] < 0 or n[1] >= lv.h:
                continue
            if lv.tile(*n) == "#":
                continue
            dist[n] = dist[c] + 1
            q.append(n)
    return dist


def run_chunk(p, w, act):
    m, j, d = act
    for t in range(CHUNK):
        sim.tick(p, w, m, bool(j), bool(d) and t == 0)
        if p.dead or p.won:
            return


def solve(lv, beam=260, max_chunks=1500, log=None):
    field = flow_field(lv)
    far = 10 ** 6

    def h(p):
        c = (math.floor(p.x), math.floor(p.y))
        best = field.get(c, far)
        for dy in (1, -1):
            best = min(best, field.get((c[0], c[1] + dy), far) + 1)
        return best

    p0 = sim.Player(*lv.spawn)
    w0 = sim.World(lv)
    frontier = [(h(p0), p0, w0, [])]
    seen = set()
    for depth in range(max_chunks):
        cand = []
        for score, p, w, acts in frontier:
            for act in ACTIONS:
                if act[2] and not (p.dashok and p.dash <= 0.0 and p.cool <= 0.0):
                    continue
                np_, nw = p.copy(), w.copy()
                run_chunk(np_, nw, act)
                if np_.dead:
                    continue
                nacts = acts + [act]
                if np_.won:
                    return nacts
                key = (round(np_.x * 6), round(np_.y * 6), round(np_.vx / 2), round(np_.vy / 3), np_.ground, np_.dashok,
                       np_.ride, np_.jumpprev, np_.coyote > 0.0, np_.dash > 0.0, np_.cool > 0.0, tuple(e.alive for e in nw.enemies), tuple(c > 0 for c in nw.crumble))
                if key in seen:
                    continue
                seen.add(key)
                # Puan: hedefe uzaklık; atılmayı ve geri gitmeyi az cezalandır
                s = h(np_) * 10 - np_.vx * 0.2 + (8 if act[2] else 0) + (1 if act[0] < 0 else 0)
                cand.append((s, np_, nw, nacts))
        if not cand:
            if log:
                log(f"  dead end at chunk {depth}")
            return None
        cand.sort(key=lambda c: c[0])
        # Çeşitlilik: hücre başına en çok PER_CELL durum (yoksa ışın tümüyle aynı çukura atlayanlarla dolar)
        frontier = []
        per = {}
        for c in cand:
            k = (math.floor(c[1].x), math.floor(c[1].y), c[1].ground)
            if per.get(k, 0) >= PER_CELL:
                continue
            per[k] = per.get(k, 0) + 1
            frontier.append(c)
            if len(frontier) >= beam:
                break
        if log and depth % 100 == 0:
            b = frontier[0][1]
            log(f"  chunk {depth}: best h={h(b)} pos=({b.x:.1f},{b.y:.1f}) states={len(cand)}")
    return None


def replay(lv, actions):
    """Re-run a solution and report what happened (for the summary and the medal times)."""
    p = sim.Player(*lv.spawn)
    w = sim.World(lv)
    ev = []
    ticks = 0
    for act in actions:
        for t in range(CHUNK):
            sim.tick(p, w, act[0], bool(act[1]), bool(act[2]) and t == 0, ev)
            ticks += 1
            if p.dead or p.won:
                return p, ticks, ev
    return p, ticks, ev

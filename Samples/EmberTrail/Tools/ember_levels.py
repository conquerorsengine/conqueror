"""Ember Trail level layouts, assembled from terrain pieces (left to right).

Legend of the generated maps (top row first):
  # ground   X crumbling block   = one-way plank   ^ spikes
  P start    C checkpoint lantern   G goal lantern (2 x 3)
  e ember    * sun shard (3 per level)
  S slug (stompable)   K thorn beetle (dash only)   M moth (stompable, flies)
  B spring   H / V moving platform (3 wide; horizontal / vertical)
"""

H = 22   # satır sayısı


class Builder:
    def __init__(self, seed):
        self.cols = []        # her sütun: alttan üste karakter listesi
        self.g = 4            # zemin yüksekliği (yürüme yüzeyi y = g)
        self.seed = seed
        self.shards = 0

    # ---- temel
    def col(self, g=None, ceiling=None):
        g = self.g if g is None else g
        c = ["#" if j < g else "." for j in range(H)]
        if ceiling is not None:
            for j in range(ceiling, H):
                c[j] = "#"
        self.cols.append(c)
        return len(self.cols) - 1

    def put(self, x, j, ch):
        if 0 <= j < H and 0 <= x < len(self.cols):
            self.cols[x][j] = ch

    def x(self):
        return len(self.cols)

    def embers(self, x0, n, j=None, step=1):
        for k in range(0, n, step):
            self.put(x0 + k, self.g + (1 if j is None else j), "e")

    def arc(self, x0, w, top):
        """Ember arc over a jump."""
        for k in range(w):
            u = (k + 0.5) / w
            self.put(x0 + k, self.g + 1 + int(round(top * 4 * u * (1 - u))), "e")

    def shard(self, x, j):
        self.put(x, j, "*")
        self.shards += 1

    # ---- parçalar
    def start(self):
        for _ in range(8):
            self.col()
        self.put(2, self.g, "P")

    def flat(self, n, embers=True):
        x0 = self.x()
        for _ in range(n):
            self.col()
        if embers and n >= 4:
            self.embers(x0 + 1, n - 2, step=2)

    def pit(self, w, arc=True):
        x0 = self.x()
        for _ in range(w):
            self.col(0)
        if arc:
            self.arc(x0 - 1, w + 2, 2.5)

    def step(self, dh, n=5):
        self.g += dh
        self.flat(n)

    def spikes(self, n):
        x0 = self.x()
        for _ in range(n):
            self.col()
        for k in range(n):
            self.put(x0 + k, self.g, "^")
        self.arc(x0 - 1, n + 2, 3)

    def planks(self, levels=2):
        """One-way plank stairs up to a higher ledge (+3 per level)."""
        x0 = self.x()
        for k in range(levels):
            for _ in range(4):
                self.col()
            self.put(x0 + k * 4 + 1, self.g + 3 * (k + 1) - 1, "=")
            self.put(x0 + k * 4 + 2, self.g + 3 * (k + 1) - 1, "=")
            self.put(x0 + k * 4 + 3, self.g + 3 * (k + 1) - 1, "=")
            self.put(x0 + k * 4 + 2, self.g + 3 * (k + 1), "e")
        self.g += 3 * levels
        for _ in range(4):
            self.col()

    def slug(self, n=8, kind="S"):
        x0 = self.x()
        self.flat(n, embers=False)
        self.put(x0 + n // 2, self.g, kind)
        self.embers(x0 + 1, n - 2, j=3, step=2)

    def moth_gap(self, w=4):
        x0 = self.x()
        self.pit(w, arc=False)
        self.put(x0 + w // 2, self.g + 2, "M")

    def spring_up(self, dh=6):
        x0 = self.x()
        self.flat(3, embers=False)
        self.put(x0 + 1, self.g, "B")
        for k in range(1, 7):
            self.put(x0 + 1, self.g + k, "e")
        self.g += dh
        self.flat(5)

    def hplat(self, w=10):
        x0 = self.x()
        self.pit(w, arc=False)
        self.put(x0 + w // 2 - 1, self.g - 1, "H")
        self.embers(x0 + 2, w - 4, j=2, step=2)

    def vshaft(self, dh=6):
        """Vertical platform next to a wall up to the next ledge."""
        x0 = self.x()
        for _ in range(4):
            self.col(0)
        self.put(x0, self.g - 1, "V")
        self.g += dh
        self.flat(5)

    def crumble_bridge(self, w=6):
        x0 = self.x()
        self.pit(w, arc=False)
        for k in range(w):
            self.put(x0 + k, self.g - 1, "X")
        self.embers(x0, w, step=2)

    def dash_gap(self, w=7):
        x0 = self.x()
        self.pit(w, arc=False)
        self.embers(x0 + 1, w - 2, j=2)

    def tunnel(self, n=8, thorn=False):
        x0 = self.x()
        for _ in range(n):
            self.col(ceiling=self.g + 2)
        self.embers(x0 + 1, n - 2, step=2)
        if thorn:
            self.put(x0 + n - 3, self.g, "K")

    def wall(self, dh):
        """Plain step up (dh <= 3 is a normal jump)."""
        self.g += dh
        self.flat(4)

    def checkpoint(self):
        x0 = self.x()
        self.flat(5, embers=False)
        self.put(x0 + 2, self.g, "C")

    def goal(self):
        x0 = self.x()
        self.flat(12, embers=False)
        self.put(x0 + 5, self.g, "G")
        for _ in range(3):
            self.col(H)        # sağ kenar duvarı

    def rows(self):
        return ["".join(self.cols[x][H - 1 - r] for x in range(len(self.cols))) for r in range(H)]


def build(design, seed):
    b = Builder(seed)
    b.start()
    for piece in design:
        name, *args = piece
        getattr(b, name)(*args)
    b.goal()
    return b


# Her seviye: (sahne, ad, dünya, tasarım, parça yerleri [(x ofseti, y)] için ayrı liste)
LEVELS = [
    ("Meadow1", "SUNNY START", "meadow", [
        ("flat", 6), ("pit", 2), ("flat", 6), ("wall", 2), ("flat", 4), ("pit", 3), ("flat", 5), ("slug", 9),
        ("checkpoint",), ("spikes", 3), ("flat", 4), ("planks", 1), ("flat", 3), ("pit", 4), ("step", -3, 6),
        ("slug", 10), ("moth_gap", 4), ("flat", 5), ("checkpoint",), ("spring_up", 6), ("pit", 3), ("step", -2, 4),
        ("slug", 8), ("pit", 4), ("step", -4, 6),
    ]),
    ("Meadow2", "WINDY HILLS", "meadow", [
        ("flat", 4), ("planks", 2), ("pit", 4), ("step", -3, 4), ("hplat", 10), ("flat", 4), ("slug", 9),
        ("checkpoint",), ("spikes", 4), ("flat", 3), ("dash_gap", 6), ("flat", 5), ("moth_gap", 5), ("flat", 4),
        ("checkpoint",), ("spring_up", 7), ("crumble_bridge", 6), ("flat", 4), ("slug", 8, "K"), ("pit", 4),
        ("step", -4, 6), ("hplat", 10), ("flat", 5),
    ]),
    ("Cave1", "GLOWCAP CAVERN", "cave", [
        ("flat", 4), ("tunnel", 10), ("pit", 3), ("flat", 4), ("slug", 9), ("checkpoint",), ("vshaft", 6),
        ("tunnel", 8, True), ("pit", 4), ("step", -3, 5), ("spikes", 4), ("flat", 4), ("checkpoint",),
        ("crumble_bridge", 7), ("flat", 4), ("moth_gap", 5), ("flat", 4), ("planks", 2), ("dash_gap", 7), ("step", -6, 6),
    ]),
    ("Cave2", "CRYSTAL DEPTHS", "cave", [
        ("flat", 4), ("spikes", 3), ("flat", 3), ("vshaft", 6), ("tunnel", 9, True), ("pit", 4), ("flat", 4),
        ("checkpoint",), ("hplat", 11), ("flat", 3), ("slug", 9, "K"), ("dash_gap", 7), ("flat", 4), ("checkpoint",),
        ("crumble_bridge", 8), ("flat", 3), ("spring_up", 7), ("moth_gap", 5), ("flat", 3), ("pit", 4), ("step", -5, 5),
        ("spikes", 4), ("flat", 4), ("checkpoint",), ("vshaft", 5), ("tunnel", 8, True), ("dash_gap", 6), ("step", -4, 5),
    ]),
    ("Sky1", "CLOUD RUINS", "sky", [
        ("flat", 4), ("hplat", 10), ("flat", 3), ("moth_gap", 5), ("flat", 4), ("spring_up", 6), ("dash_gap", 7),
        ("flat", 4), ("checkpoint",), ("crumble_bridge", 7), ("flat", 3), ("vshaft", 5), ("slug", 9), ("pit", 4),
        ("step", -4, 4), ("checkpoint",), ("hplat", 11), ("flat", 3), ("planks", 2), ("dash_gap", 7), ("step", -6, 5),
        ("spikes", 4), ("flat", 4),
    ]),
    ("Sky2", "LANTERN PEAK", "sky", [
        ("flat", 4), ("spikes", 4), ("flat", 3), ("hplat", 11), ("flat", 3), ("slug", 9, "K"), ("checkpoint",),
        ("vshaft", 6), ("moth_gap", 5), ("flat", 3), ("dash_gap", 7), ("flat", 3), ("crumble_bridge", 8), ("flat", 3),
        ("checkpoint",), ("spring_up", 7), ("moth_gap", 6), ("flat", 3), ("dash_gap", 7), ("step", -5, 5), ("hplat", 11),
        ("flat", 3), ("checkpoint",), ("tunnel", 9, True), ("spikes", 5), ("flat", 3), ("vshaft", 5), ("dash_gap", 7),
        ("step", -6, 5), ("slug", 9),
    ]),
]

WORLDS = {
    "meadow": dict(title="SUNNY MEADOW", tiles="tiles_meadow.png", far="bg_meadow_far.png", near="bg_meadow_near.png",
                   sky=(0.53, 0.78, 0.98), music="meadow.mp3", dark=False),
    "cave": dict(title="GLOWCAP CAVES", tiles="tiles_cave.png", far="bg_cave_far.png", near="bg_cave_near.png",
                 sky=(0.08, 0.07, 0.14), music="cave.mp3", dark=True),
    "sky": dict(title="CLOUD KINGDOM", tiles="tiles_sky.png", far="bg_sky_far.png", near="bg_sky_near.png",
                sky=(0.98, 0.78, 0.66), music="sky.mp3", dark=False),
}


def place_shards(b, rows):
    """Three sun shards: above flat stretches, high enough to need a full jump."""
    w = len(rows[0])
    cand = []
    for x in range(10, w - 16):
        # Yürünebilir zemin + üstünde 5 boş karo
        g = None
        for j in range(H - 1, -1, -1):
            if b.cols[x][j] == "#":
                g = j + 1
                break
        if g is None or g + 4 >= H:
            continue
        if all(b.cols[x][jj] == "." for jj in range(g, g + 5)) and all(b.cols[x + d][g - 1] == "#" for d in (-1, 1)):
            cand.append((x, g + 3))
    if not cand:
        return
    picks = [cand[int(len(cand) * f)] for f in (0.22, 0.55, 0.85)]
    for (x, j) in picks:
        b.shard(x, j)

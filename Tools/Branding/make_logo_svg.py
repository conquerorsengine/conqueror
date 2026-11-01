#!/usr/bin/env python3
"""Conqueror's Engine logo: the hand-drawn "CQ" emblem (traced with potrace) next to a typeset wordmark.

usage: make_logo_svg.py [font.otf]
Source: Tools/Branding/Conqueror_Logo_Source.png (the original artwork, white on transparent). Only its emblem is used;
the "Conqueror's Engine" wordmark is set in Montserrat ExtraBold (SIL Open Font License; Fedora package
julietaula-montserrat-fonts) with the font's own kerning, converted to outlines.
Writes Resources/Logo/:
  Conqueror_Logo.svg        lockup (emblem + wordmark), white, transparent background
  Conqueror_Logo_Dark.svg   the same in dark ink for light backgrounds
  Conqueror_Logo.png        lockup bitmap (hub sidebar, game splash)
  Conqueror_Emblem.svg      the emblem alone (square view box; make_icons.py builds the app icons from it)
Needs potrace (Fedora: sudo dnf install potrace), fontTools and pycairo.
"""
import os
import re
import subprocess
import sys
import tempfile

import cairo
import numpy as np
from fontTools.pens.recordingPen import RecordingPen
from fontTools.ttLib import TTFont
from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
LOGO = os.path.join(ROOT, "Resources", "Logo")
SRC = os.path.join(HERE, "Conqueror_Logo_Source.png")
FONT = "/usr/share/fonts/julietaula-montserrat-fonts/Montserrat-ExtraBold.otf"
WHITE = "#ffffff"
INK = "#16181d"
LINES = ["Conqueror\u2019s", "Engine"]      # typographic apostrophe


# ------------------------------------------------------------------------------------------------ emblem (potrace)
def trace(mask):
    """mask: bool array (True = ink). Returns (path data, potrace group transform)."""
    img = Image.fromarray((~mask * 255).astype(np.uint8))   # potrace: black = foreground
    with tempfile.TemporaryDirectory() as tmp:
        pbm = os.path.join(tmp, "in.pbm")
        svg = os.path.join(tmp, "out.svg")
        img.convert("1").save(pbm)
        subprocess.run(["potrace", pbm, "-s", "-o", svg, "--turdsize", "40", "--alphamax", "1.1", "--opttolerance", "0.25",
                        "--unit", "1", "--flat"], check=True)
        text = open(svg).read()
    g = re.search(r'<g transform="([^"]*)"', text).group(1)
    d = " ".join(re.findall(r' d="([^"]*)"', text))
    return d, g


# ------------------------------------------------------------------------------------------------ wordmark (font outlines)
def pair_kerning(font):
    """{(left glyph, right glyph): x advance adjustment} from the GPOS pair positioning lookups (formats 1 and 2)."""
    kern = {}
    if "GPOS" not in font:
        return kern
    gpos = font["GPOS"].table
    for lookup in gpos.LookupList.Lookup:
        for sub in lookup.SubTable:
            if lookup.LookupType == 9:
                sub = sub.ExtSubTable
            if getattr(sub, "LookupType", lookup.LookupType) != 2:
                continue
            coverage = sub.Coverage.glyphs
            if sub.Format == 1:
                for i, left in enumerate(coverage):
                    for rec in sub.PairSet[i].PairValueRecord:
                        v = rec.Value1
                        if v is not None and getattr(v, "XAdvance", 0):
                            kern.setdefault((left, rec.SecondGlyph), v.XAdvance)
            elif sub.Format == 2:
                c1 = sub.ClassDef1.classDefs
                c2 = sub.ClassDef2.classDefs
                for left in coverage:
                    row = sub.Class1Record[c1.get(left, 0)]
                    for right, k2 in c2.items():
                        v = row.Class2Record[k2].Value1
                        if v is not None and getattr(v, "XAdvance", 0):
                            kern.setdefault((left, right), v.XAdvance)
    return kern


def layout_line(font, glyphs, kern, text):
    """[(glyph name, x)] and the advance width in font units."""
    cmap = font.getBestCmap()
    hmtx = font["hmtx"]
    out, x, prev = [], 0.0, None
    for ch in text:
        name = cmap.get(ord(ch))
        if name is None:
            continue
        if prev is not None:
            x += kern.get((prev, name), 0)
        out.append((name, x))
        x += hmtx[name][0]
        prev = name
    return out, x


def glyph_ops(glyphs, name):
    pen = RecordingPen()
    glyphs[name].draw(pen)
    return pen.value


def ops_to_svg(ops, ox, oy, k):
    """Font outline (y up) -> SVG path data in the lockup space (y down), scaled by k and moved to (ox, oy)."""
    parts = []
    tx = lambda p: f"{ox + p[0] * k:.2f} {oy - p[1] * k:.2f}"
    for op, args in ops:
        if op == "moveTo":
            parts.append("M" + tx(args[0]))
        elif op == "lineTo":
            parts.append("L" + tx(args[0]))
        elif op == "curveTo":
            parts.append("C" + " ".join(tx(p) for p in args))
        elif op == "qCurveTo":
            parts.append("Q" + " ".join(tx(p) for p in args))
        elif op in ("closePath", "endPath"):
            parts.append("Z")
    return " ".join(parts)


def draw_ops(ctx, ops, ox, oy, k):
    for op, args in ops:
        pts = [(ox + p[0] * k, oy - p[1] * k) for p in args]
        if op == "moveTo":
            ctx.move_to(*pts[0])
        elif op == "lineTo":
            ctx.line_to(*pts[0])
        elif op == "curveTo":
            ctx.curve_to(*pts[0], *pts[1], *pts[2])
        elif op in ("closePath", "endPath"):
            ctx.close_path()


# ------------------------------------------------------------------------------------------------ main
def main():
    if subprocess.run(["which", "potrace"], capture_output=True).returncode != 0:
        sys.exit("potrace is not installed (Fedora: sudo dnf install potrace)")
    font_path = sys.argv[1] if len(sys.argv) > 1 else FONT
    if not os.path.exists(font_path):
        sys.exit(f"font not found: {font_path} (Fedora: sudo dnf install julietaula-montserrat-fonts)")

    # Emblem: the "CQ" at the left of the source artwork, up to the first wide empty column gap
    src = Image.open(SRC).convert("RGBA")
    a = np.array(src.getchannel("A"))
    cols = a.max(axis=0) > 20
    x0 = int(np.argmax(cols))
    x1, gap = x0, 0
    for x in range(x0, len(cols)):
        if cols[x]:
            x1, gap = x, 0
        else:
            gap += 1
            if gap > 25:
                break
    rows = a[:, x0:x1 + 1].max(axis=1) > 20
    y0 = int(np.argmax(rows))
    y1 = len(rows) - 1 - int(np.argmax(rows[::-1]))
    scale = 4
    alpha = src.getchannel("A").resize((src.width * scale, src.height * scale), Image.LANCZOS)
    alpha = alpha.filter(ImageFilter.GaussianBlur(scale * 0.6))
    mask = np.array(alpha) > 127
    mask[:, int((x1 + 12) * scale):] = False
    ed, eg = trace(mask)
    emblem_h = y1 - y0

    # Wordmark: two lines, cap tops / baselines tied to the emblem's height, left-aligned after a gap
    font = TTFont(font_path)
    glyphs = font.getGlyphSet()
    kern = pair_kerning(font)
    upm = font["head"].unitsPerEm
    cap = font["OS/2"].sCapHeight or 0.7 * upm
    cap_px = emblem_h * 0.315                      # cap height of each line
    k = cap_px / cap
    line_gap = cap_px * 0.52                       # baseline 1 -> cap top 2 (room for the q descender)
    block = cap_px * 2 + line_gap
    top = y0 + (emblem_h - block) / 2
    baselines = [top + cap_px, top + cap_px * 2 + line_gap]
    text_x = x1 + emblem_h * 0.2
    words = []
    width = 0.0
    for line, base in zip(LINES, baselines):
        placed, adv = layout_line(font, glyphs, kern, line)
        words.append((placed, base))
        width = max(width, adv * k)

    # Lockup bounds (source pixel units) with a margin
    descent = -font["OS/2"].sTypoDescender * k
    margin = emblem_h * 0.08
    bx0 = x0 - margin
    by0 = min(y0, top) - margin
    bx1 = text_x + width + margin
    by1 = max(y1, baselines[1] + descent * 0.9) + margin
    vw, vh = bx1 - bx0, by1 - by0

    text_d = " ".join(ops_to_svg(glyph_ops(glyphs, name), text_x + x * k, base, k)
                      for placed, base in words for name, x in placed)

    def write(path, fill):
        with open(path, "w") as f:
            f.write(f'<?xml version="1.0" encoding="UTF-8"?>\n'
                    f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{bx0:.1f} {by0:.1f} {vw:.1f} {vh:.1f}" '
                    f'width="{vw:.0f}" height="{vh:.0f}">\n'
                    f"  <title>Conqueror's Engine</title>\n"
                    f'  <g fill="{fill}" stroke="none">\n'
                    f'    <g transform="scale({1.0 / scale:.6f})"><g transform="{eg}"><path d="{ed}"/></g></g>\n'
                    f'    <path d="{text_d}"/>\n'
                    f'  </g>\n</svg>\n')

    write(os.path.join(LOGO, "Conqueror_Logo.svg"), WHITE)
    write(os.path.join(LOGO, "Conqueror_Logo_Dark.svg"), INK)

    # Emblem alone, square view box (same potrace output; the app icons are made from it)
    side = max(x1 - x0, y1 - y0) * 1.12
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    with open(os.path.join(LOGO, "Conqueror_Emblem.svg"), "w") as f:
        f.write(f'<?xml version="1.0" encoding="UTF-8"?>\n'
                f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{cx - side / 2:.1f} {cy - side / 2:.1f} {side:.1f} {side:.1f}" '
                f'width="{side:.0f}" height="{side:.0f}">\n'
                f"  <title>Conqueror's Engine emblem</title>\n"
                f'  <g transform="scale({1.0 / scale:.6f})">\n'
                f'    <g transform="{eg}" fill="{WHITE}" stroke="none">\n'
                f'      <path d="{ed}"/>\n'
                f'    </g>\n  </g>\n</svg>\n')

    # Bitmap lockup: 2x the source resolution so the hub and the splash stay crisp
    sharp = 2.0
    W, H = int(round(vw * sharp)), int(round(vh * sharp))
    surf = cairo.ImageSurface(cairo.FORMAT_ARGB32, W, H)
    ctx = cairo.Context(surf)
    ctx.scale(sharp, sharp)
    ctx.translate(-bx0, -by0)
    ctx.set_source_rgba(1, 1, 1, 1)
    ctx.save()
    ctx.scale(1.0 / scale, 1.0 / scale)
    m = re.search(r'translate\(([^,]+),([^)]+)\) scale\(([^,]+),([^)]+)\)', eg)
    tx, ty, sx, sy = (float(v) for v in m.groups())
    ctx.translate(tx, ty)
    ctx.scale(sx, sy)
    sys.path.insert(0, HERE)
    from svgpath import draw_path
    draw_path(ctx, ed)
    ctx.fill()
    ctx.restore()
    for placed, base in words:
        for name, x in placed:
            draw_ops(ctx, glyph_ops(glyphs, name), text_x + x * k, base, k)
    ctx.fill()
    surf.write_to_png(os.path.join(LOGO, "Conqueror_Logo.png"))
    print("emblem box:", (x0, y0, x1, y1), "lockup:", f"{vw:.0f}x{vh:.0f}", "png:", (W, H), "kerning pairs:", len(kern))


if __name__ == "__main__":
    main()

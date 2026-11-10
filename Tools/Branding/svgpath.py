"""Minimal renderer for the potrace SVGs in Resources/Logo (paths with M/m, L/l, C/c, Z/z and the potrace
translate + scale group transforms) using pycairo. Used to check the vector logo and to make icon PNGs."""
import re

import cairo

NUM = r"-?\d*\.?\d+(?:[eE][-+]?\d+)?"


def parse_svg(text):
    view = [float(v) for v in re.search(r'viewBox="([^"]+)"', text).group(1).split()]
    outer = float(re.search(r'<g transform="scale\(([^)]+)\)">', text).group(1))
    m = re.search(r'translate\(([^,]+),([^)]+)\) scale\(([^,]+),([^)]+)\)', text)
    tx, ty, sx, sy = (float(v) for v in m.groups())
    d = re.search(r' d="([^"]*)"', text, re.S).group(1)
    return view, outer, (tx, ty, sx, sy), d


def draw_path(ctx, d):
    tokens = re.findall(r"[MmLlCcZz]|" + NUM, d)
    i, cmd = 0, None
    x = y = 0.0
    sx = sy = 0.0
    while i < len(tokens):
        t = tokens[i]
        if re.match(r"[MmLlCcZz]", t):
            cmd = t
            i += 1
            if cmd in "Zz":
                ctx.close_path()
                x, y = sx, sy
            continue
        vals = []
        n = 6 if cmd in "Cc" else 2
        for _ in range(n):
            vals.append(float(tokens[i]))
            i += 1
        rel = cmd.islower()
        if cmd in "Mm":
            x, y = (x + vals[0], y + vals[1]) if rel else (vals[0], vals[1])
            ctx.move_to(x, y)
            sx, sy = x, y
            cmd = "l" if rel else "L"     # sonraki çiftler çizgi
        elif cmd in "Ll":
            x, y = (x + vals[0], y + vals[1]) if rel else (vals[0], vals[1])
            ctx.line_to(x, y)
        else:
            if rel:
                ctx.curve_to(x + vals[0], y + vals[1], x + vals[2], y + vals[3], x + vals[4], y + vals[5])
                x, y = x + vals[4], y + vals[5]
            else:
                ctx.curve_to(*vals)
                x, y = vals[4], vals[5]


def center_of_mass_offset(svg_path, resolution=512):
    """View-unit offset that puts the center of mass of the drawing on the center of the view (a bounding box center looks off for
    a lopsided shape: thin tips count as much as the heavy part)."""
    view, outer, (tx, ty, sx, sy), d = parse_svg(open(svg_path).read())
    vx, vy, vw, vh = view
    k = resolution / max(vw, vh)
    surf = cairo.ImageSurface(cairo.FORMAT_A8, resolution, resolution)
    ctx = cairo.Context(surf)
    ctx.translate((resolution - vw * k) / 2, (resolution - vh * k) / 2)
    ctx.scale(k, k)
    ctx.translate(-vx, -vy)
    ctx.scale(outer, outer)
    ctx.translate(tx, ty)
    ctx.scale(sx, sy)
    draw_path(ctx, d)
    ctx.set_fill_rule(cairo.FILL_RULE_WINDING)
    ctx.fill()
    surf.flush()
    data, stride = surf.get_data(), surf.get_stride()
    total = mx = my = 0.0
    for y in range(resolution):
        row = bytes(data[y * stride:y * stride + resolution])
        for x, a in enumerate(row):
            if a:
                total += a
                mx += a * (x + 0.5)
                my += a * (y + 0.5)
    return ((resolution / 2 - mx / total) / k, (resolution / 2 - my / total) / k)


def render(svg_path, out_png, size, fill=(1, 1, 1, 1), background=None, padding=0.0, corner=0.0, offset=(0.0, 0.0)):
    """Renders the SVG into a size x size (or the view aspect) PNG, centered with padding (fraction of size); offset moves it (view units)."""
    view, outer, (tx, ty, sx, sy), d = parse_svg(open(svg_path).read())
    vx, vy, vw, vh = view
    w = size if isinstance(size, int) else size[0]
    h = size if isinstance(size, int) else size[1]
    surf = cairo.ImageSurface(cairo.FORMAT_ARGB32, w, h)
    ctx = cairo.Context(surf)
    if background:
        ctx.set_source_rgba(*background)
        if corner > 0:
            r = corner * min(w, h)
            ctx.new_sub_path()
            ctx.arc(w - r, r, r, -1.5708, 0)
            ctx.arc(w - r, h - r, r, 0, 1.5708)
            ctx.arc(r, h - r, r, 1.5708, 3.14159)
            ctx.arc(r, r, r, 3.14159, 4.71239)
            ctx.close_path()
            ctx.fill()
        else:
            ctx.paint()
    pad = padding * min(w, h)
    k = min((w - 2 * pad) / vw, (h - 2 * pad) / vh)
    ctx.translate((w - vw * k) / 2, (h - vh * k) / 2)
    ctx.scale(k, k)
    ctx.translate(-vx + offset[0], -vy + offset[1])
    ctx.scale(outer, outer)
    ctx.translate(tx, ty)
    ctx.scale(sx, sy)
    draw_path(ctx, d)
    ctx.set_fill_rule(cairo.FILL_RULE_WINDING)
    ctx.set_source_rgba(*fill)
    ctx.fill()
    surf.write_to_png(out_png)

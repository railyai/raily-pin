#!/usr/bin/env python3
"""Sliced-preview images from Bambu Studio G-code (the CLI cannot export a preview image together with a slice).

    python bambu/gcode_preview.py plate_1.gcode out.png [--no-supports]

Draws every extrusion move as a round-capped line in its filament colour (from the G-code's filament_colour),
seen from the front-left above like Bambu Studio's Preview tab, far lines first. Support and support interface
are drawn light grey so the part colours stay readable. Purge into the chute (outside the plate) is skipped.
"""
import math
import re
import sys

import numpy as np
from PIL import Image, ImageDraw

MOVE = re.compile(r'^G([0123])\b(.*)')
ARG = re.compile(r'([XYZEIJ])(-?\d*\.?\d+)')
SUPPORT = ('Support', 'Support interface', 'Support transition')


def parse(path):
    colours, segs = [], []
    x = y = z = 0.0
    tool, feat = initial_tool(path), ''
    bed = None
    for line in open(path, errors='ignore'):
        if line.startswith(';'):
            if line.startswith('; FEATURE:'):
                feat = line.split(':', 1)[1].strip()
            elif line.startswith('; filament_colour ='):
                colours = [c.strip() for c in line.split('=', 1)[1].split(';')]
            elif line.startswith('; printable_area ='):
                pts = [tuple(float(v) for v in p.split('x')) for p in line.split('=', 1)[1].strip().split(',')]
                bed = (max(p[0] for p in pts), max(p[1] for p in pts))
            continue
        s = line.split(';', 1)[0].strip()
        if not s:
            continue
        if s[0] == 'T' and s[1:].isdigit():
            t = int(s[1:])
            if t < 16:
                tool = t
            continue
        m = MOVE.match(s)
        if not m:
            continue
        a = dict((k, float(v)) for k, v in ARG.findall(m.group(2)))
        nx, ny, nz = a.get('X', x), a.get('Y', y), a.get('Z', z)
        e = a.get('E', 0.0)
        if e > 0 and m.group(1) != '0' and (nx != x or ny != y) and feat not in ('Custom', ''):
            if m.group(1) in '23' and ('I' in a or 'J' in a):
                cx, cy = x + a.get('I', 0), y + a.get('J', 0)
                r = math.hypot(x - cx, y - cy)
                a0, a1 = math.atan2(y - cy, x - cx), math.atan2(ny - cy, nx - cx)
                cw = m.group(1) == '2'
                if cw and a1 >= a0:
                    a1 -= 2 * math.pi
                if not cw and a1 <= a0:
                    a1 += 2 * math.pi
                n = max(2, int(abs(a1 - a0) * r / 0.6))
                px, py = x, y
                for i in range(1, n + 1):
                    t = a0 + (a1 - a0) * i / n
                    qx, qy = cx + r * math.cos(t), cy + r * math.sin(t)
                    segs.append((px, py, qx, qy, nz, tool, feat in SUPPORT))
                    px, py = qx, qy
            else:
                segs.append((x, y, nx, ny, nz, tool, feat in SUPPORT))
        x, y, z = nx, ny, nz
    return colours, segs, bed


def initial_tool(path):
    """The filament the plate starts with: the only one used, or (two filaments) the one the first change
    leaves. The start G-code loads it without a plain T line."""
    used, first = None, None
    for line in open(path, errors='ignore'):
        if used is None and line.startswith('; filament: '):
            used = [int(v) - 1 for v in line.split(':', 1)[1].split(',') if v.strip()]
        s = line.strip()
        if s[:1] == 'T' and s[1:].isdigit() and int(s[1:]) < 16:
            first = int(s[1:])
            break
    used = used or [0]
    if len(used) == 1 or first is None:
        return used[0]
    others = [u for u in used if u != first]
    return others[0] if others else used[0]


def stats(path, density=1.37, diameter=1.75):
    """Grams per filament and per use, from the extrusion in the G-code: model, support, purge (the flush at each
    filament change plus the prime tower) and other (the start line and custom G-code)."""
    area = math.pi * (diameter / 2) ** 2
    tool, feat, flush = initial_tool(path), '', False
    e = {}
    for line in open(path, errors='ignore'):
        if line.startswith(';'):
            if line.startswith('; FEATURE:'):
                feat = line.split(':', 1)[1].strip()
            elif line.startswith('; FLUSH_START'):
                flush = True
            elif line.startswith('; FLUSH_END'):
                flush = False
            continue
        s = line.split(';', 1)[0].strip()
        if not s:
            continue
        if s[0] == 'T' and s[1:].isdigit():
            if int(s[1:]) < 16:
                tool = int(s[1:])
            continue
        if not (s.startswith('G1') or s.startswith('G2') or s.startswith('G3')):
            continue
        m = re.search(r'\bE(-?\d*\.?\d+)', s)
        if not m:
            continue
        if flush:
            use = 'purge'
        elif feat == 'Prime tower':
            use = 'purge'
        elif feat in SUPPORT:
            use = 'support'
        elif feat in ('Custom', ''):
            use = 'other'
        else:
            use = 'model'
        k = (tool, use)
        e[k] = e.get(k, 0.0) + float(m.group(1))
    g = {}
    for (t, use), v in e.items():
        g.setdefault(f'filament_{t + 1}', {})[use] = round(v * area * density / 1000, 2)
    return dict(grams=g)


def render(path, out, width=1600, supports=True):
    colours, segs, bed = parse(path)
    S = np.array([s[:5] for s in segs], dtype=float)
    tools = np.array([s[5] for s in segs]); sup = np.array([s[6] for s in segs])
    if bed:
        keep = (S[:, 0] >= 0) & (S[:, 0] <= bed[0]) & (S[:, 1] >= 0) & (S[:, 1] <= bed[1]) & (S[:, 2] >= 0)
        keep &= (S[:, 2] >= 0) & (S[:, 3] >= 0) & (S[:, 2] <= bed[0]) & (S[:, 3] <= bed[1])
        S, tools, sup = S[keep], tools[keep], sup[keep]
    if not supports:
        S, tools, sup = S[~sup], tools[~sup], sup[~sup]
    # camera: from the front-left, 35 degrees above
    az, el = math.radians(-35), math.radians(38)
    view = np.array([math.sin(az) * math.cos(el), -math.cos(az) * math.cos(el), math.sin(el)])  # toward the eye
    right = np.cross([0, 0, 1], view); right /= np.linalg.norm(right)
    up = np.cross(view, right)

    def proj(px, py, pz):
        P = np.stack([px, py, pz], axis=1)
        return P @ right, P @ up, P @ view
    ax, ay, ad = proj(S[:, 0], S[:, 1], S[:, 4])
    bx, by, bd = proj(S[:, 2], S[:, 3], S[:, 4])
    xs = np.concatenate([ax, bx]); ys = np.concatenate([ay, by])
    pad = 4.0
    x0, x1, y0, y1 = xs.min() - pad, xs.max() + pad, ys.min() - pad, ys.max() + pad
    sc = width / (x1 - x0)
    H = int((y1 - y0) * sc)
    ss = 2
    img = Image.new('RGB', (width * ss, H * ss), (236, 237, 240))
    dr = ImageDraw.Draw(img)
    order = np.argsort((ad + bd) / 2)
    lw = max(1, int(round(0.42 * sc * ss)))
    rgb = []
    for c in colours:
        c = c.lstrip('#')[:6]
        rgb.append(np.array([int(c[i:i + 2], 16) for i in (0, 2, 4)], dtype=float))
    zmax = max(S[:, 4].max(), 1.0)
    for i in order:
        base = rgb[tools[i]] if tools[i] < len(rgb) else np.array([128.0, 128, 128])
        if sup[i]:
            base = np.array([200.0, 202, 206])
        # light from above-left: segment direction decides how much the bead faces the light, plus height
        dx, dy = S[i, 2] - S[i, 0], S[i, 3] - S[i, 1]
        ln = math.hypot(dx, dy) or 1.0
        f = 0.80 + 0.12 * abs((dx * 0.6 - dy * 0.8) / ln) + 0.10 * S[i, 4] / zmax
        col = tuple(int(v) for v in np.clip(base * f, 0, 255))
        p0 = ((ax[i] - x0) * sc * ss, (y1 - ay[i]) * sc * ss)
        p1 = ((bx[i] - x0) * sc * ss, (y1 - by[i]) * sc * ss)
        dr.line([p0, p1], fill=col, width=lw)
    img = img.resize((width, H), Image.LANCZOS)
    img.save(out)
    return out


if __name__ == '__main__':
    render(sys.argv[1], sys.argv[2], supports='--no-supports' not in sys.argv)
    print(sys.argv[2])

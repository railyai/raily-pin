"""The six mascot shapes of the site, rasterised as 1-bit masks.

The numbers restate the SVG fallback in
frontend-visualization/src/components/agent/agent-mascot.tsx (64 x 64
viewBox); hardware/firmware/tests/check_oled_assets.py parses that file and
fails when they drift. The raster code is the one of
docs/pins/keyring-oled/render.py (the concept pack's bodies), moved here so
the firmware generator owns its data: pebble, drop and fold use the site's
paths; cube, lens and loop rebuild its rect, rotated rect and stroked
circle with the same numbers. Rasterised by Pillow without anti-aliasing.
"""
import math

from PIL import Image, ImageDraw

SHAPES = ('pebble', 'cube', 'lens', 'drop', 'loop', 'fold')   # the mascot byte's high nibble order (spec §10)
HOLLOW = frozenset({'loop', 'fold'})

# Site SVG, verbatim (check_oled_assets.py compares every number).
PATHS = {
    'pebble': 'M9 38C5 21 18 8 33 10S57 22 56 36S41 57 26 55S10 49 9 38Z',
    'drop': 'M36 7C43 17 54 34 49 46S19 59 14 44S25 10 36 7Z',
    'fold': 'M17 17C34 3 58 24 50 40S16 60 13 40S11 23 17 17Z',
}
RECTS = {  # x, y, width, height, rx, rotation in degrees about (32, 32)
    'cube': (9, 9, 46, 46, 14, 0),
    'lens': (13, 13, 38, 38, 11, 45),
}
LOOP = (32, 32, 18, 14)            # cx, cy, r, stroke width
FOLD_STROKE = 13
EYES_X = (27, 37)                  # the site's eye centres; y 31, or 47 on hollow shapes
EYES_Y, EYES_Y_HOLLOW = 31, 47


def _bez(p0, p1, p2, p3, n=24):
    out = []
    for i in range(1, n + 1):
        t = i / n
        a = (1 - t) ** 3
        b = 3 * (1 - t) ** 2 * t
        c = 3 * (1 - t) * t ** 2
        d = t ** 3
        out.append((a * p0[0] + b * p1[0] + c * p2[0] + d * p3[0],
                    a * p0[1] + b * p1[1] + c * p2[1] + d * p3[1]))
    return out


def svg_polygon(d):
    """Absolute M / C / S / Z paths only, as the site uses them; anything else raises."""
    toks = d.replace('M', ' M ').replace('C', ' C ').replace('S', ' S ').replace('Z', ' Z ').split()
    pts, cur, last_c2, cmd, nums = [], None, None, None, []

    def flush():
        nonlocal cur, last_c2
        if cmd not in ('M', 'Z') and cur is None:
            raise ValueError('path must start with M')
        if not all(math.isfinite(v) and -64 <= v <= 128 for v in nums):
            raise ValueError('coordinate out of range')
        arity = {'M': 2, 'C': 6, 'S': 4, 'Z': 0}[cmd]
        if (arity in (0, 2) and len(nums) != arity) or (arity > 2 and (not nums or len(nums) % arity)):
            raise ValueError('%s needs a multiple of %d numbers, got %d' % (cmd, arity, len(nums)))
        if cmd == 'Z':
            cur, last_c2 = None, None
        if cmd == 'M':
            cur = (nums[0], nums[1])
            pts.append(cur)
        elif cmd == 'C':
            for i in range(0, len(nums), 6):
                c1, c2, e = nums[i:i + 2], nums[i + 2:i + 4], nums[i + 4:i + 6]
                pts.extend(_bez(cur, c1, c2, e))
                cur, last_c2 = tuple(e), tuple(c2)
        elif cmd == 'S':
            if last_c2 is None:
                raise ValueError('S without a preceding C')
            for i in range(0, len(nums), 4):
                c1 = (2 * cur[0] - last_c2[0], 2 * cur[1] - last_c2[1])
                c2, e = nums[i:i + 2], nums[i + 2:i + 4]
                pts.extend(_bez(cur, c1, c2, e))
                cur, last_c2 = tuple(e), tuple(c2)

    for t in toks:
        if t in 'MCSZ':
            if cmd:
                flush()
            cmd, nums = t, []
        elif t.isalpha():
            raise ValueError('unsupported path command %r' % t)
        else:
            nums.append(float(t))
    if cmd:
        flush()
    return pts


def body_mask(shape, size, sx=1.0, sy=1.0):
    """1-bit filled body of a shape, `size` px square; sx/sy squash around the centre."""
    k = size / 64.0
    m = Image.new('1', (size, size), 0)
    d = ImageDraw.Draw(m)

    def tr(p):
        return ((p[0] - 32) * k * sx + size / 2, (p[1] - 32) * k * sy + size / 2)

    if shape == 'cube':
        x, y, w, h, rx, _ = RECTS['cube']
        d.rounded_rectangle([tr((x, y)), tr((x + w, y + h))], radius=rx * k, fill=1)
    elif shape == 'lens':                                   # rounded rect turned about the centre
        x, y, w, h, rx, rot = RECTS['lens']
        pts = []
        for cx0, cy0, a0 in ((x + w - rx, y + rx, -90), (x + w - rx, y + h - rx, 0),
                             (x + rx, y + h - rx, 90), (x + rx, y + rx, 180)):
            for i in range(9):
                a = math.radians(a0 + i * 90 / 8)
                pts.append((cx0 + rx * math.cos(a), cy0 + rx * math.sin(a)))
        c, r = 32.0, math.radians(rot)
        pts = [(c + (px - c) * math.cos(r) - (py - c) * math.sin(r),
                c + (px - c) * math.sin(r) + (py - c) * math.cos(r)) for px, py in pts]
        d.polygon([tr(p) for p in pts], fill=1)
    elif shape == 'loop':                                   # stroked circle: outer and inner edge
        _, _, r, stroke = LOOP
        o, i = (r + stroke / 2) * k, (r - stroke / 2) * k
        c = (size / 2, size / 2)
        d.ellipse([c[0] - o * sx, c[1] - o * sy, c[0] + o * sx, c[1] + o * sy], fill=1)
        d.ellipse([c[0] - i * sx, c[1] - i * sy, c[0] + i * sx, c[1] + i * sy], fill=0)
    elif shape == 'fold':                                   # stroke along the path: a round brush
        r = FOLD_STROKE * k / 2
        for x, y in (tr(p) for p in svg_polygon(PATHS['fold'])):
            d.ellipse([x - r, y - r, x + r, y + r], fill=1)
    elif shape in PATHS:
        d.polygon([tr(p) for p in svg_polygon(PATHS[shape])], fill=1)
    else:
        raise ValueError('unknown shape %r (%s)' % (shape, ', '.join(SHAPES)))
    return m

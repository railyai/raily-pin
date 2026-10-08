#!/usr/bin/env python3
"""Fit gate (lead, 2026-09-30, after #18.1d's dowels fell out of the face holes): measure every dowel / locator hole
of the face and the frame from the SLICED G-code, layer by layer, and compare with the last owner-approved print.

    python fit_gate.py <sliced .gcode.3mf> [--ref <approved .gcode.3mf>] [--allow <x,y>...] [--json out.json]

Per object, each layer is rasterised at 0.05 mm with every extrusion at its own LINE_WIDTH; closed voids of a dowel
hole's size (Ø 2.4-4.4) are the holes, tracked through the layers by their centre. Per hole:
  - crest: the smallest inscribed diameter over the layers (what a dowel must pass: rib crests or a plain bore);
  - bore: the area-equivalent diameter at the same layers; ribs present when bore - crest > 0.08;
  - grip: the height (mm) over which the inscribed diameter is <= the dowel (3.0) + 0.05, i.e. where it grips;
  - blind holes: the floor under the deepest layer of the void, its thickness and whether it is solid (a filled
    layer, no sparse infill) for its first 1.0 mm.
With --ref, each object is matched to the reference object of its kind (face: has ribbed holes) and its holes to the
reference's (the 8 symmetries of the box are tried: the plate may turn an object); each matched hole FAILs when its
crest grows by more than 0.03 (looser), its grip gets shorter by more than 0.1 or its stop thinner, unless it is
declared intentional with --allow x,y. Absolute rules: a frame hole needs a solid stop >= 1.2 (a floor, or the ring
round a neck); the plate's dowels may carry no D-flat (> 0.1) when the face holes are 3-rib holes (#18.1d: the flat,
turned onto a rib, takes it out); --face-crest-max caps the ribbed crests (the coupon step the owner approved). Face
locators (plain holes in the face) are reported, not gated. Exit 0 = pass.
"""
import argparse
import collections
import json
import math
import re
import sys

import numpy as np
from scipy import ndimage

import gcode_gate as GG

PIX = 0.05
DOWEL = 3.0


def layers_with_width(lines):
    """{obj: {z: [(a, b, width, sparse)]}}"""
    out = collections.defaultdict(collections.OrderedDict)
    z = obj = x = y = None
    feat, w = '', 0.42
    for line in lines:
        if line.startswith('; FEATURE:'):
            feat = line.split(':', 1)[1].strip(); continue
        if line.startswith('; LINE_WIDTH:'):
            w = float(line.split(':')[1]); continue
        if line.startswith('; Z_HEIGHT:'):
            z = round(float(line.split(':')[1]), 3); continue
        m = re.match(r'; start printing object, unique label id: (\d+)', line)
        if m:
            obj = m.group(1); continue
        if line.startswith('; stop printing object'):
            obj = None; continue
        if not line.startswith(('G0', 'G1', 'G2', 'G3')):
            continue
        cmd = line.split(';')[0].split()
        if not cmd:
            continue
        d = {c[0]: float(c[1:]) for c in cmd[1:] if c[0] in 'XYZEIJ' and len(c) > 1}
        nx, ny = d.get('X', x), d.get('Y', y)
        if d.get('E', 0) > 0 and obj is not None and x is not None and z is not None:
            if cmd[0] in ('G2', 'G3') and 'I' in d:
                cx, cy = x + d['I'], y + d.get('J', 0)
                r = math.hypot(x - cx, y - cy)
                a0, a1 = math.atan2(y - cy, x - cx), math.atan2(ny - cy, nx - cx)
                if cmd[0] == 'G2' and a1 >= a0: a1 -= 2 * math.pi
                if cmd[0] == 'G3' and a1 <= a0: a1 += 2 * math.pi
                n = max(2, int(abs(a1 - a0) * r / 0.2))
                pts = [(cx + r * math.cos(a0 + (a1 - a0) * t / n), cy + r * math.sin(a0 + (a1 - a0) * t / n)) for t in range(n + 1)]
                segs = list(zip(pts[:-1], pts[1:]))
            else:
                segs = [((x, y), (nx, ny))]
            out[obj].setdefault(z, []).extend((a, b, w, feat == 'Sparse infill', feat) for a, b in segs)
        x, y = nx, ny
    return out


def raster(segs, box):
    """filled mask, sparse mask (True where extruded)"""
    x0, y0, x1, y1 = box
    W, H = int((x1 - x0) / PIX) + 1, int((y1 - y0) / PIX) + 1
    masks = []
    for want_sparse in (None, True):
        byw = collections.defaultdict(lambda: np.zeros((W, H), bool))
        for (ax, ay), (bx, by), w, sp, *_ in segs:
            if want_sparse and not sp:
                continue
            L = math.hypot(bx - ax, by - ay)
            n = max(1, int(L / (PIX / 2)))
            t = np.linspace(0, 1, n + 1)
            i = ((ax + (bx - ax) * t - x0) / PIX).astype(int); j = ((ay + (by - ay) * t - y0) / PIX).astype(int)
            ok = (i >= 0) & (i < W) & (j >= 0) & (j < H)
            byw[round(w, 2)][i[ok], j[ok]] = True
        m = np.zeros((W, H), bool)
        for w, c in byw.items():
            m |= ndimage.distance_transform_edt(~c) * PIX <= w / 2
        masks.append(m)
    return masks


def holes_in(layers, box, dmin=2.4, dmax=4.4):
    """{z: [(cx, cy, crest, bore)]}"""
    res = {}
    for z, segs in layers.items():
        filled, _ = raster(segs, box)
        lab, n = ndimage.label(~filled)
        if n == 0:
            continue
        border = set(np.unique(np.concatenate([lab[0], lab[-1], lab[:, 0], lab[:, -1]])))
        sizes = ndimage.sum(np.ones_like(lab), lab, index=range(1, n + 1))
        found = []
        for k in range(1, n + 1):
            if k in border:
                continue
            area = sizes[k - 1] * PIX * PIX
            deq = math.sqrt(4 * area / math.pi)
            if not (dmin <= deq <= dmax):
                continue
            reg = lab == k
            sl = ndimage.find_objects(reg.astype(int))[0]
            sub = reg[sl[0].start - 2:sl[0].stop + 2, sl[1].start - 2:sl[1].stop + 2]
            e = ndimage.distance_transform_edt(sub)
            crest = 2 * e.max() * PIX
            ci, cj = ndimage.center_of_mass(reg)
            found.append((box[0] + ci * PIX, box[1] + cj * PIX, round(crest, 3), round(deq, 3)))
        res[z] = found
    return res


def circfit(P):
    A = np.c_[2 * P[:, 0], 2 * P[:, 1], np.ones(len(P))]
    c = np.linalg.lstsq(A, (P ** 2).sum(1), rcond=None)[0]
    return c[0], c[1]


def wall_profile(layers, z, cx, cy, rmax=2.3):
    """(crest Ø, bore Ø) of a hole at one layer from its wall toolpath (the hole's wall is an outer wall): the wall's
    inner edge = distance from the fitted centre minus half its line width. crest = the smallest, bore = the 90th pct."""
    P = np.array([(q[0], q[1], w) for (a, b, w, sp, feat) in layers[z] if feat == 'Outer wall'
                  for q in (a, b) if 0.8 < math.hypot(q[0] - cx, q[1] - cy) < rmax])
    if len(P) < 12:
        return None
    x, y = circfit(P[:, :2])
    inner = np.hypot(P[:, 0] - x, P[:, 1] - y) - P[:, 2] / 2
    return round(2 * inner.min(), 3), round(2 * np.percentile(inner, 90), 3), (x, y)


def stop_of(layers, cx, cy, z_end, direction):
    """The material a dowel lands on past the hole's closed end: thickness of the solid annulus r 0.9-1.45 (a floor or
    the ring round a neck) and whether its first 1.0 mm (or all of it, if thinner) is solid, not sparse."""
    zs = sorted(layers)
    beyond = [z for z in zs if (z > z_end if direction > 0 else z < z_end)]
    beyond = beyond if direction > 0 else beyond[::-1]
    t, solid, prev = 0.0, True, z_end
    bb = (cx - 2.0, cy - 2.0, cx + 2.0, cy + 2.0)
    for z in beyond:
        segs = [s_ for s_ in layers[z] if min(s_[0][0], s_[1][0]) < cx + 2.5 and max(s_[0][0], s_[1][0]) > cx - 2.5
                and min(s_[0][1], s_[1][1]) < cy + 2.5 and max(s_[0][1], s_[1][1]) > cy - 2.5]
        filled, sparse = raster(segs, bb)
        I, J = np.meshgrid(np.arange(filled.shape[0]), np.arange(filled.shape[1]), indexing='ij')
        rr = np.hypot(bb[0] + I * PIX - cx, bb[1] + J * PIX - cy)
        ring = (rr >= 0.9) & (rr <= 1.45)
        cov = filled[ring].mean()
        if cov < 0.6:
            break
        if t < 1.0 and (sparse[ring].mean() > 0.2 or cov < 0.95):
            solid = False
        t += abs(z - prev); prev = z
    return round(t, 2), solid


def dowels(L):
    """The printed dowels (an object ~5 mm tall made of small closed outer-wall loops): (median outer Ø, D-flat depth,
    count) from the loops at mid height, or None. The flat is Ø/2 minus the loop's smallest radius (a round dowel: ~0)."""
    best = None
    for obj, layers in L.items():
        zs = sorted(layers)
        if abs(zs[-1] - 5.0) > 0.3 or len(zs) > 40:
            continue
        z = zs[len(zs) // 2]
        loops, cur = [], []
        for a, b, w, sp, feat in layers[z]:
            if feat != 'Outer wall':
                continue
            if cur and math.hypot(a[0] - cur[-1][0], a[1] - cur[-1][1]) > 1e-3:
                loops.append(cur); cur = []
            if not cur:
                cur = [a]
            cur.append(b)
        loops.append(cur)
        ds, flats = [], []
        for lp in loops:
            P = np.array(lp)
            if len(P) < 8 or not (2.0 < np.ptp(P, axis=0).max() < 3.5 and np.ptp(P, axis=0).min() > 2.0):
                continue
            Q = np.vstack([P[i] + (P[i + 1] - P[i]) * t for i in range(len(P) - 1) for t in np.linspace(0, 1, 20)])
            x, y = circfit(P)
            r = np.hypot(Q[:, 0] - x, Q[:, 1] - y) + 0.21          # the loop's outer edge (0.42 lines)
            ds.append(2 * np.percentile(r, 90)); flats.append(np.percentile(r, 90) - r.min())
        if len(ds) >= 3 and (best is None or len(ds) > best[2]):
            best = (round(float(np.median(ds)), 3), round(float(np.median(flats)), 2), len(ds))
    return best


def measure(path):
    lines, names = GG.read_gcode(path)
    L = layers_with_width(lines)
    out = {'_dowel': dowels(L)}
    for obj, layers in L.items():
        pts = np.array([p for z in layers for s in layers[z] for p in s[:2]])
        box = (pts[:, 0].min() - 1, pts[:, 1].min() - 1, pts[:, 0].max() + 1, pts[:, 1].max() + 1)
        if (box[2] - box[0]) * (box[3] - box[1]) < 150 or max(layers) > 20:   # dowels / the pusher: nothing to gate
            continue
        per_z = holes_in(layers, box)
        tracks = []
        zs = sorted(layers)
        for z in sorted(per_z):
            for cx, cy, crest, bore in per_z[z]:
                if crest < 0.85 * bore:          # not a round hole (infill gaps, letters, pockets)
                    continue
                for tr in tracks:                # a track continues only on the next layer
                    if math.hypot(tr['cx'] - cx, tr['cy'] - cy) < 0.6 and zs.index(tr['zs'][-1]) == zs.index(z) - 1:
                        tr['zs'].append(z); break
                else:
                    tracks.append(dict(cx=cx, cy=cy, zs=[z]))
        holes = []
        for tr in tracks:
            if len(tr['zs']) < 4:
                continue
            prof = [(z, wall_profile(layers, z, tr['cx'], tr['cy'])) for z in tr['zs']]
            prof = [(z, w) for z, w in prof if w]
            if len(prof) < 3:
                continue
            cx = float(np.median([w[2][0] for _, w in prof])); cy = float(np.median([w[2][1] for _, w in prof]))
            z0, z1 = tr['zs'][0], tr['zs'][-1]
            dz = lambda z: z - zs[zs.index(z) - 1] if zs.index(z) else z
            body = prof[1:-1] if len(prof) > 4 else prof
            crest = min(w[0] for _, w in body)
            bore = float(np.median([w[1] for _, w in body]))
            ribs = bore - crest > 0.15 and bore < 3.65   # 3 ribs: ~0.25; plain / locator: < 0.1; the slot locator: bore 3.84
            lim = crest + 0.03 if ribs else DOWEL + 0.25      # ribbed: at the crest; plain: a bore within 0.25 of the dowel
            first = next((z for z, w in prof if w[0] <= lim), None)                   # the grip starts here
            grip = round(z1 - first + dz(first), 2) if first is not None else 0.0
            open_bottom = z0 <= zs[0] + 1e-6
            stop = stop_of(layers, cx, cy, z1 if open_bottom else z0, +1 if open_bottom else -1)
            if not (3.0 <= bore <= 3.9 and z1 - z0 + dz(z0) >= 1.2):
                continue
            holes.append(dict(cx=round(cx - box[0], 2), cy=round(cy - box[1], 2), z=(z0, z1),
                              depth=round(z1 - z0 + dz(z0), 2), crest=crest, bore=round(bore, 3), ribs=bool(ribs),
                              grip=grip, stop=stop[0], stop_solid=stop[1]))
        holes.sort(key=lambda h: (h['cx'], h['cy']))
        if holes:
            out[names.get(obj, obj)] = dict(id=obj, box=box, holes=holes, face=any(h['ribs'] for h in holes))
    return out


FLAT_MAX = 0.1        # a dowel D-flat deeper than this, in 3-rib holes, can take a rib out (#18.1d)
STOP_MIN = 1.2        # a frame hole's stop (a floor, or the ring round a neck), solid
CREST_TOL = 0.03      # a face crest may grow this much over the reference's
GRIP_TOL = 0.1        # a hole's grip may shrink this much under the reference's


def _align(cur, ref):
    """The holes of `cur` mapped into `ref`'s object frame: the plate may turn or mirror an object, so try the 8
    symmetries of the box and keep the one whose holes land closest to ref's. Returns [(hole, ref hole | None, dist)]."""
    cw, ch = cur['box'][2] - cur['box'][0], cur['box'][3] - cur['box'][1]
    rw, rh = ref['box'][2] - ref['box'][0], ref['box'][3] - ref['box'][1]
    best = None
    for swap in (False, True):
        for fx in (False, True):
            for fy in (False, True):
                pts = []
                for h in cur['holes']:
                    x, y, w, hh = h['cx'], h['cy'], cw, ch
                    if swap:
                        x, y, w, hh = y, x, hh, w
                    x, y = (w - x if fx else x), (hh - y if fy else y)
                    x, y = x + (rw - w) / 2, y + (rh - hh) / 2
                    pts.append((h, x, y))
                pairs = []
                for h, x, y in pts:
                    g = min(ref['holes'], key=lambda q: math.hypot(q['cx'] - x, q['cy'] - y), default=None)
                    dist = math.hypot(g['cx'] - x, g['cy'] - y) if g else 99.0
                    pairs.append((h, g, dist))
                score = sum(min(d_, 3.0) for _, _, d_ in pairs)
                if best is None or score < best[0]:
                    best = (score, pairs)
    return best[1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('path'); ap.add_argument('--ref', help='the last owner-approved sliced plate (18.1c for the face)')
    ap.add_argument('--allow', action='append', default=[],
                    help='x,y (object-relative, as printed in this report) of a hole whose change is declared intentional')
    ap.add_argument('--face-crest-max', type=float, help='absolute limit for ribbed face crests (the approved coupon step)')
    ap.add_argument('--json')
    a = ap.parse_args()
    cur = measure(a.path)
    ref = measure(a.ref) if a.ref else None
    allow = [tuple(float(v) for v in s_.split(',')) for s_ in a.allow]
    dw = cur.pop('_dowel')
    if ref:
        ref.pop('_dowel')
    ok = True
    ribbed = any(h['ribs'] for o in cur.values() for h in o['holes'])
    if dw:
        line = f'printed dowels: {dw[2]}, Ø{dw[0]:.3f}, D-flat {dw[1]:.2f}'
        if ribbed and dw[1] > FLAT_MAX:
            line += (f'  FAIL: a {dw[1]:.2f} flat in 3-rib face holes: turned onto a rib it takes that rib out '
                     f'(the fit depends on how each dowel is turned)')
            ok = False
        print(line)
    else:
        print('printed dowels: none on this plate')
    dd = dw[0] if dw else 3.0
    for name, o in cur.items():
        kind = 'face' if o['face'] else 'frame'
        print(f'object {name} ({kind}): {len(o["holes"])} dowel holes (x, y relative to the object, as printed)')
        pairs = None
        if ref:
            same = [v for v in ref.values() if v['face'] == o['face']]
            if same:
                pairs = {id(h): (g, d_) for h, g, d_ in _align(o, same[0])}
        for h in o['holes']:
            role = 'ribbed' if h['ribs'] else ('locator' if kind == 'face' else 'plain')
            s_ = (f'  ({h["cx"]:6.1f},{h["cy"]:6.1f}) {role:7s} depth {h["depth"]:.2f} crest Ø{h["crest"]:.3f} '
                  f'bore Ø{h["bore"]:.3f} grip {h["grip"]:.2f} '
                  + (f'cap {h["stop"]:.2f}' if kind == 'face' else f'stop {h["stop"]:.2f} {"solid" if h["stop_solid"] else "NOT SOLID"}'))
            bad = []
            if kind == 'frame' and (h['stop'] < STOP_MIN - 1e-6 or not h['stop_solid']):
                bad.append(f'stop {h["stop"]:.2f} < {STOP_MIN}' if h['stop'] < STOP_MIN - 1e-6 else 'stop not solid')
            if kind == 'face' and h['ribs']:
                s_ += f' (crest {h["crest"] - dd:+.3f} over the dowel)'
                if a.face_crest_max and h['crest'] > a.face_crest_max + 1e-6:
                    bad.append(f'crest over the approved {a.face_crest_max:.3f}')
            if pairs is not None:
                g, d_ = pairs.get(id(h), (None, 99.0))
                if g is None or d_ > 1.5:
                    s_ += ' | new hole (absolute rules only)'
                else:
                    s_ += f' | ref crest Ø{g["crest"]:.3f} grip {g["grip"]:.2f}' + (f' stop {g["stop"]:.2f}' if kind == 'frame' else '')
                    if h['ribs'] or kind == 'frame':
                        if h['crest'] > g['crest'] + CREST_TOL:
                            bad.append(f'looser: crest +{h["crest"] - g["crest"]:.3f} vs ref')
                        if h['grip'] < g['grip'] - GRIP_TOL:
                            bad.append(f'grip {h["grip"] - g["grip"]:+.2f} vs ref')
                    if kind == 'frame' and h['stop'] < g['stop'] - 0.05:
                        bad.append(f'thinner stop {h["stop"]:.2f} vs ref {g["stop"]:.2f}')
            declared = any(math.hypot(h['cx'] - ax, h['cy'] - ay) < 1.5 for ax, ay in allow)
            if bad and not declared:
                s_ += '  FAIL: ' + ', '.join(bad); ok = False
            elif bad:
                s_ += '  (declared: ' + ', '.join(bad) + ')'
            print(s_)
    if a.json:
        json.dump(cur, open(a.json, 'w'), indent=1, default=float)
    print('FIT PASS' if ok else 'FIT FAIL')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()

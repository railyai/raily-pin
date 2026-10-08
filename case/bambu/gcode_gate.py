#!/usr/bin/env python3
"""Layer-by-layer G-code gate (lead, 2026-09-29, after the #20 and #20b frames printed as spaghetti while Bambu Studio
reported no floating region): rasterise every object's extrusion per layer and find what the slicer's check misses.

    python gcode_gate.py <sliced .gcode.3mf | plate_N.gcode> [--strict] [--pix 0.2] [--span 4.5]

Per object:
  - first-layer contact area and pieces;
  - islands: extrusion in a layer that touches nothing in the layer below (starts in the air);
  - unsupported patches: extrusion more than 0.4 mm (2 cells) outside the layer below, > 2 mm2 (the region of the
    layer below's sparse infill counts as support: its lines move between layers);
  - for each patch, the material stacked on it: how high the column grown from that patch keeps going.
A patch with more than 1 mm stacked on it is FLAGGED. A flagged
patch whose span (twice its largest inscribed radius, holes it surrounds filled in) is <= --span (4.5 mm) is a short
bridge (the #17/#18 frames' nut-slot roofs: 12.7 mm2, span 4.0, printed clean); a wider one FAILS (the #20 / #20b
caps: the pillar's top across the Ø7.4 collar bore, span ~6, 7 mm of pillar on it, spaghetti twice). --strict fails on any flagged patch. Islands always fail.
Exit code 0 = pass, 1 = fail. Every delivery report quotes this output for every plate.
"""
import collections
import math
import re
import sys
import zipfile

import numpy as np
from scipy import ndimage

STACK_MAX = 1.0      # mm of material allowed on an unsupported patch before it is flagged
PATCH_MIN = 2.0      # mm2: smaller overhangs are the normal perimeter step-out
SUPPORT_REACH = 2    # cells (0.4 mm at 0.2): extrusion this close to the layer below counts as supported


def read_gcode(path):
    """(gcode lines, {label id: object name})"""
    if path.endswith('.3mf'):
        z = zipfile.ZipFile(path)
        name = next(n for n in z.namelist() if re.match(r'Metadata/plate_\d+\.gcode$', n))
        lines = z.read(name).decode(errors='replace').splitlines()
        names = {}
        try:
            si = z.read('Metadata/slice_info.config').decode()
            names = dict(re.findall(r'<object identify_id="(\d+)" name="([^"]+)"', si))
        except KeyError:
            pass
        return lines, names
    return open(path).read().splitlines(), {}


NON_PART = ('Custom', 'Prime tower', 'Wipe tower', '')


def layers_by_object(lines):
    """{object label: {z: segments}}. A plate sliced without object labels (the shelf jobs) is one object, 'plate'
    (2026-09-30: those plates had passed with no object analysed at all), without the purge line / prime tower."""
    out = collections.defaultdict(collections.OrderedDict)
    z = obj = x = y = None
    feat = ''
    labelled = any(l.startswith('; start printing object') for l in lines)
    for line in lines:
        if not labelled:
            obj = None if (z is None or feat in NON_PART) else ('support' if feat.startswith('Support') else 'plate')
        if line.startswith('; FEATURE:'):
            feat = line.split(':', 1)[1].strip()
            continue
        if line.startswith('; Z_HEIGHT:'):
            z = round(float(line.split(':')[1]), 3)
            continue
        m = re.match(r'; start printing object, unique label id: (\d+)', line)
        if m:
            obj = m.group(1)
            continue
        if line.startswith('; stop printing object'):
            obj = None
            continue
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
                if cmd[0] == 'G2' and a1 >= a0:
                    a1 -= 2 * math.pi
                if cmd[0] == 'G3' and a1 <= a0:
                    a1 += 2 * math.pi
                n = max(2, int(abs(a1 - a0) * r / 0.3))
                pts = [(cx + r * math.cos(a0 + (a1 - a0) * t / n), cy + r * math.sin(a0 + (a1 - a0) * t / n)) for t in range(n + 1)]
                segs = list(zip(pts[:-1], pts[1:]))
            else:
                segs = [((x, y), (nx, ny))]
            key = obj if (not labelled or not feat.startswith('Support')) else f'{obj}/support'
            out[key].setdefault(z, []).extend((a, b, feat == 'Sparse infill') for a, b in segs)
        x, y = nx, ny
    return out


def analyse(layers, pix=0.2, span_ok=4.5, support=None, gap=0.45):
    """support: {z: segments} of the plate's painted / slicer support (an unlabelled plate): any support extrusion
    within `gap` under a layer (the top z distance) counts as holding it; support itself is not gated."""
    zs = list(layers)
    pts = np.array([p for z in zs for s in layers[z] for p in s[:2]])
    x0, y0 = pts.min(0) - 2
    x1, y1 = pts.max(0) + 2
    shape = (int((x1 - x0) / pix) + 1, int((y1 - y0) / pix) + 1)

    def rast(segs):
        g = np.zeros(shape, bool)
        for a, b, _ in segs:
            n = max(1, int(math.hypot(b[0] - a[0], b[1] - a[1]) / (pix / 2)))
            t = np.linspace(0, 1, n + 1)
            g[((a[0] + (b[0] - a[0]) * t - x0) / pix).astype(int), ((a[1] + (b[1] - a[1]) * t - y0) / pix).astype(int)] = True
        return ndimage.binary_dilation(g, iterations=1)       # ~ the 0.45 line
    grids = [rast(layers[z]) for z in zs]
    # sparse infill supports its whole region (its lines move between layers; the shells on top of it are normal):
    # the infill lines of a layer closed over 1.6 mm count as solid support for the next layer
    infill = [ndimage.binary_closing(rast([s for s in layers[z] if s[2]]), iterations=int(round(1.6 / pix))) for z in zs]
    if support:
        sz = sorted(support)
        for k, z in enumerate(zs):
            under = [q for zz in sz if z - gap - 1e-6 <= zz < z - 1e-6 for q in support[zz]]
            if under:     # clipped to the part's raster box
                under = [q for q in under if x0 <= min(q[0][0], q[1][0]) and max(q[0][0], q[1][0]) <= x1
                         and y0 <= min(q[0][1], q[1][1]) and max(q[0][1], q[1][1]) <= y1]
                infill[k - 1 if k else 0] = infill[k - 1 if k else 0] | ndimage.binary_closing(rast(under), iterations=2)
    eight = np.ones((3, 3), bool)
    _, nl = ndimage.label(grids[0], structure=eight)
    res = dict(layers=len(zs), height=zs[-1], first_area=round(float(grids[0].sum() * pix * pix), 1), first_pieces=nl,
               islands=[], patches=[])
    for k in range(1, len(zs)):
        g, prev = grids[k], grids[k - 1]
        if support is not None:   # an unlabelled plate mixes layer streams (part 0.16, support its own): every part
            for j in range(k - 2, -1, -1):              # layer within `gap` under this one holds it
                if zs[j] < zs[k] - gap - 1e-6:
                    break
                prev = prev | grids[j]
        reach = ndimage.binary_dilation(prev, iterations=SUPPORT_REACH) | infill[k - 1]
        lab, nl = ndimage.label(g, structure=eight)
        for c in range(1, nl + 1):
            comp = lab == c
            if not (comp & reach).any():
                ii, jj = np.nonzero(comp)
                res['islands'].append(dict(z=zs[k], area=round(float(comp.sum() * pix * pix), 1),
                                           at=(round(x0 + ii.mean() * pix, 1), round(y0 + jj.mean() * pix, 1))))
        lab2, n2 = ndimage.label(g & ~reach, structure=eight)
        for c in range(1, n2 + 1):
            comp = lab2 == c
            area = comp.sum() * pix * pix
            if area <= PATCH_MIN:
                continue
            ii, jj = np.nonzero(comp)
            ext = ((ii.max() - ii.min() + 1) * pix, (jj.max() - jj.min() + 1) * pix)
            col, top = comp.copy(), zs[k]
            for kk in range(k + 1, len(zs)):          # the column that grows up from the patch
                col = grids[kk] & ndimage.binary_dilation(col, iterations=1)
                if not col.any():
                    break
                top = zs[kk]
            # span: the widest stretch the patch has to bridge (twice its largest inscribed radius), not its bounding
            # box: a thin curved sliver (the LED pocket ceiling's edge) is long but bridges nothing
            # (with any hole the patch surrounds filled in: every bridge line has to cross that hole too)
            span = round(float(2 * ndimage.distance_transform_edt(np.pad(ndimage.binary_fill_holes(comp), 1)).max() * pix), 1)
            stacked = round(top - zs[k], 2)
            # reported only: whether the patch surrounds a hole (a pillar's top over a bore, the screw hole in its middle)
            ring = bool((ndimage.binary_fill_holes(comp) & ~comp).any())
            res['patches'].append(dict(z=zs[k], area=round(float(area), 1), extent=tuple(round(e, 1) for e in ext), span=span,
                                       stacked=stacked, at=(round(x0 + ii.mean() * pix, 1), round(y0 + jj.mean() * pix, 1)),
                                       ring=ring,
                                       flag=(('FAIL' if span > span_ok else 'short bridge') if stacked > STACK_MAX else '')))
    return res


def main():
    argv = sys.argv[1:]
    opt = {}
    for key in ('--pix', '--span'):
        if key in argv:
            i = argv.index(key)
            opt[key] = float(argv[i + 1])
            del argv[i:i + 2]
    strict = '--strict' in argv
    argv = [a for a in argv if a != '--strict']
    lines, names = read_gcode(argv[0])
    bad = False
    objs = layers_by_object(lines)
    if not objs:
        print('no object extrusion found: nothing was gated')
        bad = True
    sup = objs.pop('support', None)
    sups = {k[:-len('/support')]: objs.pop(k) for k in [k for k in objs if k.endswith('/support')]}
    if sup:
        print(f'support: {len(sup)} layers (holds the part where it lies under it, not gated itself)')
    for oid, layers in objs.items():
        s_ = sup if oid == 'plate' else sups.get(oid)
        if s_:
            print(f'object {oid}: its support {len(s_)} layers (holds the part where it lies under it, not gated itself)')
        r = analyse(layers, opt.get('--pix', 0.2), opt.get('--span', 4.5), support=s_ if s_ else ({} if oid == 'plate' else None))
        flagged = [p for p in r['patches'] if p['flag']]
        fails = [p for p in flagged if p['flag'] == 'FAIL' or strict] + r['islands']
        bad |= bool(fails)
        print(f"object {oid} {names.get(oid, '')}: {r['layers']} layers to z {r['height']}, first layer {r['first_area']} mm2 "
              f"in {r['first_pieces']} pieces; islands {len(r['islands'])}; unsupported patches {len(r['patches'])}, "
              f"flagged (> {STACK_MAX} mm stacked) {len(flagged)} -> {'FAIL' if fails else 'pass'}")
        for i in r['islands']:
            print(f"   ISLAND       z {i['z']} area {i['area']} mm2 at {i['at']}")
        for p in flagged:
            print(f"   {p['flag']:12s} z {p['z']} area {p['area']} mm2 extent {p['extent']} span {p['span']} "
                  f"stacked {p['stacked']}{' round a hole' if p['ring'] else ''} at {p['at']}")
    print('GATE', 'FAIL' if bad else 'PASS')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Drop-in shelf variants A-F for the printed v1 trays (owner test fit, 2026-09-28).

    python shelf_variants.py build     # print/shelf-variants/: STLs in print orientation + report.json,
                                       # preview/shelf-variants*.png, preview/shelf-seated*.png
    python shelf_variants.py bambu     # bambu/out/a1m-shelf-variants/: one A1 mini plate, sliced with the CLI

The tray is not changed: every variant rests on the same 1.5 mm ledges and the stop-rib top as the v1 shelf
(0.3 mm over the cell envelope), and the motor stands at the same x/z and height as in v1 (PCB underside at
the v1 post tops). What changes is how the cell and the motor are located:

  A  cradle: two end walls under the shelf, 2.5 mm deep, 0.3 mm around the cell ends
  B  corner clips: four short end fingers, 3.5 mm deep (less plastic)
  C  cradle A with two flex tongues that grip the cell ends, so shelf and cell lift out together
  D  two floor stops on the bay floor at the cell ends + a flat shelf (the only one that prints flat)
  E  the owner's idea: one thin 1 x 10 mm rail under the shelf at each cell end
  F  cradle A with 0.5 mm around the cell instead of 0.3

All of them share the motor rails (one thin rail per side instead of the four v1 corner posts) and the
upside-down lock: the -x motor rail stands over the stop rib, so a shelf put in rails-down lands that rail on
the rib top and sits 4 mm proud. A letter and an arrow are embossed on the top face.

Why no wall along the long sides of the cell: on the rib side the cell is 0.3 mm from the rib, and on the far
side the ledge ring comes within 0.9 mm of the cell (the ledge is cut back around the cell corners). The rib
and the curved end wall already hold the cell in x; the loose direction is z, along the cell's length.
"""
import json
import math
import os
import sys

import cadquery as cq
import numpy as np

import keyring_case as K

HERE = K.HERE
OUT = os.path.join(HERE, 'print', 'shelf-variants')
FONT = '/System/Library/Fonts/Supplemental/Arial Bold.ttf'
WHITE = K.FILAMENTS['pm_cotton_white']['hex']
PERI = K.FILAMENTS['pm_pastel_periwinkle']['hex']
CELL = '#8C9199'
BOARD = '#B9BDC4'

VARIANTS = dict(
    A=dict(kind='walls', clr=0.3, t=1.2, depth=2.5),
    B=dict(kind='fingers', clr=0.3, t=1.2, depth=3.5),
    C=dict(kind='walls', clr=0.3, t=1.2, depth=2.5, tongues=True),
    D=dict(kind='floor', clr=0.3),
    E=dict(kind='rails', clr=0.3, t=1.0, depth=2.5),
    F=dict(kind='walls', clr=0.5, t=1.2, depth=2.5),
)
ON_EDGE = ('A', 'B', 'C', 'E', 'F')      # printed standing on the rib-side edge; D prints flat


def geometry(p=None):
    p = p or K.version_p('v1.0')          # the printed v1 trays (K.P() is case v1.1 since 2026-09-28)
    k = K.packing(p)
    box = K.outer_box(p)
    sh = k['parts']['shelf']
    bx0 = sh[0]
    M = K.MEASURED
    g = dict(p=p, box=box, bx0=bx0, y0s=sh[2], y1s=sh[3], floor=k['cavity'][2],
             rib_x0=bx0 - p.rib_t, lipo=k['parts']['lipo'], motor=k['parts']['motor'],
             zc=(box[4] + box[5]) / 2, notch_z=(M['jst']['z'][0], M['jst']['z'][0] + p.lead_notch[0]),
             jst_z=M['jst']['z'])
    mx0, mx1, my0, my1, mz0, mz1 = g['motor']
    g['pcb_y'] = my0 + K.DATASHEET['motor_split'][0]          # motor PCB underside (= v1 post tops)
    g['fence_top'] = g['pcb_y'] + p.fence_h
    g['x_plate'] = bx0 + p.print_clr                            # v1 shelf edge on the rib side
    g['x_tongue'] = g['rib_x0'] + 0.4                           # new: the shelf also rests on the rib top
    g['window'] = (g['notch_z'][0] - 0.5, g['jst_z'][1] + 0.35) # no tongue over the lead notch / JST plug
    g['lead_slot_x'] = 187.0                                    # +z cell end: open from the rib to here
    return g


def _xy_prism(pts, z0, z1):
    """Polygon in the (x, y) plane, extruded along z from z0 to z1."""
    return cq.Workplane('XY').polyline(pts).close().extrude(z1 - z0).translate((0, 0, z0))


def _zy_prism(pts, x0, x1):
    """Polygon in (z, y), extruded along x from x0 to x1."""
    pl = cq.Plane(origin=(x1, 0, 0), xDir=(0, 0, 1), normal=(-1, 0, 0))    # local (u, v) = (z, y), extrudes to -x
    return cq.Workplane(pl).polyline(pts).close().extrude(x1 - x0)


# ---------------------------------------------------------------- common parts of every shelf
def plate(g):
    """v1 plate on the ledges, plus a tongue over the stop-rib top (not over the lead notch / JST plug)."""
    p, box = g['p'], g['box']
    t = g['y1s'] - g['y0s']
    out = K.profile(p, box, g['y0s']).offset2D(-p.wall - p.print_clr).extrude(t)
    base = out.intersect(K._box(g['x_plate'], 300, g['y0s'] - 1, g['y1s'] + 1, -200, 200))
    w0, w1 = g['window']
    tongue = out.intersect(K._box(g['x_tongue'], g['x_plate'] + 0.01, g['y0s'] - 1, g['y1s'] + 1, -200, w0)).union(
        out.intersect(K._box(g['x_tongue'], g['x_plate'] + 0.01, g['y0s'] - 1, g['y1s'] + 1, w1, 200)))
    return base.union(tongue)


def rails(g):
    """One thin rail per motor side: a 1.0 mm fence 0.2 mm off the PCB edge up to 1.2 mm over the PCB
    underside, with a 0.8 x 0.8 mm lip under the PCB edge that carries the motor at the v1 height.
    -x rail: stands over the stop rib (the upside-down lock) and on the bed when printed on edge.
    -z rail: ends in a hook round the +x/-z PCB corner, which locates +x (the +z/+x corner meets the
    curved end wall 0.5 mm away). +z rail: below the Grove socket, clear of its pin tails.
    The rails' -x ends are 45 degree ramps so they print standing on the rib-side edge."""
    mx0, mx1, _, _, mz0, mz1 = g['motor']
    ys, yp, yf = g['y1s'], g['pcb_y'], g['fence_top']
    lip0 = yp - 0.8
    h = yf - ys
    # -x rail
    r1z = (-2.8, g['window'][0] - 0.09)
    r = K._box(g['x_tongue'], mx0 - 0.2, ys - 0.01, yf, *r1z)
    r = r.union(K._box(mx0 - 0.2, mx0 + 0.6, lip0, yp, *r1z))
    # -z rail + hook
    xa = 191.0                                            # fence top starts here; ramp down to the plate
    r = r.union(_xy_prism([(xa - h, ys - 0.01), (mx1 + 0.2, ys - 0.01), (mx1 + 0.2, yf), (xa, yf)], mz0 - 1.2, mz0 - 0.2))
    lx = xa - (yf - lip0)
    r = r.union(_xy_prism([(lx, lip0), (mx1 + 0.2, lip0), (mx1 + 0.2, yp), (lx + 0.8, yp)], mz0 - 0.2, mz0 + 0.6))
    r = r.union(K._box(mx1 + 0.2, mx1 + 1.2, ys - 0.01, yf, mz0 - 1.2, mz0 + 1.5))
    r = r.union(K._box(mx1 - 0.6, mx1 + 0.2, lip0, yp, mz0 - 0.2, mz0 + 1.5))
    # under the hook: a 45 degree corner gusset down to the plate (under the PCB corner, where v1 had its post),
    # so the hook has no flat downward face when printed on edge
    wz0, wz1 = mz0 - 0.2, mz0 + 1.5
    gus = (cq.Workplane(cq.Plane(origin=(0, ys - 0.01, 0), xDir=(1, 0, 0), normal=(0, 1, 0)))
           .polyline([(mx1 + 0.2, -wz0), (mx1 + 0.2 - (wz1 - wz0), -wz0), (mx1 + 0.2, -wz1)]).close()
           .extrude(lip0 - ys + 0.01))
    r = r.union(gus)
    # +z rail
    xa, xb = 187.4, 197.4
    r = r.union(_xy_prism([(xa - h, ys - 0.01), (xb, ys - 0.01), (xb, yf), (xa, yf)], mz1 + 0.2, mz1 + 1.2))
    lx = xa - (yf - lip0)
    r = r.union(_xy_prism([(lx, lip0), (xb, lip0), (xb, yp), (lx + 0.8, yp)], mz1 - 0.6, mz1 + 0.2))
    return r


def label(g, letter, xc=190.8, z_letter=-15.2, z_arrow=-9.0, size=7.0, emboss=0.4):
    """Letter + arrow embossed on the top face, readable from the front (+y); text up = +x."""
    y = g['y1s'] - 0.01
    pl = cq.Plane(origin=(xc, y, z_letter), xDir=(0, 0, 1), normal=(0, 1, 0))
    txt = cq.Workplane(pl).text(letter, size, emboss + 0.01, fontPath=FONT, halign='center', valign='center')
    pa = cq.Plane(origin=(xc, y, z_arrow), xDir=(0, 0, 1), normal=(0, 1, 0))
    s = size / 7.0
    arrow = cq.Workplane(pa).polyline([(-0.8 * s, -3.5 * s), (0.8 * s, -3.5 * s), (0.8 * s, 0.6 * s), (2.3 * s, 0.6 * s),
                                       (0, 3.5 * s), (-2.3 * s, 0.6 * s), (-0.8 * s, 0.6 * s)]).close().extrude(emboss + 0.01)
    return txt.union(arrow)


def under_clip(g, yb):
    """Space an under-shelf feature may use: 0.2 mm inside the ledge ring (offset wall + ledge_w + 0.2)."""
    p = g['p']
    return K.profile(p, g['box'], yb - 1).offset2D(-p.wall - p.ledge_w - 0.2).extrude(g['y0s'] - yb + 1.02)


def end_piece(g, side, x_top, x_end, t, depth, clr, lead_in=0.6):
    """A wall piece under the shelf at one cell end (side -1 = -z, +1 = +z), from the plate down `depth`.
    x_top: rib-side end at the plate; the end ramps 45 degrees (it is a downward face when printed on edge).
    x_end: far end (clipped to the ledge ring anyway). Inner bottom edge chamfered as a lead-in for the cell."""
    hz = (g['lipo'][5] - g['lipo'][4]) / 2 + clr
    z_in = side * hz
    z_out = side * (hz + t)
    x_top = max(x_top, g['x_plate'])
    y_top, yb = g['y0s'] + 0.01, g['y0s'] - depth
    w = _xy_prism([(x_top, y_top), (x_end, y_top), (x_end, yb), (x_top + depth, yb)], min(z_in, z_out), max(z_in, z_out))
    # lead-in chamfer on the pocket side of the bottom edge
    c = min(lead_in, t - 0.4)
    ch = _zy_prism([(z_in - side * 0.01, yb - 0.01), (z_in + side * (c + 0.01), yb - 0.01),
                    (z_in - side * 0.01, yb + c + 0.02)], 150, 250)
    w = w.cut(ch)
    return w.intersect(under_clip(g, yb))


def tongue_cut(g, side, xr, xf, t, depth, clr, slit=0.6):
    """C: a horizontal flex tongue in the end wall, root at xr (rib side), free end at xf; returns
    (material to cut: the slits, material to add: the bump). The wall piece past the free-end slit starts
    with a 45 degree ramp, like every downward face in the on-edge print."""
    hz = (g['lipo'][5] - g['lipo'][4]) / 2 + clr
    z_in, z_out = side * hz, side * (hz + t)
    za, zb = min(z_in, z_out) - 0.05, max(z_in, z_out) + 0.05
    y0, yb = g['y0s'], g['y0s'] - depth
    cut = K._box(xr, xf + slit, y0 - slit, y0 + 0.001, za, zb)                    # slit under the plate
    cut = cut.union(_xy_prism([(xf, yb - 0.1), (xf + slit + depth + 0.1, yb - 0.1), (xf + slit, y0 - slit + 0.001),
                               (xf, y0 - slit + 0.001)], za, zb))                  # free-end slit + ramp
    bump = 0.6                                                                      # 0.3 mm into a 32.0 mm cell
    pts = [(z_in, yb), (z_in - side * bump, yb + bump), (z_in - side * bump, y0 - slit - 0.9), (z_in, y0 - slit - 0.3)]
    b = _zy_prism(pts, xf - 2.4, xf - 0.4)
    return cut, b


def cradle(g, v, name):
    kind, clr = v['kind'], v['clr']
    t, depth = v.get('t'), v.get('depth')
    xp, xs = g['x_plate'], g['lead_slot_x']
    far = 206.0
    parts = []
    if kind == 'walls':
        parts += [end_piece(g, -1, xp, far, t, depth, clr), end_piece(g, +1, xs, far, t, depth, clr)]
    elif kind == 'fingers':
        # rib-side pair (the +z one starts past the lead slot), far pair; lengths measured at the bottom
        parts += [end_piece(g, -1, 185.0 - depth, 188.5, t, depth, clr),
                  end_piece(g, -1, 195.8 - depth, far, t, depth, clr),
                  end_piece(g, +1, max(xs, 190.5 - depth), 193.0, t, depth, clr),
                  end_piece(g, +1, 198.0 - depth, far, t, depth, clr)]
    elif kind == 'rails':
        parts += [end_piece(g, -1, 189.0 - depth, 199.0, t, depth, clr),
                  end_piece(g, +1, 190.0 - depth, 200.0, t, depth, clr)]
    out = None
    for q in parts:
        out = q if out is None else out.union(q)
    if v.get('tongues'):
        for side, (xr, xf) in ((-1, (189.0, 197.0)), (+1, (191.5, 199.5))):
            cut, bump = tongue_cut(g, side, xr, xf, t, depth, clr)
            out = out.cut(cut).union(bump.intersect(under_clip(g, g['y0s'] - depth)))
    return out


def floor_stops(g, clr=0.3, h=1.8, wall_clr=0.3, x0=183.3):
    """D: two pads on the bay floor at the cell ends. Outline 0.3 mm off the bay wall, under the ledges; they go
    in before the cell: set one down in the middle of the bay and slide it out under the ledge (see
    insertion_check). Each carries a small D."""
    p = g['p']
    reg = (K.profile(p, g['box'], g['floor']).offset2D(-p.wall - wall_clr).extrude(h)
           .intersect(K._box(x0, 300, g['floor'] - 1, g['floor'] + h + 1, -200, 200)))
    hz = (g['lipo'][5] - g['lipo'][4]) / 2 + clr
    lo = reg.intersect(K._box(0, 300, -50, 50, -200, -hz))
    hi = reg.intersect(K._box(0, 300, -50, 50, hz, 200))
    out = {}
    for key, pad, zc in (('minus-z', lo, -hz - 2.4), ('plus-z', hi, hz + 3.6)):
        pl = cq.Plane(origin=(192.0, g['floor'] + h - 0.01, zc), xDir=(0, 0, 1), normal=(0, 1, 0))
        pad = pad.union(cq.Workplane(pl).text('D', 3.2, 0.41, fontPath=FONT, halign='center', valign='center'))
        out[key] = pad
    return out


def build(letter):
    g = geometry()
    v = VARIANTS[letter]
    s = plate(g).union(rails(g)).union(label(g, letter))
    parts = {}
    if v['kind'] == 'floor':
        parts['shelf'] = s
        for k, w in floor_stops(g, v['clr']).items():
            parts[f'floor-stop-{k}'] = w
    else:
        parts['shelf'] = s.union(cradle(g, v, letter))
    return g, parts


# ---------------------------------------------------------------- orientation, checks
def to_print(w, on_edge):
    """Case frame -> print frame. On edge: -x (rib side) down, i.e. X = -z, Y = y, Z = x. Flat: front (+y) up."""
    if on_edge:
        w = w.rotate((0, 0, 0), (0, 1, 0), -90)
    else:
        w = w.rotate((0, 0, 0), (1, 0, 0), 90)
    bb = w.val().BoundingBox() if len(w.vals()) == 1 else cq.Compound.makeCompound(w.vals()).BoundingBox()
    return w.translate((-bb.xmin, -bb.ymin, -bb.zmin))


def _comp(w):
    return cq.Compound.makeCompound([s for v in w.vals() for s in (v.Solids() if hasattr(v, 'Solids') else [v])])


def gap_overlap(a, b):
    from OCP.BRepExtrema import BRepExtrema_DistShapeShape
    A, B = _comp(a), _comp(b)
    vol = 0.0
    for sa in A.Solids():
        for sb in B.Solids():
            try:
                vol += sa.intersect(sb).Volume()
            except Exception:
                pass
    d = BRepExtrema_DistShapeShape(A.wrapped, B.wrapped); d.Perform()
    return round(d.Value(), 3), round(vol, 4)


def flipped(g, w):
    """The shelf turned over the wrong way (rails down): 180 degrees about the x axis through the plate's
    mid-plane and the case centre line, which maps the shelf outline onto itself."""
    yc = (g['y0s'] + g['y1s']) / 2
    return w.rotate((0, yc, g['zc']), (1, yc, g['zc']), 180)


def overhangs(stl, bed_eps=0.25, deg=45.0):
    """Downward faces steeper than `deg` from vertical that are not on the bed, in mm2 (print frame)."""
    import trimesh
    m = trimesh.load(stl)
    n = m.face_normals
    c = m.triangles_center
    lim = -math.cos(math.radians(deg))
    sel = (n[:, 2] < lim - 1e-6) & (c[:, 2] > bed_eps)
    return round(float(m.area_faces[sel].sum()), 2), dict(x=round(float(m.extents[0]), 1), y=round(float(m.extents[1]), 1),
                                                          z=round(float(m.extents[2]), 1))


def insertion_check(g, pads, t=1.3, ledge_margin=0.1):
    """D floor stops: lowered at (final - t) they clear the ledge ring and the rib, then slide to the wall.
    The bay floor region is convex, so the straight slide stays inside it."""
    p = g['p']
    res = {}
    opening = (K.profile(p, g['box'], g['floor'] - 1).offset2D(-p.wall - p.ledge_w - ledge_margin).extrude(30)
               .intersect(K._box(g['bx0'] + 0.1, 300, -50, 50, -200, 200)))
    for k, pad in pads.items():
        sz = -1 if k == 'minus-z' else 1
        pre = pad.translate((-t, 0, -sz * t))
        outside = pre.cut(opening)
        vol = sum(v.Volume() for v in outside.vals())
        res[k] = dict(shift_mm=[-t, -sz * t], outside_opening_mm3=round(vol, 4))
    return res


def cell_play(g):
    """How far the cell (envelope and the owner's 20 x 32 cell) can slide in z in the bare bay, from the
    centred position: the inner wall below the ledges bounds the far corners."""
    p = g['p']
    poly, _ = K.outline_pts(p, g['box'], 400)
    P_ = np.array(poly)

    def inside(x, z):
        return K._inside_margin(P_, [(x, z)])[0] - p.wall >= 0

    out = {}
    for name, (w, l) in dict(envelope=(20.5, 32.0), owner=(20.0, 32.0)).items():
        x0 = g['bx0']
        res = {}
        for sgn in (-1, 1):
            d = 0.0
            while d < 20:
                zs = [sgn * (l / 2 + d + 0.05)]
                if not all(inside(x0 + w, z) and inside(x0, z) for z in zs):
                    break
                d += 0.05
            res['+z' if sgn > 0 else '-z'] = round(d, 1)
        out[name] = res
    return out


def export_all():
    os.makedirs(OUT, exist_ok=True)
    trays = {}
    for cl in ('back', 'snap'):
        b, info = K.parts(K.version_p('v1.0', closure=cl))
        trays[cl] = b
    g = geometry()
    cell, mot = K.lipo(g['p']), K.motor(g['p'])
    cz = K.cable_zone(g['p'])
    zone = K._box(*cz)
    report = dict(date='2026-09-28', geometry=dict(
        shelf_y=(g['y0s'], g['y1s']), rib_x=(g['rib_x0'], g['bx0']), tongue_x=g['x_tongue'], tongue_window_z=g['window'],
        notch_z=g['notch_z'], motor=g['motor'], pcb_underside_y=g['pcb_y'], fence_top_y=g['fence_top'],
        cell_envelope=g['lipo'], lead_slot_x=(g['x_plate'], g['lead_slot_x'])), cell_play_bare_bay=cell_play(g), variants={})
    built = {}
    for L in VARIANTS:
        g, parts = build(L)
        built[L] = parts
        rv = dict(parts={})
        for name, w in parts.items():
            on_edge = L in ON_EDGE and name == 'shelf'
            fn = f'shelf-{L}.stl' if name == 'shelf' else f'shelf-{L}-{name}.stl'
            path = os.path.join(OUT, fn)
            cq.exporters.export(to_print(w, on_edge), path, tolerance=0.01, angularTolerance=0.1)
            ov, ext = overhangs(path)
            vol = sum(x.Volume() for x in w.vals())
            rv['parts'][name] = dict(file=fn, orientation='on edge, rib side down' if on_edge else 'flat',
                                     cm3=round(vol / 1000, 3), grams_solid=round(vol / 1000 * K.DATASHEET['pm_matte_density'], 2),
                                     overhang_mm2_over_45deg=ov, print_size_mm=ext)
        allw = None
        for w in parts.values():
            allw = w if allw is None else allw.union(w)
        chk = {}
        for cl, b in trays.items():
            chk[f'tray_{cl}'] = gap_overlap(allw, b['tray'])
            chk[f'cover_{cl}'] = gap_overlap(allw, b['cover'])
        chk['cell_envelope'] = gap_overlap(allw, cell)
        chk['motor'] = gap_overlap(parts['shelf'], mot)
        chk['cable_zone'] = gap_overlap(allw, zone)
        rv['check'] = {k: dict(min_gap=a, overlap_mm3=b_) for k, (a, b_) in chk.items()}
        fl = flipped(g, parts['shelf'])
        rv['upside_down'] = dict(overlap_with_tray_mm3=gap_overlap(fl, trays['back']['tray'])[1],
                                 overlap_with_cell_mm3=gap_overlap(fl, cell)[1])
        if L == 'D':
            rv['floor_stop_insertion'] = insertion_check(g, {k[len('floor-stop-'):]: w for k, w in parts.items()
                                                            if k.startswith('floor-stop')})
        report['variants'][L] = rv
        print(L, json.dumps(rv, default=float))
    json.dump(report, open(os.path.join(OUT, 'report.json'), 'w'), indent=1, default=float)
    return g, built, trays


# ---------------------------------------------------------------- previews
def renders(g, built, trays):
    import closure_study as C
    # 1. all six, from above (rails, letters) and from below (what holds the cell), one row each
    gap = 58.0
    top, bot = [], []
    for i, (L, parts) in enumerate(built.items()):
        dz = i * gap
        for name, w in parts.items():
            dx = 0.0 if name == 'shelf' else -26.0
            top.append(w.translate((dx, 0, dz)))
            bot.append(w.translate((dx, 0, -dz)))       # seen from below z runs the other way: keep A..F left to right
    C.figure('shelf-variants-top', [('shelves', top, 'obj', WHITE)], (0.55, 1.0, 0.35), (1, 0, 0), width=2400)
    C.figure('shelf-variants-bottom', [('shelves', bot, 'obj', WHITE)], (0.55, -1.0, 0.35), (1, 0, 0), width=2400)
    # 2. C seated in the tray with the cell and the motor, cover off; and a section across the cell ends
    tray = trays['back']
    tb = [tray[k] for k in ('tray', 'button', 'loop') if k in tray]
    cut_x = 176.0
    keep = K._box(cut_x, 260, -50, 50, -100, 100)
    tb = [w.intersect(keep) for w in tb]
    cell, mot = K.lipo(g['p']), K.motor(g['p'])
    for L in ('C', 'D'):
        parts = built[L]
        C.figure(f'shelf-seated-{L}', [('tray', tb, 'obj', WHITE), ('shelf', list(parts.values()), 'obj', PERI),
                                        ('cell', [cell], 'obj', CELL), ('motor', [mot], 'ctx', BOARD)],
                 (0.35, 1.0, 0.55), (1, 0, 0), width=2000)
    x = 194.0
    for L in ('A', 'C'):
        C.figure(f'shelf-seated-{L}-section', [
            ('tray', C.keep_below_x(tb, x), 'obj', WHITE), ('shelf', C.keep_below_x(list(built[L].values()), x), 'obj', PERI),
            ('cell', C.keep_below_x([cell], x), 'obj', CELL), ('motor', C.keep_below_x([mot], x), 'ctx', BOARD)],
            *C.SECTION, width=2000)


def sheet(out_path):
    """One PNG: top row the six from above, bottom row from below, then the seated views."""
    from PIL import Image
    pv = K.PREVIEW
    ims = [Image.open(os.path.join(pv, f)).convert('RGB') for f in
           ('shelf-variants-top.png', 'shelf-variants-bottom.png')]
    W = max(i.width for i in ims)
    H = sum(i.height for i in ims)
    s = Image.new('RGB', (W, H), 'white')
    y = 0
    for i in ims:
        s.paste(i, ((W - i.width) // 2, y)); y += i.height
    s.save(out_path)
    return out_path


# ---------------------------------------------------------------- Bambu (A1 mini, one plate)
def bambu_job():
    sys.path.insert(0, os.path.join(HERE, 'bambu'))
    import build_bambu as B
    key = 'a1m'
    pd = os.path.join(B.PROF, key)
    if not os.path.exists(os.path.join(pd, 'machine.json')):
        B.write_profiles(key)
    objs, params = [], []
    i = 1
    for L in VARIANTS:
        for fn in sorted(os.listdir(OUT)):
            if not fn.endswith('.stl') or not fn.startswith(f'shelf-{L}'):
                continue
            objs.append(dict(path=os.path.join(OUT, fn), count=1, filaments=[1], assemble_index=[i]))
            if L in ON_EDGE and fn == f'shelf-{L}.stl':
                params.append(dict(assemble_index=i, print_params={'brim_type': 'outer_only', 'brim_width': '5',
                                                                   'brim_object_gap': '0'}))
            i += 1
    plates = [{'plate_name': 'Shelf variants A-F (white)', 'need_arrange': True,
               'plate_params': {'print_sequence': 'by layer'}, 'objects': objs, 'assembled_params': params}]
    name = 'a1m-shelf-variants'
    os.makedirs(B.JOBS, exist_ok=True)
    jp = os.path.join(B.JOBS, f'{name}.json')
    json.dump({'plates': plates}, open(jp, 'w'), indent=1)
    od = os.path.join(B.OUT, name); os.makedirs(od, exist_ok=True)
    fil = ';'.join(os.path.join(pd, f'filament-{k + 1}.json') for k in range(len(B.SPOOLS)))
    base = [B.CLI, '--debug', '2', '--load-assemble-list', jp, '--load-settings',
            f'{pd}/machine.json;{pd}/process.json', '--load-filaments', fil, '--allow-multicolor-oneplate']
    return name, od, base


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'build'
    if cmd == 'build':
        g, built, trays = export_all()
        renders(g, built, trays)
    elif cmd == 'renders':
        g = geometry()
        built = {L: build(L)[1] for L in VARIANTS}
        trays = {'back': K.parts(K.version_p('v1.0', closure='back'))[0]}
        renders(g, built, trays)
    elif cmd == 'bambu':
        import subprocess
        name, od, base = bambu_job()
        final_dir = sys.argv[2] if len(sys.argv) > 2 else od
        for attempt in range(3):
            r = subprocess.run(base + ['--slice', '0', '--outputdir', od, '--export-3mf', f'{name}-sliced.3mf'],
                               capture_output=True, text=True)
            res = json.load(open(os.path.join(od, 'result.json')))
            print('slice', attempt + 1, res.get('return_code'), res.get('error_string'))
            if res.get('return_code') == 0:
                break
        else:
            raise SystemExit(f'{name}: slicing failed three times, no project exported (see {od}/result.json)')
        r = subprocess.run(base + ['--outputdir', final_dir, '--export-3mf', f'{name}.3mf'], capture_output=True, text=True)
        print('project', r.returncode)
        if r.returncode != 0:
            raise SystemExit(f'{name}: project export failed ({r.returncode})')
    else:
        raise SystemExit(__doc__)

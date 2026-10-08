#!/usr/bin/env python3
"""Assembly-guide line art: edition A (checkpoint, the Grove vibration module: tray 17.1 + shelf 17.2b + frame / face
18.2-G) and edition B (the DA7280: tray 17.1 + shelf 17.3c + frame / face 18.1e).

    python guide_figures_da7280.py [out_dir] [A|B]     # -> <out_dir>/*.{svg,png} (default: guide-figures-da7280, B)

Edition A's step-8 is the Grove cable from the main board's UART port down into the module's vertical socket (no hub).

Same renderer and style as guide_figures.py (one HLR pass, the part added in the step dark and lifted, the rest grey,
no text in any raster). Figures for stack.md: step-5 (the battery into the bay), step-7 (the main board onto the
standoffs + the battery lead), step-8 (the hub onto the display, Grove UART -> hub, Qwiic hub -> motor board),
step-10 (the 4 screws from the back), closed, hanging. The DA7280 and the hub are neutral placeholder boxes (the
DA7280 25.4 x 29.2 with its LRA Ø10 x 4 on top, in its 17.3b place; the hub 25.4 x 17.8 on the display); no dowels,
no motor-seat or pin detail (the 17.3b pins are cut off at the plate).
"""
import os
import sys

import cadquery as cq

import guide_figures as G
import keyring_case as K


def tube(pts, r):
    """A cable: cylinders between the points, a ball at each joint."""
    w = None
    for a, b in zip(pts, pts[1:]):
        va, vb = cq.Vector(*a), cq.Vector(*b)
        d = vb - va
        s = cq.Solid.makeCylinder(r, d.Length, va, d.normalized())
        w = cq.Workplane().add(s) if w is None else w.union(cq.Workplane().add(s))
    for q in pts[1:-1]:
        w = w.union(cq.Workplane().add(cq.Solid.makeSphere(r, cq.Vector(*q), angleDegrees1=-90, angleDegrees2=90)))
    return w


def main_a():
    """Edition A: the frozen case for the Grove vibration module (17.1 + 17.2b + 18.2-G)."""
    p17 = K.version_p('v1.3', closure='back', proto=17)
    b17, info17 = K.parts(p17)
    p = K.version_p('v1.3-p18.2-G', closure='back', proto=18)
    b, info = K.parts(p)
    S, _ = K.shelf_17_2b()
    sy1 = K.packing(p17)['parts']['shelf'][3]
    shelf = S.cut(K._box(100, 300, sy1 + 0.001, 40, -60, 60))            # no motor-seat detail
    mot = K.motor(p)
    bp = K.board_parts()
    board = [bp['expansion'], bp['headers'], bp['xiao']]
    cell = K.lipo(p17)
    sc = K.screws(p, info)
    screws_ = sc[0::2]
    tray = [b17[k] for k in ('tray', 'button', 'pad', 'loop') if k in b17]
    cover = [b['cover'], b['frame']]
    lead = tube([(183.0, 0.3, 10.9), (180.5, 1.0, 10.9), (178.0, 1.6, 11.0), (176.9, 1.8, 11.0)], 0.45)
    x0, x1, y0, y1, z0, z1 = K.packing(p)['parts']['motor']
    zc = (z0 + z1) / 2 + p.motor_dz
    sock_top = y0 + sum(K.DATASHEET['motor_split'])
    sx = x0 + 3.0
    plug = K._box(sx - 2.3, sx + 2.3, sock_top - 2.0, sock_top + 1.0, zc - 4.6, zc + 4.6)
    grove = tube([(160.2, 3.6, 17.2), (160.2, 6.8, 16.0), (170.0, 10.4, 10.0), (sx, sock_top + 1.2, zc + 1.0),
                  (sx, sock_top + 0.9, zc)], 0.6)
    SIDE, BACK = (0.25, 0.5, 1.0), (0.35, -1.0, 0.8)
    base = tray + [shelf, mot]
    G.figure('step-5', [('new', G.moved([cell], 14), 'obj'), ('rest', tray, 'ctx')], view=SIDE)
    G.figure('step-7', [('new', G.moved(board, 16) + [lead], 'obj'), ('rest', base + [cell], 'ctx')], view=SIDE)
    G.figure('step-8', [('new', [grove, plug], 'obj'), ('rest', base + [cell] + board + [lead], 'ctx')], view=SIDE)
    # step-3: an M2 nut into the side slot of each frame pillar (slots open toward the case middle; frame upside down)
    nuts_ = sc[1::2]
    xmid = (info['box'][0] + info['box'][1]) / 2
    nuts_out = [n.translate((7.0 if hx < xmid else -7.0, 0, 0)) for n, (hx, hz) in zip(nuts_, info['fastening']['holes'])]
    bx = G.figure('step-3', [('new', nuts_out, 'obj'), ('rest', [frame], 'ctx')], view=BACK)
    hole_marks('step-3', bx, BACK, G.UP, {i + 1: (hx, info['fastening']['nut'][0], hz)
                                         for i, (hx, hz) in enumerate(info['fastening']['holes'])})
    # step-9: the cover (face + frame, nuts in) onto tray 17.1: USB-C end down first, the loop end still raised
    import math
    ang = 7.0
    usb_x = info['box'][0]
    def tilt(w):
        return w.rotate((usb_x, split, 0), (usb_x, split, 1), ang).translate((0, 1.5, 0))
    G.figure('step-9', [('new', [tilt(face), tilt(frame)] + [tilt(n) for n in nuts_], 'obj'),
                        ('rest', tray + [shelf, da, cell] + board + [hub], 'ctx')], view=SIDE)
    G.figure('step-10', [('new', G.moved(screws_, -12), 'obj'), ('rest', tray + cover, 'ctx')], view=BACK)
    G.figure('closed', [('case', tray + cover + screws_, 'obj')])
    lp = info17['loop']
    box = info17['box']
    yc, zc2 = (box[2] + box[3]) / 2, (box[4] + box[5]) / 2
    Rr, wire = 12.5 - 0.7, 0.7
    ring = cq.Workplane().add(cq.Solid.makeTorus(Rr, wire, pnt=cq.Vector(lp['hole_x'] + Rr - lp['hole_r'] + wire, yc, zc2),
                                                 dir=cq.Vector(0, 0, 1)))
    G.figure('hanging', [('case', tray + cover + screws_, 'obj'), ('ring', [ring], 'ctx')])


def hole_marks(name, bx, view, up, holes, width=1600):
    """<name>-holes.json beside the figure: each numbered hole's point in the SVG's coordinates and in the PNG's
    pixels (the guide sets the numbers as vector overlays; no text in any raster)."""
    import json
    vv = cq.Vector(*view).normalized(); xd = cq.Vector(*up).cross(vv).normalized(); yd = vv.cross(xd)
    k = width / bx[2]
    out = {}
    for n, (x, y, z) in holes.items():
        pnt = cq.Vector(x, y, z)
        sx, sy = pnt.dot(xd), -pnt.dot(yd)
        out[str(n)] = dict(svg=[round(sx, 3), round(sy, 3)], png=[round((sx - bx[0]) * k, 1), round((sy - bx[1]) * k, 1)])
    json.dump(dict(viewBox=[round(v, 3) for v in bx], png_width=width, holes=out),
              open(os.path.join(G.OUT, f'{name}-holes.json'), 'w'), indent=1)


def main(out=None, edition='B'):
    G.OUT = out or os.path.join(K.HERE, 'guide-figures-da7280')
    if edition == 'A':
        return main_a()
    p17 = K.version_p('v1.3', closure='back', proto=17)
    b17, info17 = K.parts(p17)
    p = K.version_p('v1.3-p18.1e', closure='back', proto=18)
    b, info = K.parts(p)
    S, inf = K.shelf_17_3c()
    pl = inf['place']
    shelf = S.cut(K._box(100, 300, pl['yb'] + 0.001, 40, -60, 60))     # the pads show the seat, no pins
    da = K._box(pl['bx0'], pl['bx0'] + 25.4, pl['yb'], pl['top'], pl['zlo'], pl['zlo'] + 29.2)
    lx, lz = pl['bx0'] + 12.7, pl['zlo'] + 29.21 - 23.09
    da = da.union(K._cyl(lx, lz, 5.0, pl['top'] - 0.01, pl['top'] + 4.0))
    hub = K._box(141.3, 166.7, -0.6, 1.0, -8.6, 9.2)
    bp = K.board_parts()
    board = [bp['expansion'], bp['headers'], bp['xiao']]
    cell = K.lipo(p17)
    sc = K.screws(p, info)
    screws_ = sc[0::2]
    tray = [b17[k] for k in ('tray', 'button', 'pad', 'loop') if k in b17]
    face, frame = b['cover'], b['frame']
    cover = [face, frame]
    split = info['split']
    # dowels: numbered as the owner sheet (along the case from the USB end, then across)
    dw = sorted([(x, z) for x, z, _ in p.extra_dowels] + [(x, z) for x, z, *_ in p.short_dowels], key=lambda d: (round(d[0]), d[1]))
    depth = {}
    for x, z, _ in p.extra_dowels:
        depth[(x, z)] = p.peg_frame_d
    for i, (x, z, *_r) in enumerate(p.short_dowels):
        depth[(x, z)] = p.short_frame_d[i]
    def dowel(x, z, y0):
        return K._cyl(x, z, 1.5, y0, y0 + 5.0)
    seated = [dowel(x, z, split - depth[(x, z)]) for x, z in dw]
    lifted = [dowel(x, z, split + 6.0) for x, z in dw]
    # cables (schematic runs, as stack.md describes them); the Qwiic run rises over the 2 x 4 pins to the raised J1
    lead = tube([(183.0, 0.3, 10.9), (180.5, 1.0, 10.9), (178.0, 1.6, 11.0), (176.9, 1.8, 11.0)], 0.45)
    grove = tube([(160.2, 3.6, 17.2), (160.2, 6.0, 15.5), (162.5, 5.2, 11.0), (165.0, 3.0, 8.0), (165.5, 1.6, 6.5)], 0.6)
    j1z = pl['zlo'] + 16.51
    jy = pl['top'] + 1.5
    qwiic = tube([(166.2, 1.6, 2.0), (169.0, 5.0, 4.0), (172.0, 9.0, j1z), (pl['bx0'] + 0.48 - 3.0, jy, j1z)], 0.55)
    plug = K._box(pl['bx0'] + 0.48 - 3.0, pl['bx0'] + 0.48, pl['top'] + 0.3, pl['top'] + 3.1, j1z - 2.5, j1z + 2.5)
    SIDE, BACK, TOP = (0.25, 0.5, 1.0), (0.35, -1.0, 0.8), (0.3, 1.0, 0.55)
    base = tray + [shelf, da]
    G.figure('step-5', [('new', G.moved([cell], 14), 'obj'), ('rest', tray, 'ctx')], view=SIDE)
    G.figure('step-6', [('new', G.moved([shelf, da], 14), 'obj'), ('rest', tray + [cell], 'ctx')], view=SIDE)
    G.figure('step-7', [('new', G.moved(board, 16) + [lead], 'obj'), ('rest', base + [cell], 'ctx')], view=SIDE)
    G.figure('step-8', [('new', G.moved([hub], 8) + [grove, qwiic, plug], 'obj'),
                        ('rest', base + [cell] + board + [lead], 'ctx')], view=SIDE)
    # new order step 1: the dowels into the frame (parting face up); step 4: the face onto them
    bx = G.figure('step-1', [('new', lifted, 'obj'), ('rest', [frame], 'ctx')], view=TOP)
    hole_marks('step-1', bx, TOP, G.UP, {i + 1: (x, split, z) for i, (x, z) in enumerate(dw)})
    tips = {i + 1: (x, split - depth[(x, z)] + 5.0, z) for i, (x, z) in enumerate(dw)}      # the seated dowels' tips
    bx = G.figure('step-4', [('new', G.moved([face], 20), 'obj'), ('rest', [frame] + seated, 'ctx')], view=SIDE)
    hole_marks('step-4', bx, SIDE, G.UP, tips)
    # step-3: an M2 nut into the side slot of each frame pillar (slots open toward the case middle; frame upside down)
    nuts_ = sc[1::2]
    xmid = (info['box'][0] + info['box'][1]) / 2
    nuts_out = [n.translate((7.0 if hx < xmid else -7.0, 0, 0)) for n, (hx, hz) in zip(nuts_, info['fastening']['holes'])]
    bx = G.figure('step-3', [('new', nuts_out, 'obj'), ('rest', [frame], 'ctx')], view=BACK)
    hole_marks('step-3', bx, BACK, G.UP, {i + 1: (hx, info['fastening']['nut'][0], hz)
                                         for i, (hx, hz) in enumerate(info['fastening']['holes'])})
    # step-9: the cover (face + frame, nuts in) onto tray 17.1: USB-C end down first, the loop end still raised
    import math
    ang = 7.0
    usb_x = info['box'][0]
    def tilt(w):
        return w.rotate((usb_x, split, 0), (usb_x, split, 1), ang).translate((0, 1.5, 0))
    G.figure('step-9', [('new', [tilt(face), tilt(frame)] + [tilt(n) for n in nuts_], 'obj'),
                        ('rest', tray + [shelf, da, cell] + board + [hub], 'ctx')], view=SIDE)
    G.figure('step-10', [('new', G.moved(screws_, -12), 'obj'), ('rest', tray + cover, 'ctx')], view=BACK)
    G.figure('closed', [('case', tray + cover + screws_, 'obj')])
    lp = info17['loop']
    box = info17['box']
    yc, zc = (box[2] + box[3]) / 2, (box[4] + box[5]) / 2
    Rr, wire = 12.5 - 0.7, 0.7
    ring = cq.Workplane().add(cq.Solid.makeTorus(Rr, wire, pnt=cq.Vector(lp['hole_x'] + Rr - lp['hole_r'] + wire, yc, zc),
                                                 dir=cq.Vector(0, 0, 1)))
    G.figure('hanging', [('case', tray + cover + screws_, 'obj'), ('ring', [ring], 'ctx')])


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else None, sys.argv[2] if len(sys.argv) > 2 else 'B')

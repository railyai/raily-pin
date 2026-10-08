#!/usr/bin/env python3
"""Edition B close-ups for the leaflet (2026-09-30): step-6a/6b/6c (cell, shelf 17.3c, DA7280 at the loop end, one view
looking almost straight into the open tray, loop end up) and the hole-9 dowel inset step-1-d1 / step-1-d2 (about 6x).
Same renderer and style as guide_figures.py, no text in any raster; sidecar JSON with the points the leaflet labels.

    DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib python guide_figures_b_closeups.py <out_dir>
"""
import json
import os
import sys

import cadquery as cq

import guide_figures as G
import guide_figures_da7280 as B
import keyring_case as K

VIEW6 = (0.2, 1.0, 0.35)


def proj(view, up, bx, width, pts):
    vv = cq.Vector(*view).normalized(); xd = cq.Vector(*up).cross(vv).normalized(); yd = vv.cross(xd)
    k = width / bx[2]
    out = {}
    for n, (x, y, z) in pts.items():
        v = cq.Vector(x, y, z); sx, sy = v.dot(xd), -v.dot(yd)
        out[n] = dict(model=[x, y, z], svg=[round(sx, 3), round(sy, 3)], png=[round((sx - bx[0]) * k, 1), round((sy - bx[1]) * k, 1)])
    return out


def sidecar(name, bx, view, pts=None, width=1600, up=None):
    up = up or G.UP
    json.dump(dict(viewBox=[round(v, 3) for v in bx], png_width=width, view=list(view), up=list(up),
                   points=proj(view, up, bx, width, pts)), open(os.path.join(G.OUT, f'{name}-points.json'), 'w'), indent=1)


def main(out):
    G.OUT = out
    p17 = K.version_p('v1.3', closure='back', proto=17)
    b17, info17 = K.parts(p17)
    p = K.version_p('v1.3-p18.1e', closure='back', proto=18)
    b, info = K.parts(p)
    S, inf = K.shelf_17_3c()
    pl = inf['place']
    shelf = S.cut(K._box(100, 300, pl['yb'] + 0.001, 40, -60, 60))
    comps = inf['comps']
    da = [comps['board'], comps['lra'], comps['j1'], comps['j2']]
    cell = K.lipo(p17)
    tray = [b17[k] for k in ('tray', 'button', 'pad', 'loop') if k in b17]
    box = info17['box']
    # the crop: the loop-end half, x 170 ... 216, full width, over the tray's height
    frame6 = [K._box(170.0, 216.0, box[2], box[3], box[4], box[5])]
    # the cell's lead: out of its end (x 182.2), up through the partition's notch (x 180.6-181.9, z 6.89-14.89) toward the board
    lead = B.tube([(183.0, -2.0, 11.0), (182.0, -0.4, 11.0), (181.2, 0.6, 10.9), (179.5, 1.6, 10.9), (177.0, 1.8, 11.0)], 0.45)
    notch = (181.25, 1.25, 10.89)
    g = G.figure('step-6a', [('new', G.moved([cell], 6) + [lead], 'obj'), ('rest', tray, 'ctx')], view=VIEW6, frame=frame6)
    sidecar('step-6a', g, VIEW6, dict(notch=notch))
    g = G.figure('step-6b', [('new', G.moved([shelf], 6), 'obj'), ('rest', tray + [cell, lead], 'ctx')], view=VIEW6, frame=frame6)
    sidecar('step-6b', g, VIEW6, dict(notch=notch))
    j1 = comps['j1'].val().BoundingBox(); j2 = comps['j2'].val().BoundingBox()
    g = G.figure('step-6c', [('new', G.moved(da, 6), 'obj'), ('rest', tray + [cell, lead, shelf], 'ctx')], view=VIEW6, frame=frame6)
    sidecar('step-6c', g, VIEW6, dict(notch=notch,
                                      qwiic_used_J1=(j1.xmin, j1.ymax + 6, (j1.zmin + j1.zmax) / 2),
                                      qwiic_spare_J2=(j2.xmax, j2.ymax + 6, (j2.zmin + j2.zmax) / 2)))
    # hole 9 inset (the loop-centre dowel), the step-1 view; d2 as a half-section through the hole axis
    SIDE9, UP9 = (0.3, 0.35, 1.0), (0, 1, 0)       # a side view, the dowel standing: the protrusion reads
    split = info['split']
    hx, hz = p.short_dowels[2][0], p.short_dowels[2][1]
    D = p.short_frame_d[2]
    frame = b['frame']
    region = frame.intersect(K._cyl(hx, hz, 5.0, split - 6, split + 0.01))
    dowel = lambda y0: (cq.Workplane().add(cq.Solid.makeCylinder(1.5, 5.0, cq.Vector(hx, y0, hz), cq.Vector(0, 1, 0)))
                        .faces('>Y or <Y').edges().chamfer(0.3))
    d_up = dowel(split + 3.0)
    half = K._box(hx - 10, hx + 10, split - 10, split + 10, hz, hz + 10)          # the near (+z) half cut away
    g = G.figure('step-1-d1', [('new', [d_up], 'obj'), ('rest', [region.cut(half)], 'ctx')], view=SIDE9, up=UP9)
    sidecar('step-1-d1', g, SIDE9, dict(hole9_mouth=(hx, split, hz), dowel_bottom=(hx, split + 3.0, hz)), up=UP9)
    sec_region = region.cut(half)
    d_seat = dowel(split - D).cut(half)
    g = G.figure('step-1-d2', [('new', [d_seat], 'obj'), ('rest', [sec_region], 'ctx')], view=SIDE9, up=UP9)
    sidecar('step-1-d2', g, SIDE9, up=UP9, pts=dict(dowel_top=(hx, split - D + 5.0, hz), frame_surface=(hx + 3.0, split, hz),
                                      stop_ring=(hx, split - D, hz)))
    # the side button: tongue x 155.25-171.25 on +z; its nub over D1 (x 167.78-169.73)
    bb = b17['button'].val().BoundingBox()
    btn = dict(button_nub=(168.75, -0.6, bb.zmax), tongue_usb_end=(bb.xmin, -0.6, bb.zmax), tongue_loop_end=(bb.xmax, -0.6, bb.zmax))
    sc = K.screws(p, info)[0::2]
    cover = [b['cover'], b['frame']]
    g = G.figure('closed', [('case', tray + cover + sc, 'obj')])
    sidecar('closed', g, G.ISO, btn)
    # hole 9's centre in the full step-1 figure (its own crop box from step-1-holes.json)
    s1 = json.load(open(os.path.join(G.OUT, 'step-1-holes.json')))
    json.dump(dict(step1_hole9=s1['holes'].get('9'), protrusion_mm=round(5.0 - D, 2), stop_ring_mm=1.28),
              open(os.path.join(G.OUT, 'step-1-hole9.json'), 'w'), indent=1)


if __name__ == '__main__':
    main(sys.argv[1])

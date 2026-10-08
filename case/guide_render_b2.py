"""Guide renders for edition B with cover 18.2-B (owner, 2026-10-03), no part codes anywhere.

Wraps the edition-B figure scripts: every version_p() call is built with proto=None / proto_label=None, the edition-B
cover 'v1.3-p18.1e' is swapped for 'v1.3-p18.2-B', and shelf_17_3c() defaults to proto=None (the shipped shelf has no
code). Then draws the new cover figures for the owner's order (dowels into the FACE first, then the frame onto them):

    face-dowels   the face lying front down, back up; a dowel lifted over each of its 9 holes   (+ -holes.json)
    frame-on      the frame, parting face down, lowered onto the 9 dowels standing in the face  (+ -holes.json)
    nuts          the cover still face down: an M2 nut beside each pillar's side slot            (+ -points.json)
    rack          the dowel rack, rows S 3.00 / M 3.05 / L 3.10                                   (+ -points.json)

    DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib python guide_render_b2.py <out_dir> [old|new|all]
"""
import json
import os
import sys

CASE = os.path.dirname(os.path.abspath(__file__))
CALLER_CWD = os.getcwd()   # <out_dir> is relative to where the script was started
sys.path.insert(0, CASE)
os.chdir(CASE)
import cadquery as cq
import keyring_case as K

_orig_vp = K.version_p
_orig_shelf = K.shelf_17_3c


def version_p_b2(name='v1', **kw):
    if name == 'v1.3-p18.1e':
        name = 'v1.3-p18.2-B'
    kw['proto'] = None
    kw['proto_label'] = None
    return _orig_vp(name, **kw)


def shelf_17_3c_b2(*a, **kw):
    kw['proto'] = None
    return _orig_shelf(*a, **kw)


K.version_p = version_p_b2
K.shelf_17_3c = shelf_17_3c_b2

import guide_figures as G
import guide_figures_da7280 as B
import guide_figures_b_closeups as C
import guide_colour_b as CB


def proj(view, up, bx, width, pts):
    vv = cq.Vector(*view).normalized(); xd = cq.Vector(*up).cross(vv).normalized(); yd = vv.cross(xd)
    k = width / bx[2]
    out = {}
    for n, (x, y, z) in pts.items():
        v = cq.Vector(x, y, z); sx, sy = v.dot(xd), -v.dot(yd)
        out[str(n)] = dict(model=[x, y, z], svg=[round(sx, 3), round(sy, 3)], png=[round((sx - bx[0]) * k, 1), round((sy - bx[1]) * k, 1)])
    return out


def sidecar(name, bx, view, up, pts, width=1600, key='points'):
    with open(os.path.join(G.OUT, f'{name}-{key}.json'), 'w') as f:
        json.dump({'viewBox': [round(v, 3) for v in bx], 'png_width': width, 'view': list(view), 'up': list(up),
                   key: proj(view, up, bx, width, pts)}, f, indent=1)


def new_figures(out):
    G.OUT = out
    p = K.version_p('v1.3-p18.2-B', closure='back')
    if p.proto is not None or p.proto_label is not None or p.test_joint or not p.sml_sprues:
        sys.exit('guide_render_b2: 18.2-B must build with no part code, no test joint and the S/M/L rack')
    b, info = K.parts(p)
    face, frame = b['cover'], b['frame']
    split = info['split']
    # dowels numbered as the owner sheet (P18-2G-sborka): along the case from the USB end, then across
    dw = sorted([(x, z) for x, z, _ in p.extra_dowels] + [(x, z) for x, z, *_ in p.short_dowels], key=lambda d: (round(d[0]), d[1]))
    face_d = {(x, z): p.peg_face_d for x, z, _ in p.extra_dowels}
    for i, (x, z, *_r) in enumerate(p.short_dowels):
        face_d[(x, z)] = p.short_face_ds[i]
    L = 2 * p.peg_depth                      # the dowel, 5.0 with 0.3 end chamfers

    def dowel(x, z, y0):
        return (cq.Workplane().add(cq.Solid.makeCylinder(p.peg_d / 2, L, cq.Vector(x, y0, z), cq.Vector(0, 1, 0)))
                .faces('>Y or <Y').edges().chamfer(0.3))
    # in the face's hole (from the line up face_d), bottomed: its lower end face_d - L below the line
    seated = [dowel(x, z, split + face_d[(x, z)] - L) for x, z in dw]
    lifted = [dowel(x, z, split - 6.0 - L) for x, z in dw]
    numbered = {i + 1: (x, split, z) for i, (x, z) in enumerate(dw)}
    loc = [tuple(round(v, 3) for v in l_) for l_ in p.dowel_locators]
    locators = [i + 1 for i, (x, z) in enumerate(dw) if (round(x, 3), round(z, 3)) in loc]
    # 1: the face lying front down (its back, the parting side, faces -y = up), a dowel over each hole
    FACEUP, UP = (0.3, -1.0, 0.55), G.UP
    bx = G.figure('face-dowels', [('new', lifted, 'obj'), ('rest', [face], 'ctx')], view=FACEUP, up=UP)
    sidecar('face-dowels', bx, FACEUP, UP, numbered, key='holes')
    # 2: the frame, parting face down, lowered onto the dowels standing in the face
    SIDEB = (0.25, -0.5, 1.0)
    tips = {i + 1: (x, split + face_d[(x, z)] - L, z) for i, (x, z) in enumerate(dw)}
    bx = G.figure('frame-on', [('new', G.moved([frame], -20), 'obj'), ('rest', [face] + seated, 'ctx')], view=SIDEB, up=UP)
    sidecar('frame-on', bx, SIDEB, UP, tips, key='holes')
    # 3: the cover still face down: an M2 nut beside each pillar's side slot (slots open toward the case middle)
    BACK = (0.35, -1.0, 0.8)
    sc = K.screws(p, info)
    nuts_ = sc[1::2]
    xmid = (info['box'][0] + info['box'][1]) / 2
    holes = info['fastening']['holes']
    if len(nuts_) != len(holes):
        sys.exit(f'guide_render_b2: {len(nuts_)} nuts for {len(holes)} pillar holes')
    nuts_out = [n.translate((7.0 if hx < xmid else -7.0, 0, 0)) for n, (hx, hz) in zip(nuts_, holes)]
    bx = G.figure('nuts', [('new', nuts_out, 'obj'), ('rest', [frame, face] + seated, 'ctx')], view=BACK, up=UP)
    pts = {}
    for i, (n, (hx, hz)) in enumerate(zip(nuts_out, holes)):
        c = n.val().BoundingBox().center
        pts[f'nut{i + 1}'] = (c.x, c.y, c.z)
        pts[f'pillar{i + 1}'] = (hx, c.y, hz)
    sidecar('nuts', bx, BACK, UP, pts)
    # 4: the dowel rack (print pose, z up), seen from above; the label tabs at x -5, rows pitch_y 12
    rack = K.sml_rack(p)
    RV, RU = (0.0, 0.35, 1.0), (0, 1, 0)
    bx = G.figure('rack', [('rack', [rack], 'obj')], view=RV, up=RU)
    rp = {}
    for i, (lt, dd) in enumerate(p.sml_sprues):
        rp[f'{lt}_tab'] = (-5.0, i * 12.0, 1.0)
        rp[f'{lt}_end'] = (48.1, i * 12.0 + 3.0, 2.5)
    sidecar('rack', bx, RV, RU, rp)
    with open(os.path.join(out, 'cover-facts.json'), 'w') as f:
        json.dump(dict(dowels=[dict(n=i + 1, x=x, z=z, face_d=face_d[(x, z)], proud=round(L - face_d[(x, z)], 2))
                               for i, (x, z) in enumerate(dw)], locators=locators, peg_clr_face=p.peg_clr_face,
                       peg_ribs=p.peg_ribs, sizes=p.sml_sprues, pusher_cup=1.5), f, indent=1)
    print('locators', locators)


if __name__ == '__main__':
    what = sys.argv[2] if len(sys.argv) > 2 else 'all'
    if not 2 <= len(sys.argv) <= 3 or what not in ('old', 'new', 'all'):
        sys.exit('usage: python guide_render_b2.py <out_dir> [old|new|all]')
    out = os.path.join(CALLER_CWD, sys.argv[1])
    os.makedirs(out, exist_ok=True)
    if what in ('old', 'all'):
        B.main(out, 'B')       # step-5..10, closed, hanging (+ the frame-first step-1/3/4, unused)
        C.main(out)            # step-6a/6b/6c, closed-points (+ the hole-9 inset, unused)
        CB.main(out)           # colour-v1/v5
    if what in ('new', 'all'):
        new_figures(out)

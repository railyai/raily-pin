#!/usr/bin/env python3
"""Per-part line-art layers of the Keyring P1 (DIY kit, edition B: the DA7280 motor) for the atlas scroll-driven
exploded view, in the same layer format as the site's other atlas views: one SVG per part on one shared canvas, camera and mm scale, each part at its assembled place,
a white `class="fill"` silhouette under a `class="lines"` group, no text; assembled.svg (one hidden-line pass over the
whole closed case); the visible-only layers for the site's colour masks; layers.json; composites and check images.

Edition B: tray 17.1 (`version_p('v1.3')`), shelf 17.3c (`shelf_17_3c()`, the DA7280 raised on pads, the arrow
deboss), cover 18.1e (`version_p('v1.3-p18.1e')`: frame + face, 9 round dowels), the DA7280 board as placed by
17.3c (`da7280_place`), the XIAO nRF52840 Sense on the Expansion Board (Seeed STEPs via `board_parts()`), the
Grove-Qwiic hub (`guide/cad/parts.py`) on the display, the 602030 cell envelope (`lipo()`), 4 M2 x 16 countersunk
screws and 4 M2 nuts (`screws()`). The part codes (17.1 on the tray, 18.1e-3 on the face) are left out, as in
guide_render_b.py; no part geometry changes. The breakaway frame tie (a print aid) and the cables are not drawn.

Camera: C3's (orthographic 3/4 iso, keyring flat, face up, loop LEFT, USB-C RIGHT, seen from above and from the long
side without the side button). Every part is moved by one translation into C3's frame: X along the length (0 = the
USB-C end), Y up (0 = the back), Z across (0 = the side without the button).

    cd hardware/raily-pin-public/case
    DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib python atlas_layers_p1.py <new_out_dir> [--cache file.pkl]

It refuses to write into a folder that already has files. --cache keeps the CAD stage (parts, hidden lines, meshes:
several minutes) in a pickle and reuses it when the file exists: a pickle runs code when loaded, so point --cache only
at a file this script wrote on your machine. Needs CadQuery, numpy, scipy, Pillow, matplotlib
and cairosvg (the venv of README.md).
"""
import io
import json
import math
import os
import pickle
import re
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ARGS = [a for a in sys.argv[1:] if not a.startswith('--')]
CACHE = None
for i_, a_ in enumerate(sys.argv):
    if a_ == '--cache' and i_ + 1 < len(sys.argv):
        CACHE = sys.argv[i_ + 1]
        ARGS = [a for a in ARGS if a != CACHE]

SCALE = 10.0          # px per mm (as C3)
MARGIN = 40.0         # px
STROKE = 1.4          # px
INK = '#1d2433'
SS = 4                # z-buffer samples per pixel side
GAP = 4.0             # mm of air between a part and the highest part under it in the exploded state
GAPS = {'frame-18.1e-B': 10.0, 'face-18.1e-B': 14.0}   # more air under the frame and the face: the eye sees the electronics, then the frame plate and its dowels
N = (0.30, 0.95, -0.85)    # C3's camera: from the keyring toward the eye


def norm(v):
    m = math.sqrt(sum(c * c for c in v))
    return tuple(c / m for c in v)


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


NV = norm(N)
VX = norm(tuple(-1.0 * (1 if i == 0 else 0) - dot((-1, 0, 0), NV) * NV[i] for i in range(3)))
VY = cross(NV, VX)


def proj(p):
    return (dot(p, VX), dot(p, VY))


# ---------------------------------------------------------------- parts (CAD stage)
# (id, explode direction along y (0 = stays), what it is, source)
LAYERS = [
    ('tray-17.1', 0, 'tray 17.1 with the side-button tongue and the key loop (one print, the back colour)',
     "keyring_case.parts(version_p('v1.3')): tray + button + loop"),
    ('cell-602030', +1, 'LiPo 602030 with PCM: the largest listed envelope 20.5 x 32 x 6.7, in its bay',
     'keyring_case.lipo(): DATASHEET lipo envelope (EEMB LP602030), r1 corners'),
    ('shelf-17.3c', +1, 'shelf 17.3c: plate, fence, three pads and crush-rib pins for the DA7280, up arrow deboss',
     'keyring_case.shelf_17_3c()'),
    ('da7280-lra', +1, 'SparkFun Qwiic Haptic Driver DA7280 (ROB-17590) with its LRA and the two Qwiic sockets J1 / J2',
     'keyring_case.da7280_place() via shelf_17_3c() (the DA7280 dict: board, LRA, J1, J2) + its 3 holes (Ø3.048)'),
    ('pcb-xiao-expansion', +1, 'XIAO nRF52840 Sense on its 2 x 7 headers on the XIAO Expansion Board',
     'keyring_case.board_parts(): Seeed STEPs (expansion.step, xiao.step) + 2 x 7 headers (guide/cad/render.py)'),
    ('hub-grove-qwiic', +1, 'Seeed Grove-Qwiic Hub (103020292) on the display',
     'guide/cad/parts.py qwiic_hub() at the guide B place (x 141.3-166.7, z -8.6..9.2, on the display at y -0.6), '
     'turned 180 deg so its Grove socket faces the UART edge (assumption)'),
    ('nuts-m2', +1, '4 M2 nuts (ISO 4032), seated in the frame pillar slots when closed',
     'keyring_case.screws(): the nuts'),
    ('frame-18.1e-B', +1, 'cover frame 18.1e (edition B): lip ring, 0.8 plate, 4 screw pillars, dowel posts',
     "keyring_case.parts(version_p('v1.3-p18.1e'))['frame'] (frame_tie, a breakaway print aid, left out)"),
    ('dowels-9', +1, '9 round dowels Ø3 x 5 (0.3 end chamfers), seated in the frame',
     'p.extra_dowels (6) + p.short_dowels (3) of v1.3-p18.1e at split - frame hole depth; Ø3.0 x 5.0, 0.3 chamfers'),
    ('face-18.1e-B', +1, 'cover face 18.1e (edition B): the crown, the face colour',
     "keyring_case.parts(version_p('v1.3-p18.1e'))['cover']"),
    ('screws-m2x16', -1, '4 M2 x 16 countersunk screws (ISO 7046-1), from the back',
     'keyring_case.screws(): the screws'),
]
SUB = {'tray-17.1': ('tray', 'button', 'loop')}     # colour bodies inside one layer (V5 paints loop + button front)


def build_parts():
    """{layer id: [cq.Workplane, ...]} in the case model's frame, plus info for the README."""
    sys.path.insert(0, HERE)
    os.chdir(HERE)
    import keyring_case as K
    p17 = K.version_p('v1.3', closure='back', proto=None, proto_label=None)
    b17, info17 = K.parts(p17)
    p = K.version_p('v1.3-p18.1e', closure='back', proto=None, proto_label=None)
    assert p.motor_kind == 'da7280' and p.hold_down is False, 'not edition B'
    b, info = K.parts(p)
    S, inf = K.shelf_17_3c()                 # proto 17: its number is under the plate, out of this camera
    comps = inf['comps']
    board = comps['board']
    for hx, hz in inf['holes']:
        board = board.cut(K._cyl(hx, hz, K.DA7280['hole_d'] / 2, -50, 50))
    bp = K.board_parts()
    sys.path.insert(0, K.CAD)
    cwd = os.getcwd()
    os.chdir(K.CAD)
    try:
        import parts as PT
    finally:
        os.chdir(cwd)
    hub, _ = PT.qwiic_hub(141.3, -0.6, -8.6)
    hc = (141.3 + 25.4 / 2, 0.0, -8.6 + 17.8 / 2)
    hub = [w.rotate(hc, (hc[0], 1.0, hc[2]), 180) for w in hub]
    sc = K.screws(p, info)
    split = info['split']
    dowels = []
    for x, z, _ in p.extra_dowels:
        dowels.append((x, z, p.peg_frame_d))
    for i, (x, z, *_r) in enumerate(p.short_dowels):
        dowels.append((x, z, p.short_frame_d[i]))
    dw = []
    for x, z, d in dowels:
        y0 = split - d
        c = 0.3
        body = K._cyl(x, z, 1.5, y0 + c, y0 + 5.0 - c).union(K._cone(x, z, 2.4, 3.0, y0, y0 + c)).union(
            K._cone(x, z, 3.0, 2.4, y0 + 5.0 - c, y0 + 5.0))
        dw.append(body)
    out = {
        'tray-17.1': {'tray': [b17['tray']], 'button': [b17['button']], 'loop': [b17['loop']]},
        'cell-602030': {'': [K.lipo(p17)]},
        'shelf-17.3c': {'': [S]},
        'da7280-lra': {'': [board, comps['lra'], comps['j1'], comps['j2']]},
        'pcb-xiao-expansion': {'': [bp['expansion'], bp['headers'], bp['xiao']]},
        'hub-grove-qwiic': {'': hub},
        'nuts-m2': {'': sc[1::2]},
        'frame-18.1e-B': {'': [b['frame']]},
        'dowels-9': {'': dw},
        'face-18.1e-B': {'': [b['cover']]},
        'screws-m2x16': {'': sc[0::2]},
    }
    box = info['box']
    shift = (-box[0], -box[2], -box[4])
    meta = dict(box_model_mm=[round(v, 3) for v in box], split_model_y=split, shift_mm=[round(v, 4) for v in shift],
                dowels=[(round(x, 2), round(z, 2), d) for x, z, d in dowels], screw_holes=info['fastening']['holes'])
    return out, shift, meta


POLY_USED = []        # layers whose hidden lines came from the polygonal HLR (the exact one failed on them)


def hlr_lines(shape, cq, tag=None):
    """Visible sharp edges + silhouettes (no smooth seams), as C3: OCCT's exact HLR. If it returns nothing (it fails
    on one B-spline face of face 18.1e at model x 120.6, y 12.75, z 24.5, just above the parting line at the
    USB-end, button-side corner, and then on any compound holding it), OCCT's polygonal HLR on a 0.01 mm
    mesh of the same shape."""
    from OCP.BRepAdaptor import BRepAdaptor_Curve
    from OCP.BRepMesh import BRepMesh_IncrementalMesh
    from OCP.GCPnts import GCPnts_QuasiUniformDeflection
    from OCP.gp import gp_Ax2, gp_Dir, gp_Pnt
    from OCP.HLRAlgo import HLRAlgo_Projector
    from OCP.HLRBRep import HLRBRep_Algo, HLRBRep_HLRToShape, HLRBRep_PolyAlgo, HLRBRep_PolyHLRToShape
    from OCP.TopAbs import TopAbs_EDGE
    from OCP.TopExp import TopExp_Explorer
    from OCP.TopoDS import TopoDS

    def collect(h):
        lines = []
        for comp in (h.VCompound(), h.OutLineVCompound()):
            if comp.IsNull():
                continue
            ex = TopExp_Explorer(comp, TopAbs_EDGE)
            while ex.More():
                e = TopoDS.Edge_s(ex.Current())
                c = BRepAdaptor_Curve(e)
                pts = GCPnts_QuasiUniformDeflection(c, 0.01, c.FirstParameter(), c.LastParameter())
                if pts.IsDone() and pts.NbPoints() > 1:
                    lines.append([(pts.Value(i + 1).X(), pts.Value(i + 1).Y()) for i in range(pts.NbPoints())])
                ex.Next()
        return lines

    proj_ = HLRAlgo_Projector(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(*NV), gp_Dir(*VX)))
    hlr = HLRBRep_Algo()
    hlr.Add(shape.wrapped)
    hlr.Projector(proj_)
    hlr.Update()
    hlr.Hide()
    lines = collect(HLRBRep_HLRToShape(hlr))
    if lines:
        return lines
    BRepMesh_IncrementalMesh(shape.wrapped, 0.01, False, 0.1, True)
    pa = HLRBRep_PolyAlgo(shape.wrapped)
    pa.Projector(proj_)
    pa.Update()
    h = HLRBRep_PolyHLRToShape()
    h.Update(pa)
    POLY_USED.append(tag)
    return collect(h)


def cad_stage():
    """Every layer's hidden lines (its own HLR), solids' boxes and meshes, the closed assembly's lines, and the
    assembled-at-explode lines are computed later (they need the offsets): the compound is kept as BREP text."""
    import cadquery as cq
    parts, shift, meta = build_parts()
    res = dict(meta=meta, layers={})
    allsol = []
    for lid, subs in parts.items():
        sols = {}
        for sub, ws in subs.items():
            ss = []
            for w in ws:
                for v in w.vals():
                    for s in (v.Solids() if hasattr(v, 'Solids') else [v]):
                        ss.append(s.translate(cq.Vector(*shift)))
            sols[sub] = ss
        flat = [s for ss in sols.values() for s in ss]
        allsol += flat
        comp = cq.Compound.makeCompound(flat)
        lines = hlr_lines(comp, cq, lid)
        meshes = {}
        for sub, ss in sols.items():
            V, T = [], []
            for s in ss:
                vs_, tris = s.tessellate(0.05, 0.3)
                o = len(V)
                V += [(v.x, v.y, v.z) for v in vs_]
                T += [(a + o, b_ + o, c + o) for a, b_, c in tris]
            meshes[sub] = (V, T)
        boxes = []
        for s in flat:
            bb = s.BoundingBox()
            boxes.append((bb.xmin, bb.xmax, bb.ymin, bb.ymax, bb.zmin, bb.zmax))
        bb = comp.BoundingBox()
        buf = io.BytesIO()
        comp.exportBrep(buf)
        brep = buf.getvalue()
        res['layers'][lid] = dict(lines=lines, meshes=meshes, boxes=boxes, brep=brep,
                                  bbox=(bb.xmin, bb.xmax, bb.ymin, bb.ymax, bb.zmin, bb.zmax))
        print(lid, len(lines), 'lines', sum(len(m[1]) for m in meshes.values()), 'triangles', flush=True)
    res['assembled_lines'] = hlr_lines(cq.Compound.makeCompound(allsol), cq, 'assembled')
    res['poly_hlr'] = list(POLY_USED)
    return res


def load_brep(data):
    import cadquery as cq
    return cq.Shape.importBrep(io.BytesIO(data))


# ---------------------------------------------------------------- explode
def overlap_xz(a, b, tol=0.3):
    return min(a[1], b[1]) - max(a[0], b[0]) > tol and min(a[5], b[5]) - max(a[4], b[4]) > tol


def explode_lifts(L):
    """Lift along y per layer, in layer order: the smallest lift that puts the layer's every solid GAP above every
    solid of the layers before it whose footprint (x-z box) it overlaps, at their own lifts; rounded up to 1 mm.
    The tray stays; the screws go down until their tips are GAP under the tray's back."""
    placed = []
    lifts = {}
    for lid, d, *_ in LAYERS:
        boxes = L[lid]['boxes']
        if d == 0:
            lift = 0.0
        elif d > 0:
            need = 0.0
            for (pb, plift) in placed:
                for b in boxes:
                    if overlap_xz(b, pb):
                        need = max(need, pb[3] + plift + GAPS.get(lid, GAP) - b[2])
            lift = float(math.ceil(need - 1e-6))
        else:
            tray_back = min(b[2] for b in L[LAYERS[0][0]]['boxes'])
            top = max(b[3] for b in boxes)
            lift = -float(math.ceil(top - tray_back + GAP - 1e-6))
        lifts[lid] = lift
        if d >= 0:
            placed += [(b, lift) for b in boxes]
    return lifts


# ---------------------------------------------------------------- rasters
def canvas(L, lifts):
    us, vs = [], []
    for lid, *_ in LAYERS:
        for k in (0.0, lifts[lid]):
            off = proj((0, k, 0))
            for ln in L[lid]['lines']:
                us += [u + off[0] for u, _ in ln]
                vs += [v + off[1] for _, v in ln]
    umin, umax, vmin, vmax = min(us), max(us), min(vs), max(vs)
    Wpx = round((umax - umin) * SCALE + 2 * MARGIN)
    Hpx = round((vmax - vmin) * SCALE + 2 * MARGIN)
    return Wpx, Hpx, umin, vmax


def depth_map(mesh, Wpx, Hpx, umin, vmax):
    """The mesh's nearest depth per z-buffer sample, cropped to its box: (x0, y0, D) with -inf where it is absent.
    Sample (i, j) of pixel (x, y) sits at x - 0.5 + (i + 0.5) / SS, as atlas_visible_layers.zbuffer."""
    V = np.asarray(mesh[0], dtype=float)
    T = np.asarray(mesh[1], dtype=np.int64)
    if len(T) == 0:
        return 0, 0, np.full((1, 1), -np.inf, np.float32)
    X = ((V @ np.array(VX) - umin) * SCALE + MARGIN + 0.5) * SS - 0.5
    Y = ((vmax - V @ np.array(VY)) * SCALE + MARGIN + 0.5) * SS - 0.5
    Z = V @ np.array(NV)
    gx0, gy0 = max(int(np.floor(X.min())), 0), max(int(np.floor(Y.min())), 0)
    gx1, gy1 = min(int(np.ceil(X.max())), Wpx * SS - 1), min(int(np.ceil(Y.max())), Hpx * SS - 1)
    D = np.full((gy1 - gy0 + 1, gx1 - gx0 + 1), -np.inf, np.float32)
    for a, b, c in T:
        xs, ys = (X[a], X[b], X[c]), (Y[a], Y[b], Y[c])
        den = (ys[1] - ys[2]) * (xs[0] - xs[2]) + (xs[2] - xs[1]) * (ys[0] - ys[2])
        if abs(den) < 1e-12:
            continue
        x0, x1 = max(int(np.ceil(min(xs))), gx0), min(int(np.floor(max(xs))), gx1)
        y0, y1 = max(int(np.ceil(min(ys))), gy0), min(int(np.floor(max(ys))), gy1)
        if x1 < x0 or y1 < y0:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1), np.arange(y0, y1 + 1))
        l0 = ((ys[1] - ys[2]) * (gx - xs[2]) + (xs[2] - xs[1]) * (gy - ys[2])) / den
        l1 = ((ys[2] - ys[0]) * (gx - xs[2]) + (xs[0] - xs[2]) * (gy - ys[2])) / den
        l2 = 1 - l0 - l1
        inside = (l0 >= -1e-9) & (l1 >= -1e-9) & (l2 >= -1e-9)
        z = (l0 * Z[a] + l1 * Z[b] + l2 * Z[c]).astype(np.float32)
        sub = D[y0 - gy0:y1 - gy0 + 1, x0 - gx0:x1 - gx0 + 1]
        win = inside & (z > sub)
        sub[win] = z[win]
    return gx0, gy0, D


def place(dm, Wpx, Hpx, dx=0, dy=0, dz=0.0):
    """A cropped depth map onto the full sample grid, shifted by (dx, dy) samples and dz in depth."""
    x0, y0, D = dm
    full = np.full((Hpx * SS, Wpx * SS), -np.inf, np.float32)
    h, w = D.shape
    X0, Y0 = x0 + dx, y0 + dy
    a0, b0 = max(Y0, 0), max(X0, 0)
    a1, b1 = min(Y0 + h, Hpx * SS), min(X0 + w, Wpx * SS)
    if a1 > a0 and b1 > b0:
        full[a0:a1, b0:b1] = D[a0 - Y0:a1 - Y0, b0 - X0:b1 - X0] + dz
    return full


def pix_share(sel, Wpx, Hpx):
    return sel.reshape(Hpx, SS, Wpx, SS).mean(axis=(1, 3))


def raster_fill(tris, to_px, Wpx, Hpx):
    from PIL import Image, ImageDraw, ImageFilter
    img = Image.new('L', (Wpx, Hpx), 0)
    dr = ImageDraw.Draw(img)
    for t in tris:
        dr.polygon([to_px(u, v) for u, v in t], fill=255)
    img = img.filter(ImageFilter.MaxFilter(3))      # grown 1 px so the fill reaches under its lines (as C3)
    return np.asarray(img, dtype=float)


def trace(a):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    Hpx, Wpx = a.shape
    fig = plt.figure()
    cs = plt.contour(np.arange(Wpx), np.arange(Hpx), a, levels=[127])
    segs = [seg.tolist() for seg in cs.allsegs[0] if len(seg) > 3]
    plt.close(fig)
    return segs


def ring_area(seg):
    x, y = np.array(seg).T
    return abs(np.dot(x, np.roll(y, 1)) - np.dot(y, np.roll(x, 1))) / 2


def drop_small(mask, min_px=6.0):
    """atlas_visible_layers.drop_small: rings under min_px px^2 (specks, pinholes) dropped, the mask follows."""
    from matplotlib.path import Path
    segs = trace(mask.astype(float) * 255)
    mask = mask.copy()
    for s in segs:
        if ring_area(s) < min_px:
            x, y = np.array(s).T
            gx, gy = np.meshgrid(np.arange(int(x.min()), int(x.max()) + 2), np.arange(int(y.min()), int(y.max()) + 2))
            inside = Path(s).contains_points(np.c_[gx.ravel(), gy.ravel()]).reshape(gx.shape)
            ok = (gy < mask.shape[0]) & (gx < mask.shape[1])
            mask[gy[inside & ok], gx[inside & ok]] ^= True
    return [s for s in segs if ring_area(s) >= min_px], mask


def clip_lines(lines, keep):
    """atlas_visible_layers.clip_lines: polylines (px) kept where `keep` holds, tested every 0.5 px."""
    Hpx, Wpx = keep.shape

    def ok(x, y):
        i, j = int(round(y)), int(round(x))
        return 0 <= i < Hpx and 0 <= j < Wpx and bool(keep[i, j])

    out = []
    for pts in lines:
        run = [pts[0]] if ok(*pts[0]) else []
        for p, q in zip(pts, pts[1:]):
            n = max(1, int(np.ceil(np.hypot(q[0] - p[0], q[1] - p[1]) / 0.5)))
            prev = ok(*p)
            for s in range(1, n + 1):
                r = (p[0] + (q[0] - p[0]) * s / n, p[1] + (q[1] - p[1]) * s / n)
                cur = ok(*r)
                if cur and not prev:
                    run = [r]
                elif cur and s == n:
                    run.append(r)
                elif prev and not cur:
                    run.append((p[0] + (q[0] - p[0]) * (s - 1) / n, p[1] + (q[1] - p[1]) * (s - 1) / n))
                    if len(run) > 1:
                        out.append(run)
                    run = []
                prev = cur
        if len(run) > 1:
            out.append(run)
    return out


def svg_text(Wpx, Hpx, segs, paths_px, fill=True):
    paths = ['M' + ' L'.join(f'{x:.2f},{y:.2f}' for x, y in ln) for ln in paths_px]
    head = f'<svg xmlns="http://www.w3.org/2000/svg" width="{Wpx}" height="{Hpx}" viewBox="0 0 {Wpx} {Hpx}">\n'
    body = ''
    if fill:
        sil_d = ' '.join('M' + ' L'.join(f'{x:.1f},{y:.1f}' for x, y in seg) + ' Z' for seg in segs)
        body += f'<path class="fill" d="{sil_d}" fill="#ffffff" fill-rule="evenodd" stroke="none"/>\n'
        body += (f'<g class="lines" fill="none" stroke="{INK}" stroke-width="{STROKE}" stroke-linecap="round" '
                 f'stroke-linejoin="round">\n<path d="{" ".join(paths)}"/>\n</g>\n')
    else:
        body += (f'<g fill="none" stroke="{INK}" stroke-width="{STROKE}" stroke-linecap="round" '
                 f'stroke-linejoin="round">\n<path d="{" ".join(paths)}"/>\n</g>\n')
    return head + body + '</svg>\n'


def bbox_of(mask):
    ys, xs = np.nonzero(mask)
    return [int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())] if len(xs) else None


def ndimage_ok(leak):
    from scipy import ndimage
    return bool(ndimage.binary_erosion(leak, iterations=1).any())


def grow(m, n):
    from PIL import Image, ImageFilter
    return np.asarray(Image.fromarray(m.astype(np.uint8) * 255).filter(ImageFilter.MaxFilter(2 * n + 1))) > 127


# ---------------------------------------------------------------- main
def main():
    if not ARGS:
        raise SystemExit(__doc__)
    OUT = os.path.abspath(ARGS[0])
    if os.path.isdir(OUT) and os.listdir(OUT):
        raise SystemExit(f'{OUT} has files: give a new folder (this script never overwrites)')
    if CACHE and os.path.exists(CACHE):
        R = pickle.load(open(CACHE, 'rb'))
    else:
        R = cad_stage()
        if CACHE:
            pickle.dump(R, open(CACHE, 'wb'))
    os.makedirs(OUT, exist_ok=True)
    L = R['layers']
    ids = [lid for lid, *_ in LAYERS]
    if not all(L[lid]['lines'] for lid in ids) or not R['assembled_lines']:     # a cache from before the fallback
        import cadquery as cq
        for lid in ids:
            if not L[lid]['lines']:
                L[lid]['lines'] = hlr_lines(load_brep(L[lid]['brep']), cq, lid)
        if not R['assembled_lines']:
            R['assembled_lines'] = hlr_lines(cq.Compound.makeCompound([load_brep(L[lid]['brep']) for lid in ids]), cq,
                                             'assembled')
        R['poly_hlr'] = sorted(set(R.get('poly_hlr', [])) | set(POLY_USED))
        if CACHE:
            pickle.dump(R, open(CACHE, 'wb'))
    lifts = explode_lifts(L)
    Wpx, Hpx, umin, vmax = canvas(L, lifts)

    def to_px(u, v):
        return ((u - umin) * SCALE + MARGIN, (vmax - v) * SCALE + MARGIN)

    def off_px(lift):
        u, v = proj((0, lift, 0))
        return (round(u * SCALE, 1), round(-v * SCALE, 1))

    # depth maps per colour body (closed), the layer's silhouette, its lines in px
    dms, fills = {}, {}
    for lid in ids:
        ly = L[lid]
        dms[lid] = {sub: depth_map(m, Wpx, Hpx, umin, vmax) for sub, m in ly['meshes'].items()}
        tris = []
        for V, T in ly['meshes'].values():
            P2 = [proj(v) for v in V]
            tris += [[P2[a], P2[b], P2[c]] for a, b, c in T]
        fills[lid] = raster_fill(tris, to_px, Wpx, Hpx)
        ly['px'] = [[to_px(u, v) for u, v in ln] for ln in ly['lines']]
        print('raster', lid, flush=True)

    # ---- paint order from the exploded state: for every pair overlapping on screen, the nearer one paints later
    shifted = {}                            # per layer: (x0, y0, D) cropped to its box, at its exploded place
    for lid in ids:
        dx, dy = off_px(lifts[lid])
        sx, sy = int(round(dx * SS)), int(round(dy * SS))
        dz = np.float32(dot((0, lifts[lid], 0), NV))
        crops = list(dms[lid].values())
        x0 = min(c[0] for c in crops)
        y0 = min(c[1] for c in crops)
        x1 = max(c[0] + c[2].shape[1] for c in crops)
        y1 = max(c[1] + c[2].shape[0] for c in crops)
        D = np.full((y1 - y0, x1 - x0), -np.inf, np.float32)
        for cx, cy, C in crops:
            sub = D[cy - y0:cy - y0 + C.shape[0], cx - x0:cx - x0 + C.shape[1]]
            np.maximum(sub, C, out=sub)
        shifted[lid] = (x0 + sx, y0 + sy, D + dz)
    n = len(ids)
    front = np.zeros((n, n), np.int64)      # front[i, j]: samples where both are present and i is nearer
    for i in range(n):
        for j in range(i + 1, n):
            ax_, ay_, A_ = shifted[ids[i]]
            bx_, by_, B_ = shifted[ids[j]]
            x0, y0 = max(ax_, bx_), max(ay_, by_)
            x1, y1 = min(ax_ + A_.shape[1], bx_ + B_.shape[1]), min(ay_ + A_.shape[0], by_ + B_.shape[0])
            if x1 <= x0 or y1 <= y0:
                continue
            a = A_[y0 - ay_:y1 - ay_, x0 - ax_:x1 - ax_]
            b = B_[y0 - by_:y1 - by_, x0 - bx_:x1 - bx_]
            both = np.isfinite(a) & np.isfinite(b)
            front[i, j] = int((both & (a > b + 1e-4)).sum())
            front[j, i] = int((both & (b > a + 1e-4)).sum())
    after = {i: {j for j in range(n) if j != i and front[i, j] > front[j, i]} for i in range(n)}   # i after j
    order, left, cycles = [], set(range(n)), []
    while left:
        ready = [i for i in left if not (after[i] & left)]
        if not ready:                       # a cycle: take the one with the least contested wins
            i = min(left, key=lambda k: sum(front[j, k] for j in after[k] & left))
            cycles.append([ids[k] for k in left])
            ready = [i]
        ready.sort()                        # explode order among equals
        order.append(ready[0])
        left.discard(ready[0])
    z_index = {ids[k]: zi for zi, k in enumerate(order)}
    contested = [dict(a=ids[i], b=ids[j], a_in_front_samples=int(front[i, j]), b_in_front_samples=int(front[j, i]))
                 for i in range(n) for j in range(i + 1, n) if front[i, j] and front[j, i]]

    # ---- layer SVGs
    meta = dict(version='P1-B', units='mm', px_per_mm=SCALE, canvas_px=[Wpx, Hpx],
                camera=dict(kind='orthographic 3/4 iso', toward_eye=list(NV), screen_x=list(VX), screen_y=list(VY),
                            note='keyring flat, face up, loop left, USB-C right (the shared atlas camera, unchanged)'),
                frame='case mm: X along the length (0 = USB-C end), Y up (0 = back), Z across (0 = side without the '
                      'button); the model frame of keyring_case.py moved by shift_mm',
                shift_mm=R['meta']['shift_mm'], origin_px=list(to_px(0, 0)), explode_gap_mm=GAP,
                explode_gap_overrides_mm=GAPS,
                explode_order='back to front: tray, cell, shelf, DA7280, main board, hub, nuts, frame, dowels, face; '
                              'screws from the back (down)',
                parts=[])
    for k, (lid, d, what, src) in enumerate(LAYERS):
        ly = L[lid]
        segs = trace(fills[lid])
        ly['segs'] = segs
        fn = f'{k}-{lid}.svg'
        open(os.path.join(OUT, fn), 'w').write(svg_text(Wpx, Hpx, segs, ly['px']))
        pu = [p[0] for ln in ly['px'] for p in ln]
        pv = [p[1] for ln in ly['px'] for p in ln]
        b = ly['bbox']
        lift = lifts[lid]
        dirv = [0, d, 0]
        meta['parts'].append(dict(
            id=lid, file=fn, of_part=lid, what=what, source=src, explode_order=k, z_index=z_index[lid],
            assembled_offset_mm=[0, 0, 0], assembled_offset_px=[0, 0],
            bbox_mm=dict(x=[round(b[0], 2), round(b[1], 2)], y=[round(b[2], 2), round(b[3], 2)],
                         z=[round(b[4], 2), round(b[5], 2)]),
            bbox_px=[round(min(pu), 1), round(min(pv), 1), round(max(pu), 1), round(max(pv), 1)],
            explode_dir_mm=dirv, explode_dir_px=[round(v * d, 3) for v in off_px(1.0)],
            suggested_exploded_offset_mm=[0.0, lift, 0.0], suggested_exploded_offset_px=list(off_px(lift))))
        print('svg', fn, flush=True)
    meta['paint_order'] = [ids[k] for k in order]
    meta['paint_order_contested_pairs'] = contested
    meta['paint_order_cycles'] = cycles
    asm_px = [[to_px(u, v) for u, v in ln] for ln in R['assembled_lines']]
    open(os.path.join(OUT, 'assembled.svg'), 'w').write(svg_text(Wpx, Hpx, [], asm_px, fill=False))
    meta['assembled_file'] = 'assembled.svg'

    # ---- visible layers (closed): owner per pixel over every colour body
    bodies = [(lid, sub) for lid in ids for sub in dms[lid]]
    zb = np.full((Hpx * SS, Wpx * SS), -np.inf, np.float32)
    idb = np.full((Hpx * SS, Wpx * SS), -1, np.int16)
    for k, (lid, sub) in enumerate(bodies):
        f = place(dms[lid][sub], Wpx, Hpx)
        win = f > zb
        zb[win] = f[win]
        idb[win] = k
    del zb
    part_of = np.array([ids.index(lid) for lid, _ in bodies])
    pid = np.where(idb >= 0, part_of[np.maximum(idb, 0)], -1)
    shares = np.stack([pix_share(pid == k, Wpx, Hpx) for k in range(n)])
    for _ in range(4):                   # the 1 px fringe no sample reaches: the most common owner around it
        empty = shares.max(axis=0) == 0
        if not empty.any():
            break
        pad = np.pad(shares, ((0, 0), (1, 1), (1, 1)))
        near = sum(pad[:, 1 + dy:1 + dy + Hpx, 1 + dx:1 + dx + Wpx] for dy in (-1, 0, 1) for dx in (-1, 0, 1))
        shares[:, empty] = near[:, empty] * 1e-3
    own = shares.argmax(axis=0)
    covered_any = shares.max(axis=0) > 0
    visible, vis_masks = [], {}
    for k, lid in enumerate(ids):
        full = fills[lid] > 127
        segs, mask = drop_small(full & (own == k) & covered_any)
        if mask.sum() == 0:
            continue
        keep = grow(mask, 1)
        fn = f'{ids.index(lid)}-{lid}-visible.svg'
        open(os.path.join(OUT, fn), 'w').write(svg_text(Wpx, Hpx, segs, clip_lines(asm_px, keep)))
        vis_masks[lid] = mask
        visible.append(dict(id=f'{lid}-visible', of_part=lid, file=fn, z_index=z_index[lid],
                            hidden_px_removed=int((full & ~mask).sum()), area_px=int(mask.sum()),
                            bbox_px=bbox_of(mask)))
    # the tray's colour bodies (V5 paints the loop and the button tongue in the face colour): overlays inside tray-visible
    missing = [k for k in ('tray-17.1', 'face-18.1e-B') if k not in vis_masks]
    if missing:                          # the colour masks and checks below need both
        raise SystemExit(f'closed view: nothing of {missing} is seen; check the camera and the parts')
    tk = ids.index('tray-17.1')
    tray_vis = vis_masks['tray-17.1']
    for sub in ('loop', 'button'):
        bk = bodies.index(('tray-17.1', sub))
        sh = pix_share(idb == bk, Wpx, Hpx) > 0.5
        m = sh & tray_vis
        segs, m = drop_small(m)
        if not m.any():
            meta['not_seen_when_closed'] = meta.get('not_seen_when_closed', []) + [
                f'tray-17.1 {sub}: on the far (+Z) wall, not seen from this camera; no -visible file']
            continue
        fn = f'{tk}-tray-17.1-{sub}-visible.svg'
        open(os.path.join(OUT, fn), 'w').write(svg_text(Wpx, Hpx, segs, clip_lines(asm_px, grow(m, 1))))
        vis_masks[f'tray-{sub}'] = m
        visible.append(dict(id=f'tray-17.1-{sub}-visible', of_part='tray-17.1', file=fn, z_index=z_index['tray-17.1'],
                            overlay=True, area_px=int(m.sum()), bbox_px=bbox_of(m),
                            purpose=f'the tray\'s {sub} as seen closed, a subset of tray-17.1-visible: paint it over '
                                    f'the tray in the face colour for the V5 colourway (V1 keeps it in the back colour)'))
    # openings: every pixel where an inside part is what is seen (through the USB-C window, the switch slot, ...)
    outside = {'tray-17.1', 'face-18.1e-B', 'frame-18.1e-B', 'screws-m2x16'}
    inside_k = [k for k, lid in enumerate(ids) if lid not in outside]
    om = np.isin(own, inside_k) & covered_any & (shares.max(axis=0) > 0.25)
    segs, om = drop_small(om, 2.0)
    open(os.path.join(OUT, 'openings.svg'), 'w').write(svg_text(Wpx, Hpx, segs, clip_lines(asm_px, grow(om, 1))))
    visible.append(dict(id='openings', of_part=None, file='openings.svg', z_index=max(z_index.values()) + 1,
                        area_px=int(om.sum()), bbox_px=bbox_of(om),
                        parts_seen=sorted({ids[k] for k in inside_k if ((own == k) & om).any()}),
                        purpose='the inside parts seen through the case openings in the closed view (the union of '
                                'their -visible layers), to paint dark if wanted'))
    meta['visible_layers'] = visible
    meta['fully_hidden_when_closed'] = [lid for lid in ids if lid not in vis_masks]
    meta['note'] = (
        'Every SVG shares one canvas: a part sits at its assembled place, so stack them at offset 0 for the assembled '
        'view; translate each by suggested_exploded_offset_px (scaled by the scroll progress) to explode. '
        'explode_dir_px is the screen motion per mm along explode_dir_mm. Paint by z_index (paint_order: the screws '
        'sit under the back, so they paint first). Each SVG has a white fill under its lines so a layer hides the ones '
        'painted before it. Layers cannot hide each other correctly where parts nest (the tray walls in front of the '
        'boards): for the closed state use assembled.svg, the whole assembly with true hidden lines, and cross-fade to '
        'the layers as the explode starts.')
    meta['explode_note'] = (
        'Every part moves along +Y (up, toward the top of the screen) except the screws (down). Its lift is the '
        f'smallest that puts each of its solids {GAP:g} mm (explode_gap_mm; explode_gap_overrides_mm for the frame and '
        'the face) above every solid of the parts before it in explode order whose x-z footprint it overlaps, at '
        'their own lifts, rounded up to 1 mm. Parts side by side share a level (the main board at the USB-C end, the '
        'cell at the loop end); the nuts stay on the screw axes, between the boards and the frame; the screws drop '
        f'until their tips are {GAP:g} mm under the tray back. Lifts (mm): '
        + ', '.join(f'{lid} {lifts[lid]:g}' for lid in ids) + '. z_index comes from a z-buffer of the exploded state: '
        'for every pair of parts that overlap on screen, the one in front paints later '
        f'({len(contested)} contested pairs, {len(cycles)} cycles).')
    meta['visible_layers_note'] = (
        'Assembled (closed) view only. Each *-visible.svg is that part as seen in the closed case: its own silhouette '
        'minus every pixel where another part is in front (z-buffer over all parts, 4x4 samples per pixel; each pixel '
        'belongs to one part), with the lines of assembled.svg kept only where that part is seen. 0-tray-17.1-visible, '
        '4-pcb-xiao-expansion-visible (the XIAO USB-C receptacle in the USB-C window and the power switch lever in its '
        'slot) and 9-face-18.1e-B-visible tile the closed silhouette without overlapping, so they can be painted in '
        'any order: tray in the back colour, face in the face colour, the board in a dark colour (openings.svg is the '
        'same pixels as the board tile). 0-tray-17.1-loop-visible.svg is an overlay inside the tray tile: paint it '
        'over the tray in the face colour for the V5 colourway. The frame (face colour), the nuts, dowels, shelf, '
        'cell, DA7280, hub and screws are not seen in the closed case (fully_hidden_when_closed). For the explode keep '
        'the full layers (parts list), since a moved part shows its hidden pieces. One camera, one visible set.')
    json.dump(meta, open(os.path.join(OUT, 'layers.json'), 'w'), indent=1)

    # ---- composites
    import cairosvg

    def composite(name, offsets, lines_only=None):
        """Rendered from the exported SVGs themselves (their even-odd fills keep holes open), each translated by its
        offset and painted by z_index; lines_only: one line set at the same stroke."""
        if lines_only is not None:
            doc = svg_text(Wpx, Hpx, [], lines_only, fill=False)
        else:
            body = ''
            for lid, pm in sorted(zip(ids, meta['parts']), key=lambda t: z_index[t[0]]):
                ox, oy = offsets.get(lid, (0, 0))
                inner = re.sub(r'^<svg[^>]*>\n|</svg>\n$', '', open(os.path.join(OUT, pm['file'])).read())
                body += f'<g transform="translate({ox} {oy})">\n{inner}</g>\n'
            doc = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{Wpx}" height="{Hpx}" viewBox="0 0 {Wpx} {Hpx}">\n'
                   + body + '</svg>\n')
        cairosvg.svg2png(bytestring=doc.encode(), write_to=os.path.join(OUT, name), background_color='white')

    exploded = {lid: off_px(lifts[lid]) for lid in ids}
    composite('composite-assembled.png', {}, lines_only=asm_px)
    composite('composite-assembled-layers.png', {})
    composite('composite-exploded.png', exploded)
    print('composites', flush=True)

    # ---- exploded truth: one HLR over every part at its exploded place, against the painted layers
    import cadquery as cq
    moved = [load_brep(L[lid]['brep']).translate(cq.Vector(0, lifts[lid], 0)) for lid in ids]
    ex_lines = hlr_lines(cq.Compound.makeCompound(moved), cq, 'exploded check')
    ex_px = [[to_px(u, v) for u, v in ln] for ln in ex_lines]
    composite('check-exploded-hlr.png', {}, lines_only=ex_px)
    from PIL import Image

    def ink_png(fn):
        a = np.asarray(Image.open(os.path.join(OUT, fn)).convert('L'))
        return a < 128
    lay_ink, hlr_ink = ink_png('composite-exploded.png'), ink_png('check-exploded-hlr.png')
    extra = lay_ink & ~grow(hlr_ink, 2)
    missing = hlr_ink & ~grow(lay_ink, 2)
    diff = np.full((Hpx, Wpx, 3), 255, np.uint8)
    diff[lay_ink & hlr_ink] = (190, 190, 190)
    diff[extra] = (220, 40, 40)
    diff[missing] = (30, 90, 220)
    Image.fromarray(diff).save(os.path.join(OUT, 'check-exploded-diff.png'))
    checks = dict(exploded_layers_vs_true_hlr=dict(
        ink_px_in_layers_not_within_2px_of_true_hlr=int(extra.sum()),
        ink_px_in_true_hlr_not_within_2px_of_layers=int(missing.sum()),
        note='red: lines the stacked layers show that one HLR pass over the exploded assembly hides (a paint-order or '
             'collision error); blue: lines the true HLR shows that the layers hide'))
    # collisions in the exploded state: solids of different layers that intersect (boxes first)
    coll = []
    for i, a in enumerate(ids):
        for b in ids[i + 1:]:
            hit = any(min(p[1], q[1]) > max(p[0], q[0]) and min(p[3] + lifts[a], q[3] + lifts[b]) >
                      max(p[2] + lifts[a], q[2] + lifts[b]) and min(p[5], q[5]) > max(p[4], q[4])
                      for p in L[a]['boxes'] for q in L[b]['boxes'])
            if hit:
                coll.append([a, b])
    checks['exploded_box_overlaps'] = coll

    # ---- flat-colour mask checks (closed)
    BACK, FRONT, DARK, BG = (0xF4, 0xEF, 0xEB), (0xAD, 0xB4, 0xE6), (40, 44, 52), (255, 255, 255)

    def alpha(fn):
        s = re.sub(r'<g class="lines".*?</g>', '', open(os.path.join(OUT, fn)).read(), flags=re.S)
        return np.asarray(Image.open(io.BytesIO(cairosvg.svg2png(bytestring=s.encode()))).convert('RGBA'))[..., 3] / 255
    lines_rgba = np.asarray(Image.open(io.BytesIO(cairosvg.svg2png(url=os.path.join(OUT, 'assembled.svg')))).convert('RGBA'))

    def comp(stack, bg=BG):
        img = np.zeros((Hpx, Wpx, 3)) + bg
        for fn, col in stack:
            a = alpha(fn)[..., None]
            img = img * (1 - a) + np.array(col) * a
        la = lines_rgba[..., 3:4] / 255
        img = img * (1 - la) + lines_rgba[..., :3] * la
        return img.astype(np.uint8)
    f = {lid: m['file'] for lid, m in zip(ids, meta['parts'])}
    v = {e['of_part'] if not e.get('overlay') else e['id']: e['file'] for e in visible if e['of_part']}
    before = comp([(f['tray-17.1'], BACK), (f['face-18.1e-B'], FRONT)])
    after = comp([(v['tray-17.1'], BACK), (v['face-18.1e-B'], FRONT), ('openings.svg', DARK)])
    tiles_stack = [(e['file'], FRONT if e['of_part'] in ('face-18.1e-B', 'frame-18.1e-B') else
                    (BACK if e['of_part'] in ('tray-17.1', 'screws-m2x16') else DARK))
                   for e in visible if e['of_part'] and not e.get('overlay')]
    tiles = comp(list(reversed(tiles_stack)), bg=(255, 0, 255))     # magenta shows any gap
    v5 = comp([(v['tray-17.1'], BACK)] + [(v[k], FRONT) for k in ('tray-17.1-loop-visible', 'tray-17.1-button-visible')
                                           if k in v] + [(v['face-18.1e-B'], FRONT), ('openings.svg', DARK)])
    Image.fromarray(before).save(os.path.join(OUT, 'check-visible-before.png'))
    Image.fromarray(after).save(os.path.join(OUT, 'check-visible-assembled.png'))
    Image.fromarray(tiles).save(os.path.join(OUT, 'check-visible-tiles.png'))
    Image.fromarray(v5).save(os.path.join(OUT, 'check-visible-v5.png'))
    covered = pix_share(idb >= 0, Wpx, Hpx) > 0.5
    a_t = {e['file']: alpha(e['file']) for e in visible if e['of_part'] and not e.get('overlay')}
    tsum = sum(a_t.values())
    fk, tk_ = ids.index('face-18.1e-B'), ids.index('tray-17.1')
    win_face = pix_share(pid == fk, Wpx, Hpx)
    win_tray = pix_share(pid == tk_, Wpx, Hpx)
    ff, fv = alpha(f['face-18.1e-B']) > 0.5, alpha(v['face-18.1e-B']) > 0.5
    tf, tv = alpha(f['tray-17.1']) > 0.5, alpha(v['tray-17.1']) > 0.5
    other_front_face = (win_face < 0.25) & covered
    other_front_tray = (win_tray < 0.25) & covered
    def interior(m):                     # more than 2 px inside: the 1 px grown fill and the stroke are edge noise
        return m & ~grow(~m, 2)
    checks['masks'] = dict(
        gaps_closed_case_visible_tiles=int((covered & ~(tsum > 0.5)).sum()),
        px_where_visible_tiles_overlap=int((tsum > 1.1).sum()),
        note='colour leaks are counted more than 2 px inside the painted region (edge pixels are shared with the '
             'neighbour tile and sit under the 1.4 px stroke)',
        face_colour_where_another_part_is_in_front_full_face=int((interior(ff) & other_front_face).sum()),
        face_colour_where_another_part_is_in_front_face_visible=int((interior(fv) & other_front_face).sum()),
        back_colour_where_another_part_is_in_front_full_tray_under_face=int((interior(tf & ~ff) & ~grow(ff, 2)
                                                                              & other_front_tray).sum()),
        back_colour_where_another_part_is_in_front_tray_visible=int((interior(tv) & other_front_tray).sum()),
        openings_px=int(om.sum()))
    # the before / after crops where the full layers leak colour
    leak = (np.abs(before.astype(int) - after.astype(int)).sum(axis=2) > 30)
    crops = []
    if ndimage_ok(leak):
        from scipy import ndimage
        core = ndimage.binary_erosion(leak, iterations=1)     # not the 1-2 px outline differences
        lab, nl = ndimage.label(grow(core, 6))
        sizes = ndimage.sum(core, lab, range(1, nl + 1))
        for r in np.argsort(sizes)[::-1][:3]:
            if sizes[r] == 0:
                continue
            ys, xs = np.nonzero((lab == r + 1) & core)
            cx, cy = int(xs.mean()), int(ys.mean())
            x0, y0 = max(cx - 60, 0), max(cy - 45, 0)
            x0, y0 = min(x0, Wpx - 120), min(y0, Hpx - 90)
            crop = np.concatenate([before[y0:y0 + 90, x0:x0 + 120], np.full((90, 6, 3), 255, np.uint8),
                                   after[y0:y0 + 90, x0:x0 + 120]], axis=1)
            name = f'check-visible-crop-{len(crops) + 1}.png'
            Image.fromarray(crop).resize((crop.shape[1] * 4, 360), Image.NEAREST).save(os.path.join(OUT, name))
            crops.append(dict(file=name, at_px=[cx, cy], leak_px=int(sizes[r])))
    checks['before_after_crops'] = crops
    meta['checks'] = checks
    meta['model'] = R['meta']
    meta['polygonal_hlr'] = dict(used_for=sorted(set(R.get('poly_hlr', [])) | set(POLY_USED)), why=(
        'OCCT exact HLR returns nothing for face 18.1e (one B-spline face at model x 120.6, y 12.75, z 24.5, just above '
        'the parting line at the USB-end, button-side corner) and for any compound holding it; those line sets come from OCCT polygonal HLR on a 0.01 mm mesh'))
    json.dump(meta, open(os.path.join(OUT, 'layers.json'), 'w'), indent=1)
    print(json.dumps(dict(canvas=[Wpx, Hpx], lifts=lifts, z_index=z_index, cycles=cycles, checks=checks), indent=1))


if __name__ == '__main__':
    main()

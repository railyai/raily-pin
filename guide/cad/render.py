#!/usr/bin/env python3
"""CAD line art for the Raily Keyring guide.

Sources (Seeed Studio, CC BY-SA 4.0, printables.com 1336695 and 1336692):
  expansion.step  Seeed Studio Expansion Board Base for XIAO with Grove OLED
  xiao.step       XIAO nRF52840 v3
Headers and the soldering iron are parametric (2.54 mm pitch, 1 x 7).

Hidden-line removal with OpenCASCADE HLRBRep; visible sharp edges and
silhouettes are written as SVG polylines, so counts and positions come
straight from the CAD. Silkscreen and logos (zero-thickness solids) are
dropped. Coordinates: the models are y-up, board in the x-z plane.
"""
import math, sys
import cadquery as cq
from OCP.HLRBRep import HLRBRep_Algo, HLRBRep_HLRToShape
from OCP.HLRAlgo import HLRAlgo_Projector
from OCP.gp import gp_Ax2, gp_Pnt, gp_Dir
from OCP.BRepAdaptor import BRepAdaptor_Curve
from OCP.GCPnts import GCPnts_QuasiUniformDeflection
from OCP.TopExp import TopExp_Explorer
from OCP.TopAbs import TopAbs_EDGE
from OCP.TopoDS import TopoDS

INK = '#3c3c40'; BLUE = '#3d5afe'

# Inner socket rows (XIAO) on the Expansion Board, from the STEP: x 121.13..138.92, z centres -6.74 / 8.51, top y 2.9
SOCK_X0, SOCK_X1, SOCK_Y = 121.13, 138.92, 2.9
ROW_Z = (-6.74, 8.51)
PIN_X = [SOCK_X0 + (SOCK_X1 - SOCK_X0) / 2 + (i - 3) * 2.54 for i in range(7)]


def clean(shape, min_thick=0.05):
    keep = [v for v in shape.solids().vals() if min(v.BoundingBox().xlen, v.BoundingBox().ylen, v.BoundingBox().zlen) > min_thick]
    return cq.Workplane().add(cq.Compound.makeCompound(keep))


def expansion():
    return clean(cq.importers.importStep('expansion.step'))


def headers(y0, below=3.0, above=3.0, spacer=True, pins=True):
    """Two 1 x 7 male headers: spacer 2.54 tall from y0, pins 0.64 square.

    Owner decision 2026-09-28: the strips go in SHORT end down. A strip has a ~3 mm short end and a
    ~6 mm long end; the short end goes down (into the sockets or a breadboard), the XIAO sits on the
    long end. The default (3 mm below, 3 mm above the spacer) is the finished, clipped part: the tails
    stand 1.8 mm over the XIAO. above=6.0 is the long end before clipping; below=6.0 draws the WRONG
    way round. `above` may be a function (row index, pin index) -> mm, for part-clipped strips."""
    parts = []
    for ri, z in enumerate(ROW_Z):
        parts.append(cq.Workplane().box(17.78, 2.54, 2.54, centered=(True, False, True)).translate(((SOCK_X0 + SOCK_X1) / 2, y0, z)))
        for i, x in enumerate(PIN_X):
            up = above(ri, i) if callable(above) else above
            parts.append(cq.Workplane().box(0.64, below + 2.54 + up, 0.64, centered=(True, False, True)).translate((x, y0 - below, z)))
    w = parts[0]
    for p in parts[1:]:
        w = w.add(p)
    return w


def xiao(y_bottom):
    """XIAO turned 180 degrees about y so its USB-C overhangs the board edge at x = 119.5, pads over the sockets.
    The PCB solid is swapped for xiao-pcb-clean.step: the same board with its raised silkscreen text defeatured."""
    raw = [s for c in clean(cq.importers.importStep('xiao.step')).vals() for s in c.Solids()]
    pcb = max(raw, key=lambda v: v.Volume())
    clean_pcb = cq.importers.importStep('xiao-pcb-clean.step').val()
    x = cq.Workplane().add(cq.Compound.makeCompound([clean_pcb] + [v for v in raw if v is not pcb]))
    x = x.rotate((0, 0, 0), (0, 1, 0), 180)
    # after rotation: pad x span -9.36..5.88 (centre -1.74), pad rows z 14.27 / -2.04 (centre 6.11); board bottom y -0.2
    return x.translate(((SOCK_X0 + SOCK_X1) / 2 + 1.74, y_bottom + 0.2, (ROW_Z[0] + ROW_Z[1]) / 2 - 6.11))


def iron(tip, direction=(0.55, 0.75, -0.4)):
    """Soldering iron: conical tip, thin barrel, flared collar, wider grip."""
    d = cq.Vector(*direction).normalized(); t = cq.Vector(*tip)
    parts = [cq.Solid.makeCone(0.2, 0.75, 3.2, pnt=t, dir=d),                 # conical tip
             cq.Solid.makeCylinder(0.75, 5.5, pnt=t + d * 3.2, dir=d),         # barrel
             cq.Solid.makeCone(0.95, 2.0, 2.2, pnt=t + d * 8.7, dir=d),        # collar flare
             cq.Solid.makeCylinder(2.9, 13, pnt=t + d * 10.9, dir=d),          # grip
             cq.Solid.makeCylinder(3.2, 1.2, pnt=t + d * 12.2, dir=d)]         # grip ring
    return cq.Workplane().add(cq.Compound.makeCompound(parts))


def project(shapes, view, up=(0, 1, 0)):
    comp = cq.Compound.makeCompound([s for w in shapes for s in w.vals()])
    v = gp_Dir(*view)
    ax = gp_Ax2(gp_Pnt(0, 0, 0), v)
    # keep "up" on screen
    upv = cq.Vector(*up); vv = cq.Vector(*view).normalized()
    xdir = upv.cross(vv).normalized()
    ax = gp_Ax2(gp_Pnt(0, 0, 0), v, gp_Dir(xdir.x, xdir.y, xdir.z))
    proj = HLRAlgo_Projector(ax)
    algo = HLRBRep_Algo(); algo.Add(comp.wrapped); algo.Projector(proj); algo.Update(); algo.Hide()
    hl = HLRBRep_HLRToShape(algo)
    polylines = []
    for comp_ in (hl.VCompound(), hl.OutLineVCompound()):
        if comp_ is None or comp_.IsNull():
            continue
        ex = TopExp_Explorer(comp_, TopAbs_EDGE)
        while ex.More():
            e = TopoDS.Edge_s(ex.Current())
            c = BRepAdaptor_Curve(e)
            disc = GCPnts_QuasiUniformDeflection(c, 0.01)
            pts = [c.Value(disc.Parameter(i)) for i in range(1, disc.NbPoints() + 1)] if disc.IsDone() else []
            if len(pts) >= 2:
                polylines.append([(p.X(), -p.Y()) for p in pts])
            ex.Next()
    return polylines, (xdir, vv.cross(xdir) * -1 if False else None, ax)


def to2d(point, view, up=(0, 1, 0)):
    vv = cq.Vector(*view).normalized(); xdir = cq.Vector(*up).cross(vv).normalized(); ydir = vv.cross(xdir)
    p = cq.Vector(*point)
    return (p.dot(xdir), -p.dot(ydir))


def svg(polys, width_mm, label, overlay='', crop=None, pad=1.5, stroke=0.0024):
    xs = [x for pl in polys for x, _ in pl]; ys = [y for pl in polys for _, y in pl]
    x0, y0, x1, y1 = crop or (min(xs) - pad, min(ys) - pad, max(xs) + pad, max(ys) + pad)
    W, H = x1 - x0, y1 - y0
    sw = stroke * W
    d = ''.join('M' + ' L'.join(f'{x:.2f},{y:.2f}' for x, y in pl) for pl in polys)
    return (f'<svg class="iso" viewBox="{x0:.2f} {y0:.2f} {W:.2f} {H:.2f}" role="img" aria-label="{label}" xmlns="http://www.w3.org/2000/svg">'
            f'<path d="{d}" fill="none" stroke="{INK}" stroke-width="{sw:.3f}" stroke-linecap="round" stroke-linejoin="round"/>{overlay}</svg>'), (x0, y0, W, H)


# ---------- styled rendering: hierarchy, fills ----------
OBJ = '#3c3c40'; CTX = '#9a9aa0'; FILL_TOP = '#ffffff'; FILL_SIDE = '#f2f2f4'; FILL_SIDE2 = '#e8e8ec'


def _edges(comp_, ):
    out = []
    if comp_ is None or comp_.IsNull():
        return out
    ex = TopExp_Explorer(comp_, TopAbs_EDGE)
    while ex.More():
        e = TopoDS.Edge_s(ex.Current())
        c = BRepAdaptor_Curve(e)
        disc = GCPnts_QuasiUniformDeflection(c, 0.01)
        pts = [c.Value(disc.Parameter(i)) for i in range(1, disc.NbPoints() + 1)] if disc.IsDone() else []
        if len(pts) >= 2:
            out.append([(p.X(), -p.Y()) for p in pts])
        ex.Next()
    return out


def project_groups(groups, view, up=(0, 1, 0)):
    """groups: list of (name, [Workplane]). One HLR over everything; edges returned per group as (outline, internal)."""
    vv = cq.Vector(*view).normalized(); xdir = cq.Vector(*up).cross(vv).normalized()
    ax = gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(vv.x, vv.y, vv.z), gp_Dir(xdir.x, xdir.y, xdir.z))
    algo = HLRBRep_Algo()
    for _, ws in groups:
        algo.Add(cq.Compound.makeCompound([s for w in ws for s in w.vals()]).wrapped)
    algo.Projector(HLRAlgo_Projector(ax)); algo.Update(); algo.Hide()
    res = {}
    for i, (name, _) in enumerate(groups, start=1):
        algo.Select(i)
        hl = HLRBRep_HLRToShape(algo)
        res[name] = (_edges(hl.OutLineVCompound()), _edges(hl.VCompound()))
    algo.Select()
    return res


def _shade(col, f):
    return '#' + ''.join(f'{int(int(col[i:i + 2], 16) * f):02x}' for i in (1, 3, 5))


def fill_png(groups, view, box, px_per_mm, path, up=(0, 1, 0), shade=None):
    """Painter's-algorithm face fills: top-facing white, sides light grey. box = (x0, y0, W, H) in view units.
    shade: group name -> factor (< 1 darkens that group's fills, e.g. the black plastic of headers and sockets)."""
    from PIL import Image, ImageDraw
    vv = cq.Vector(*view).normalized(); xdir = cq.Vector(*up).cross(vv).normalized(); ydir = vv.cross(xdir)
    x0, y0, W, H = box
    img = Image.new('RGBA', (int(W * px_per_mm), int(H * px_per_mm)), (255, 255, 255, 0))
    d = ImageDraw.Draw(img)
    tris = []
    for gname, ws in groups:
        k = (shade or {}).get(gname, 1.0)
        for w in ws:
            for sol in w.vals():
                for f in sol.Faces():
                    try:
                        vs, ts = f.tessellate(0.05, 0.3)
                    except Exception:
                        continue
                    for a, b, c in ts:
                        A, B, Cc = vs[a], vs[b], vs[c]
                        n = (B - A).cross(Cc - A)
                        if n.Length < 1e-12:
                            continue
                        n = n.normalized()
                        if n.dot(vv) < 0:
                            n = n * -1
                        col = FILL_TOP if abs(n.dot(cq.Vector(*up))) > 0.8 else (FILL_SIDE if abs(n.dot(xdir)) > abs(n.dot(cq.Vector(*up).cross(xdir))) else FILL_SIDE2)
                        if k != 1.0:
                            col = _shade(col, k)
                        depth = (A + B + Cc).dot(vv) / 3
                        pts = [((p.dot(xdir) - x0) * px_per_mm, (-p.dot(ydir) - y0) * px_per_mm) for p in (A, B, Cc)]
                        tris.append((depth, pts, col))
    tris.sort(key=lambda t: t[0])
    for _, pts, col in tris:
        d.polygon(pts, fill=col)
    img.save(path)


def styled_svg(res, styles, label, png=None, overlay='', crop=None, pad=1.5, base=0.0024):
    """styles: name -> colour. Outline edges at base width, internal edges at 60 %."""
    allpts = [p for o, i in res.values() for pl in o + i for p in pl]
    xs = [p[0] for p in allpts]; ys = [p[1] for p in allpts]
    x0, y0, x1, y1 = crop or (min(xs) - pad, min(ys) - pad, max(xs) + pad, max(ys) + pad)
    W, H = x1 - x0, y1 - y0
    sw = base * W
    body = ''
    if png:
        body += f'<image href="{png}" x="{x0:.2f}" y="{y0:.2f}" width="{W:.2f}" height="{H:.2f}"/>'
    order = sorted(res, key=lambda n: styles[n] == OBJ)   # context first, object on top
    for name in order:
        outline, internal = res[name]; col = styles[name]
        for pls, w in ((internal, sw * 0.6), (outline, sw)):
            d = ''.join('M' + ' L'.join(f'{x:.2f},{y:.2f}' for x, y in pl) for pl in pls)
            if d:
                body += f'<path d="{d}" fill="none" stroke="{col}" stroke-width="{w:.3f}" stroke-linecap="round" stroke-linejoin="round"/>'
    return (f'<svg class="iso" viewBox="{x0:.2f} {y0:.2f} {W:.2f} {H:.2f}" role="img" aria-label="{label}" '
            f'xmlns="http://www.w3.org/2000/svg">{body}{overlay}</svg>'), (x0, y0, W, H)


def split_sockets(E):
    """Split the Expansion Board into (inner XIAO socket strips, everything else)."""
    inner, rest = [], []
    for c in E.vals():
        for s in c.Solids():
            b = s.BoundingBox()
            if 15 < b.xlen < 19 and b.zlen < 3.5 and abs(b.xmin - SOCK_X0) < 0.1 and any(abs((b.zmin + b.zmax) / 2 - z) < 0.2 for z in ROW_Z):
                inner.append(s)
            else:
                rest.append(s)
    return cq.Workplane().add(cq.Compound.makeCompound(inner)), cq.Workplane().add(cq.Compound.makeCompound(rest))

#!/usr/bin/env python3
"""Closure study (owner, 2026-09-28): keep the front clean.

    python closure_study.py            # -> preview/closure-*.png, print/closure-study.json

A  screws from the back (P.closure='back'): ISO 7046-1 M2 x 16 countersunk heads flush with the back,
   ISO 4032 M2 nuts in side slots of the cover pillars.
B  screwless snap-fit (P.closure='snap'): four cantilever latches in the long walls, bumps on the cover,
   a pry notch beside USB-C.
C  sliding lid on rails: a concept section only (not built), to show why it is not recommended.

Sections are line art with flat fills in the filament colours (tray Cotton White, cover Pastel Periwinkle,
hardware steel grey, board grey context). No text in any raster.
"""
import base64
import json
import os

import cadquery as cq

import keyring_case as K

WHITE = K.FILAMENTS['pm_cotton_white']['hex']
PERI = K.FILAMENTS['pm_pastel_periwinkle']['hex']
STEEL = '#8C9199'
BOARD = '#B9BDC4'


def figure(name, groups, view, up, crop=None, width=1600, pad=2.0):
    """groups: [(name, [Workplane], 'obj'|'ctx', colour)]. Writes preview/<name>.png."""
    import cairosvg
    R, cwd = K._render_module(); os.chdir(cwd)
    from OCP.BRepLib import BRepLib
    for _, ws, _, _ in groups:
        for w in ws:
            for v in w.vals():
                BRepLib.EncodeRegularity_s(v.wrapped, 1e-3)
    gs = [(n, ws) for n, ws, _, _ in groups]
    res = R.project_groups(gs, view, up)
    if crop is None:
        polys = [pl for o, i in res.values() for pl in o + i]
        xs = [x for pl in polys for x, _ in pl]; ys = [y for pl in polys for _, y in pl]
        crop = (min(xs) - pad, min(ys) - pad, max(xs) - min(xs) + 2 * pad, max(ys) - min(ys) + 2 * pad)
    os.makedirs(K.BUILD, exist_ok=True); os.makedirs(K.PREVIEW, exist_ok=True)
    png = os.path.join(K.BUILD, f'fill-{name}.png')
    shapes, cols = [], []
    for _, ws, _, c in groups:
        for w in ws:
            shapes.append(w); cols.append(c)
    K.shade_png(shapes, view, up, crop, max(12, 900 / crop[2]), png, colours=cols)
    href = 'data:image/png;base64,' + base64.b64encode(open(png, 'rb').read()).decode()
    styles = {n: (R.OBJ if kind == 'obj' else R.CTX) for n, _, kind, _ in groups}
    svg, _ = R.styled_svg(res, styles, f'Raily Keyring case, {name}', png=href,
                          crop=(crop[0], crop[1], crop[0] + crop[2], crop[1] + crop[3]), base=0.0022)
    path = os.path.join(K.BUILD, f'{name}.svg')
    open(path, 'w').write(svg)
    out = os.path.join(K.PREVIEW, f'{name}.png')
    cairosvg.svg2png(url=path, write_to=out, output_width=width, background_color='white')
    print(out)


def keep_below_x(ws, x, big=400.0):
    """Keep x <= plane (the cut face looks toward +x)."""
    box = K._box(x - big, x, -big, big, -big, big)
    out = []
    for w in ws:
        try:
            r = w.intersect(box)
            if r.vals():
                out.append(r)
        except Exception:
            pass
    return out


def board_near(x, dx=6.0):
    bp = K.board_parts()
    sols = [s for w in bp.values() for v in w.vals() for s in v.Solids()
            if s.BoundingBox().xmin < x and s.BoundingBox().xmax > x - dx]
    return cq.Workplane().add(cq.Compound.makeCompound(sols))


SECTION = ((1, 0, 0), (0, 1, 0))       # looking at the cut face from +x, front up, width across


def section(p, x, name, crop=None, with_board=True, extra=()):
    b, info = K.parts(p)
    tray = [b[k] for k in ('tray', 'button', 'loop') if k in b]
    cover = [b[k] for k in ('cover', 'led') if k in b]
    hw = K.screws(p, info)
    groups = [('tray', keep_below_x(tray, x), 'obj', WHITE), ('cover', keep_below_x(cover, x), 'obj', PERI)]
    if hw:
        groups.append(('hardware', keep_below_x(hw, x), 'obj', STEEL))
    if with_board:
        groups.append(('board', keep_below_x([board_near(x)], x), 'ctx', BOARD))
    groups += list(extra)
    figure(name, [g for g in groups if g[1]], *SECTION, crop=crop)
    return b, info


def elevation(p, name, view, up=(1, 0, 0)):
    b, info = K.parts(p)
    tray = [b[k] for k in ('tray', 'button', 'loop') if k in b]
    cover = [b[k] for k in ('cover', 'led') if k in b]
    hw = K.screws(p, info)
    groups = [('tray', tray, 'obj', WHITE), ('cover', cover, 'obj', PERI)]
    if hw:
        groups.append(('hardware', hw, 'obj', STEEL))
    figure(name, groups, view, up)


def concept_c(x_mid=160.0, slab=12.0):
    """C, section only: walls 3.0 with a rail groove 1.0 deep under the wall top, the lid an inset panel
    between the walls with a skirt and a tongue in each groove; no pillars (they would drag across the board)."""
    p = K.P(wall=3.0, closure='snap')
    body, info = K.outer_body(p)
    box = info['box']
    x0, x1, y0, y1, z0, z1 = K.packing(p)['cavity']
    split = y1
    cav = K.profile(p, box, y0).offset2D(-p.wall).extrude(y1 - y0)
    hollow = body.cut(cav)
    sl = K._box(x_mid - slab / 2, x_mid + slab / 2, -50, 50, -50, 50)
    hollow = hollow.intersect(sl)
    g0, g1, depth, skirt_t, clr = split - 1.2, split - 0.4, 1.0, 1.0, 0.15
    lid = hollow.intersect(K._box(x_mid - slab, x_mid + slab, split, 40, z0, z1))    # inset panel between the walls
    skirt = (K._box(x_mid - slab / 2, x_mid + slab / 2, split - 1.4, split + 0.01, z0 + clr, z0 + clr + skirt_t)
             .union(K._box(x_mid - slab / 2, x_mid + slab / 2, split - 1.4, split + 0.01, z1 - clr - skirt_t, z1 - clr)))
    tongue = (K._box(x_mid - slab / 2, x_mid + slab / 2, g0 + clr, g1 - clr, z0 - depth + clr, z0 + clr + 0.01)
              .union(K._box(x_mid - slab / 2, x_mid + slab / 2, g0 + clr, g1 - clr, z1 - clr - 0.01, z1 + depth - clr)))
    lid = lid.union(skirt).union(tongue)
    tray = hollow.cut(K._box(x_mid - slab, x_mid + slab, split, 40, z0, z1))
    tray = tray.cut(K._box(x_mid - slab, x_mid + slab, g0, g1, z0 - depth, z0)).cut(
        K._box(x_mid - slab, x_mid + slab, g0, g1, z1, z1 + depth))
    groups = [('tray', keep_below_x([tray], x_mid), 'obj', WHITE), ('cover', keep_below_x([lid], x_mid), 'obj', PERI),
              ('board', keep_below_x([board_near(x_mid)], x_mid), 'ctx', BOARD)]
    figure('closure-C-section', groups, *SECTION)
    W = box[5] - box[4]
    return dict(width=W, wall=p.wall)


def main():
    base = K.P(closure='front')
    _, bi = K.parts(base)
    W0 = bi['box'][5] - bi['box'][4]; D0 = bi['box'][3] - bi['box'][2]
    out = dict(reference=dict(width=W0, thickness_rim=D0))

    # A: section through the two screws at x = 173.49
    pA = K.P(closure='back')
    bA, iA = section(pA, 173.49, 'closure-A-section')
    section(pA, 173.49, 'closure-A-detail', crop=(-23.5, -13.5, 12.0, 22.5))
    elevation(pA, 'closure-A-back', K.BACK[0])
    elevation(pA, 'closure-A-front', K.FRONT[0])
    f = iA['fastening']
    out['A'] = dict(screws='4 x ISO 7046-1 M2 x %g countersunk, cross recess H0 (dk 3.8, k 1.2)' % pA.back_screw_len,
                    nuts='4 x ISO 4032 M2 (s 4.0, m 1.6) in side slots of the cover pillars',
                    nut_y=f['nut'], tip_y=f['tip'], front_plate_inner_y=iA['split'],
                    tip_to_front_plate=iA['split'] - f['tip'], boss_d=pA.nut_boss_d,
                    width=iA['box'][5] - iA['box'][4], thickness_rim=iA['box'][3] - iA['box'][2],
                    cover_cm3=round(sum(v.Volume() for v in bA['cover'].vals()) / 1000, 2))

    # B: section through the two latches at x = 178, detail of the +z latch, side elevation
    pB = K.P(closure='snap')
    bB, iB = section(pB, 178.0, 'closure-B-section')
    section(pB, 178.0, 'closure-B-detail', crop=(-28.6, -12.4, 7.0, 9.5), with_board=False)
    elevation(pB, 'closure-B-side', K.SIDE[0])
    elevation(pB, 'closure-B-otherside', (0, 0, -1))
    sm = K.snap_mechanics(pB)
    cover_g = sum(v.Volume() for v in bB['cover'].vals()) / 1000 * K.DATASHEET['pm_matte_density']
    out['B'] = dict(mechanics={k: (round(v, 3) if isinstance(v, float) else v) for k, v in sm.items()},
                    cover_g=round(cover_g, 1), pull_off_over_cover_weight=round(sm['pull_off_N'] / (cover_g / 1000 * 9.81)),
                    width=iB['box'][5] - iB['box'][4], thickness_rim=iB['box'][3] - iB['box'][2],
                    latches=[dict(side='+z' if L['s'] > 0 else '-z', bump_x=L['xb'], hinge_x=L['hinge'], free_x=L['free'])
                             for L in K.latches(pB)])
    # C: concept section
    out['C'] = concept_c()
    os.makedirs(os.path.join(K.HERE, 'print'), exist_ok=True)
    json.dump(out, open(os.path.join(K.HERE, 'print', 'closure-study.json'), 'w'), indent=1, default=float)
    print(json.dumps(out, indent=1, default=float))


if __name__ == '__main__':
    main()

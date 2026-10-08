#!/usr/bin/env python3
"""Assembly-guide line art from the case model (for the guide's case pages).

    python guide_figures.py            # -> guide-figures/*.{svg,png}

Figures: overview, exploded, closed, hanging; assembly steps step-1 ... step-6 for closure A (screws from the
back) and step-5b for closure B (snap-fit) (the part added in each step is dark, the rest grey, the new part
lifted above its seat); colour maps colour-v1 / colour-v5
(front and iso, every piece filled in its filament colour).

Same renderer and style as the guide's CAD figures (../guide/cad/render.py: one HLR pass,
object edges #3c3c40, context #9a9aa0, flat fills underneath). No text in any raster; the guide
adds its labels as vector overlays. Board drawings are adaptations of Seeed Studio models (CC BY-SA 4.0).
"""
import base64
import os
import shutil

import cadquery as cq

import keyring_case as K

OUT = os.path.join(K.HERE, 'guide-figures')
ISO = (0.45, 1.0, 0.9)
UP = (1, 0, 0)


def figure(name, groups, view=ISO, up=UP, width=1600, colours=None, frame=None):
    """groups: [(name, [Workplane], 'obj'|'ctx')]"""
    import cairosvg
    R, cwd = K._render_module(); os.chdir(cwd)
    from OCP.BRepLib import BRepLib
    for _, ws, _ in groups:
        for w in ws:
            for v in w.vals():
                BRepLib.EncodeRegularity_s(v.wrapped, 1e-3)
    gs = [(n, ws) for n, ws, _ in groups]
    res = R.project_groups(gs, view, up)
    polys = [pl for o, i in res.values() for pl in o + i]
    if frame is not None:        # crop to the projection of these solids (a close-up of part of the scene)
        import cadquery as cq_
        vv = cq_.Vector(*view).normalized(); xd = cq_.Vector(*up).cross(vv).normalized(); yd = vv.cross(xd)
        pts_ = [v.toTuple() for w in frame for s_ in w.vals() for v in s_.Vertices()]
        polys = [[(cq_.Vector(*q).dot(xd), -cq_.Vector(*q).dot(yd)) for q in pts_]]
    xs = [x for pl in polys for x, _ in pl]; ys = [y for pl in polys for _, y in pl]
    pad = 3.0 if frame is None else 0.5
    bx = (min(xs) - pad, min(ys) - pad, max(xs) - min(xs) + 2 * pad, max(ys) - min(ys) + 2 * pad)
    os.makedirs(K.BUILD, exist_ok=True)
    png = os.path.join(K.BUILD, f'fill-guide-{name}.png')
    K.shade_png([w for _, ws in gs for w in ws], view, up, bx, 10, png, colours=colours)
    href = 'data:image/png;base64,' + base64.b64encode(open(png, 'rb').read()).decode()
    styles = {n: (R.OBJ if kind == 'obj' else R.CTX) for n, _, kind in groups}
    svg, _ = R.styled_svg(res, styles, f'Raily Keyring case, {name}', png=href,
                          crop=(bx[0], bx[1], bx[0] + bx[2], bx[1] + bx[3]), base=0.0022)
    os.makedirs(OUT, exist_ok=True)
    open(os.path.join(OUT, f'{name}.svg'), 'w').write(svg)
    cairosvg.svg2png(url=os.path.join(OUT, f'{name}.svg'), write_to=os.path.join(OUT, f'{name}.png'),
                     output_width=width, background_color='white')
    print(os.path.join(OUT, f'{name}.png'))
    return bx


def moved(ws, dy=0.0, dx=0.0):
    return [w.translate((dx, dy, 0)) for w in ws]


def main(only=None):
    p = K.P()
    b, info = K.parts(p)
    bp = K.board_parts()
    board = [bp['expansion'], bp['headers'], bp['xiao']]
    cell, mot = K.lipo(p), K.motor(p)
    sc = K.screws(p, info)
    screws_, nuts_ = sc[0::2], sc[1::2]
    tray = [b[k] for k in ('tray', 'button', 'pad', 'loop') if k in b]
    cover = [b[k] for k in ('cover', 'zone1', 'zone2', 'led') if k in b]

    # overview: everything seated in the tray, cover off
    figure('overview', [('parts', board + [cell, mot], 'obj'), ('case', tray + [b['shelf']], 'ctx')])
    # exploded: along the screw axis (front up)
    figure('exploded', [('parts', moved(board, 18) + moved([cell], 10) + moved([b['shelf']], 22) + moved([mot], 30), 'obj'),
                        ('case', tray + moved(cover, 52) + moved(nuts_, 52) + moved(screws_, -24), 'ctx')],
           view=(0.25, 0.5, 1.0))
    # closed
    figure('closed', [('case', tray + cover + screws_, 'obj')])
    # hanging: a 25 mm split ring through the lug (ring plane contains the hole axis)
    lp = info['loop']
    box = info['box']
    yc, zc = (box[2] + box[3]) / 2, (box[4] + box[5]) / 2
    Rr, wire = 12.5 - 0.7, 0.7
    ring = cq.Workplane().add(cq.Solid.makeTorus(Rr, wire, pnt=cq.Vector(lp['hole_x'] + Rr - lp['hole_r'] + wire, yc, zc),
                                                 dir=cq.Vector(0, 0, 1)))
    figure('hanging', [('case', tray + cover + screws_, 'obj'), ('ring', [ring], 'ctx')])

    # assembly steps (closure A, screws from the back): the part added in the step is dark and lifted above
    # its seat, the rest grey. step-5b is the snap-fit alternate (closure B) for the same moment.
    SIDE = (0.25, 0.5, 1.0)
    BACK = (0.35, -1.0, 0.8)
    xmid = (info['box'][0] + info['box'][1]) / 2
    nuts_in = [n.translate((7.0 if hx < xmid else -7.0, 0, 0)) for n, (hx, hz) in zip(nuts_, info['fastening']['holes'])]
    figure('step-1', [('new', moved([cell], 14), 'obj'), ('rest', tray, 'ctx')], view=SIDE)                   # cell into the bay
    figure('step-2', [('new', moved([b['shelf'], mot], 16), 'obj'), ('rest', tray + [cell], 'ctx')], view=SIDE)  # shelf, motor
    figure('step-3', [('new', moved(board, 16), 'obj'), ('rest', tray + [cell, b['shelf'], mot], 'ctx')], view=SIDE)
    figure('step-4', [('new', nuts_in, 'obj'), ('rest', cover, 'ctx')], view=BACK)                            # nuts into the pillar slots
    figure('step-5', [('new', moved(cover + nuts_, 30), 'obj'), ('rest', tray + board + [b['shelf'], mot], 'ctx')], view=SIDE)
    figure('step-6', [('new', moved(screws_, -12), 'obj'), ('rest', tray + cover, 'ctx')], view=BACK)          # screws from the back
    pB = K.P(closure='snap')
    bB, _ = K.parts(pB)
    trayB = [bB[k] for k in ('tray', 'button', 'pad', 'loop') if k in bB]
    coverB = [bB[k] for k in ('cover', 'zone1', 'zone2', 'led') if k in bB]
    figure('step-5b', [('new', moved(coverB, 12), 'obj'), ('rest', trayB + board + [bB['shelf'], mot], 'ctx')], view=SIDE)

    # colour maps: every piece in its filament colour (front and iso)
    names = [n for n in ('tray', 'button', 'loop', 'cover', 'led') if n in b]
    for sc in K.SHIPPING:
        cols = [K.FILAMENTS[K.body_filament(p, n, sc)]['hex'] for n in names]
        cols += ['#8A8D93'] * len(screws_)
        pieces = [b[n] for n in names] + screws_
        figure(f'colour-{sc}-front', [('case', pieces, 'obj')], view=(0, 1, 0), colours=cols)
        figure(f'colour-{sc}-iso', [('case', pieces, 'obj')], view=ISO, colours=cols)


if __name__ == '__main__':
    main()

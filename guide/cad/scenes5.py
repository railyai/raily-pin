"""Bench scenes drawn from CAD: multimeter check, first charge in a LiPo pouch, the phone next to the keyring."""
import sys, math, cadquery as cq, render as R, parts as PT
out = sys.argv[1]; which = sys.argv[2:]
def to2(p, V, up=(0, 1, 0)): return R.to2d(p, V, up)
def txt(x, y, t, size=4.0, col='#1d1d1f', anchor='middle', weight=700):
    return f'<text x="{x:.2f}" y="{y:.2f}" font-family="Inter Guide, sans-serif" font-size="{size}" font-weight="{weight}" fill="{col}" text-anchor="{anchor}">{t}</text>'
def seg(p0, p1, V, col, w):
    (x0, y0), (x1, y1) = to2(p0, V), to2(p1, V)
    return f'<line x1="{x0:.2f}" y1="{y0:.2f}" x2="{x1:.2f}" y2="{y1:.2f}" stroke="{col}" stroke-width="{w}" stroke-linecap="round"/>'
def curve(pts, V, col, w):
    q = [to2(p, V) for p in pts]
    d = f'M{q[0][0]:.2f},{q[0][1]:.2f} ' + (f'Q{q[1][0]:.2f},{q[1][1]:.2f} {q[2][0]:.2f},{q[2][1]:.2f}' if len(q) == 3 else
                                             f'C{q[1][0]:.2f},{q[1][1]:.2f} {q[2][0]:.2f},{q[2][1]:.2f} {q[3][0]:.2f},{q[3][1]:.2f}')
    return f'<path d="{d}" fill="none" stroke="{col}" stroke-width="{w}" stroke-linecap="round"/>'
def render(name, grp, V, label, ov, margins, px=10, styles=None):
    res = R.project_groups(grp, V)
    st = styles or {g[0]: R.OBJ for g in grp}
    _, g = R.styled_svg(res, st, '', pad=0)
    l, t, r, b = margins
    crop = (g[0] - l, g[1] - t, g[0] + g[2] + r, g[1] + g[3] + b)
    svg, box = R.styled_svg(res, st, label, png=f'{name}-fill.png', overlay=ov, crop=crop)
    R.fill_png(grp, V, box, px, f'{out}/{name}-fill.png')
    open(f'{out}/{name}.svg', 'w').write(svg); print(name, 'ok', flush=True)
    return crop

if 'meter' in which:
    V = (-0.55, 1.3, 1)
    # multimeter lying flat, display toward the far end (-z), probe sockets at the near end
    body = cq.Workplane().box(42, 9, 80).edges('|Y').fillet(7).edges('>Y').fillet(1.5).translate((0, 4.5, 0))
    disp = cq.Workplane().box(32, 1.0, 20).edges('|Y').fillet(1.5).translate((0, 9.0, -24))
    body = body.cut(disp)
    dial = cq.Workplane('XZ').circle(10).extrude(-2.2).translate((0, 9.0 + 2.2, 4)).faces('>Y').edges().fillet(0.6)
    knob = cq.Workplane().box(3.0, 1.6, 16).edges('|Y').fillet(1.2).translate((0, 12.0, 4))
    socks = [cq.Workplane('XZ').circle(2.6).extrude(-1.2).translate((dx, 9.0 + 1.2, 30)) for dx in (-9, 9)]
    keys = [cq.Workplane().box(9, 1.0, 4).edges('|Y').fillet(1.0).translate((dx, 9.5, -6)) for dx in (-13, 13)]
    # JST plug on the battery leads, its two contacts facing -x toward the probes
    px_, py_, pz_ = 45.0, 2.25, 14.0
    plug = PT.jst_plug(px_, py_, pz_, gap=0)
    ws = PT.jst_wire_starts(px_, py_, pz_, gap=0)
    lipo = cq.Workplane().box(24, 5, 30).edges('|Y').fillet(2.0).edges('>Y').fillet(0.8).translate((76, 2.5, 10))
    def w2(p0, p1, bend):
        pts = [cq.Vector(*p0), cq.Vector(*bend), cq.Vector(*p1)]
        prof = cq.Workplane(cq.Plane(origin=pts[0], normal=(pts[1] - pts[0]).normalized())).circle(0.5)
        return prof.sweep(cq.Workplane().spline(pts, includeCurrent=False))
    leads = [w2(ws[k], (64, 2.5, 10 + dz), (58, 2.0, pz_ + dz * 3)) for k, dz in (('+', 1.2), ('-', -1.2))]
    grp = [('meter', [body, dial, knob] + socks + keys), ('bat', [plug[0], lipo] + leads)]
    # probes as vector strokes: red tip on the + contact (A), black tip on the - contact
    ca, cb = (px_ + 0.4, py_, pz_ + 1.0), (px_ + 0.4, py_, pz_ - 1.0)
    def probe(tip, col, sock, side):
        back = (tip[0] - 14, tip[1] + 9, tip[2] + side * 7)
        grip = (tip[0] - 5, tip[1] + 3.2, tip[2] + side * 2.5)
        return (curve([(sock, 10.2, 30), (sock * 1.4, 14, 48), (back[0] - 12, back[1] + 6, back[2] + 6), back], V, col, 1.1) +
                seg(back, grip, V, col, 2.6) + seg(grip, tip, V, '#3c3c40', 0.6))
    ov = probe(ca, '#d32f2f', -9, 1) + probe(cb, '#1d1d1f', 9, -1)
    ov += curve([ws['+'], (58, 2.0, pz_ + 3.6), (64, 2.5, 11.2)], V, '#d32f2f', 0.9) + curve([ws['-'], (58, 2.0, pz_ - 3.6), (64, 2.5, 8.8)], V, '#1d1d1f', 0.9)
    # readout on the display, the A mark at the red contact, the swap hint
    dx_, dy_ = to2((0, 8.6, -24), V)
    ov += txt(dx_, dy_ + 2.2, '+3.9 V', size=6.2, col='#1d1d1f', weight=600)
    render('check-meter', grp, V, 'Multimeter reading +3.9 V across the battery plug', ov, (4, 4, 4, 4))
    # close-up of the plug face: both contacts, a probe tip on each, A marked at the red one
    V2 = (-1, 0.55, 0.42)
    ov2 = ''
    def probe2(tip, col, ang, L=14.0, tipL=3.2):
        tx, ty = to2(tip, V2); a = math.radians(ang)
        gx, gy = tx + tipL * math.cos(a), ty + tipL * math.sin(a); bx, by = gx + L * math.cos(a), gy + L * math.sin(a)
        return (f'<line x1="{bx:.2f}" y1="{by:.2f}" x2="{gx:.2f}" y2="{gy:.2f}" stroke="{col}" stroke-width="1.9" stroke-linecap="round"/>'
                f'<line x1="{gx:.2f}" y1="{gy:.2f}" x2="{tx:.2f}" y2="{ty:.2f}" stroke="#3c3c40" stroke-width="0.35" stroke-linecap="round"/>')
    ov2 += probe2(ca, '#d32f2f', 160) + probe2(cb, '#1d1d1f', 205)
    ax_, ay_ = to2(ca, V2)
    ov2 += f'<circle cx="{ax_:.2f}" cy="{ay_:.2f}" r="0.9" fill="none" stroke="{R.BLUE}" stroke-width="0.25"/>'
    ov2 += f'<line x1="{ax_:.2f}" y1="{ay_ + 0.9:.2f}" x2="{ax_:.2f}" y2="{ay_ + 5.0:.2f}" stroke="{R.BLUE}" stroke-width="0.2"/>' + txt(ax_, ay_ + 7.6, 'A', size=2.6, col=R.BLUE)
    render('check-plug', [('bat', [plug[0]])], V2, 'Close-up: red probe on contact A, black probe on the other contact', ov2, (18, 6, 14, 10), px=40)

E = R.expansion(); INNER, REST = R.split_sockets(E)
HS = R.headers(R.SOCK_Y); XS = R.xiao(R.SOCK_Y + 2.54)
BOARD = [REST, INNER, HS, XS]

if 'charge' in which:
    # the board with the battery on its JST, lying in an open soft LiPo-safe pouch; USB-C cable into the XIAO through the open zip
    A = (-1, 1.3, 0.8)
    J = PT.JST; zc = (J['z_plus'] + J['z_minus']) / 2
    PL = PT.jst_plug(J['x_open'], J['y_mid'], zc, gap=0); ws = PT.jst_wire_starts(J['x_open'], J['y_mid'], zc, gap=0)
    lipo = cq.Workplane().box(30, 5, 24).edges('|Y').fillet(2.0).edges('>Y').fillet(0.8).translate((200, -3.2 + 2.5, 2))
    def w2(p0, p1, bend):
        pts = [cq.Vector(*p0), cq.Vector(*bend), cq.Vector(*p1)]
        prof = cq.Workplane(cq.Plane(origin=pts[0], normal=(pts[1] - pts[0]).normalized())).circle(0.5)
        return prof.sweep(cq.Workplane().spline(pts, includeCurrent=False))
    leads = [w2(ws[k], (185, -0.7, 2 + dz), (ws[k][0] + 3, ws[k][1] - 1.0, ws[k][2] + dz)) for k, dz in (('+', 1.2), ('-', -1.2))]
    U = PT.usb_c_plug(117.96, 7.74, 0.875)
    grp = [('kit', BOARD + PL + [lipo] + leads + U)]
    res = R.project_groups(grp, A)
    _, g = R.styled_svg(res, {'kit': R.OBJ}, '', pad=0)
    # kit box on screen (without the cable tail): board corners and the battery
    kp = [to2((x, y, z), A) for x in (119.5, 177.5) for z in (-20.4, 22.1) for y in (-3.2, 9)] + [to2((x, 0, z), A) for x in (185, 215) for z in (-10, 14)]
    x0 = min(p[0] for p in kp); x1 = max(p[0] for p in kp); y0 = min(p[1] for p in kp); y1 = max(p[1] for p in kp)
    W_, H_ = x1 - x0, y1 - y0
    # soft pouch drawn as vector: back wall behind the kit (under the fill), front lip over it
    L, Rr, T, B = x0 - 0.10 * W_, x1 + 0.08 * W_, y0 - 0.30 * H_, y1 + 0.26 * H_
    back = (f'M{L:.2f},{y1 - 0.05 * H_:.2f} C{L - 2:.2f},{T + 0.3 * H_:.2f} {L + 4:.2f},{T + 2:.2f} {L + 0.16 * W_:.2f},{T + 1.5:.2f} '
            f'C{L + 0.45 * W_:.2f},{T - 3.5:.2f} {L + 0.70 * W_:.2f},{T + 4.5:.2f} {Rr - 0.10 * W_:.2f},{T + 0.10 * H_:.2f} '
            f'C{Rr - 2:.2f},{T + 0.14 * H_:.2f} {Rr + 2:.2f},{T + 0.40 * H_:.2f} {Rr:.2f},{y1 - 0.02 * H_:.2f} '
            f'C{Rr - 0.2 * W_:.2f},{B + 1:.2f} {L + 0.25 * W_:.2f},{B + 2:.2f} {L:.2f},{y1 - 0.05 * H_:.2f} Z')
    zip_ = (f'M{L + 0.05 * W_:.2f},{T + 4.2:.2f} C{L + 0.09 * W_:.2f},{T + 3.4:.2f} {L + 0.12 * W_:.2f},{T + 3.4:.2f} {L + 0.16 * W_:.2f},{T + 3.4:.2f} '
            f'C{L + 0.45 * W_:.2f},{T - 1.6:.2f} {L + 0.70 * W_:.2f},{T + 6.4:.2f} {Rr - 0.10 * W_:.2f},{T + 0.10 * H_ + 2:.2f}')
    folds = (f'<path d="M{L + 0.30 * W_:.2f},{T + 2.5:.2f} q2,{0.12 * H_:.2f} -1,{0.24 * H_:.2f}" fill="none" stroke="#b8b8be" stroke-width="0.3"/>'
             f'<path d="M{L + 0.62 * W_:.2f},{T + 3.5:.2f} q-2,{0.10 * H_:.2f} 1,{0.20 * H_:.2f}" fill="none" stroke="#b8b8be" stroke-width="0.3"/>')
    under = (f'<path d="{back}" fill="#f4f4f6" stroke="#9a9aa0" stroke-width="0.45" stroke-linejoin="round"/>'
             f'<path d="{zip_}" fill="none" stroke="#6e6e73" stroke-width="1.1" stroke-dasharray="0.35 0.55"/>' + folds)
    lipY = y1 + 0.06 * H_
    lip = (f'M{L:.2f},{y1 - 0.05 * H_:.2f} C{L + 0.25 * W_:.2f},{lipY + 3:.2f} {Rr - 0.25 * W_:.2f},{lipY + 1:.2f} {Rr:.2f},{y1 - 0.02 * H_:.2f} '
           f'C{Rr - 0.2 * W_:.2f},{B + 1:.2f} {L + 0.25 * W_:.2f},{B + 2:.2f} {L:.2f},{y1 - 0.05 * H_:.2f} Z')
    stitch = f'M{L + 2:.2f},{y1 - 0.02 * H_ + 1.2:.2f} C{L + 0.25 * W_:.2f},{lipY + 4.4:.2f} {Rr - 0.25 * W_:.2f},{lipY + 2.4:.2f} {Rr - 2:.2f},{y1 + 1.0:.2f}'
    over = (f'<path d="{lip}" fill="#eeeef1" stroke="#9a9aa0" stroke-width="0.45" stroke-linejoin="round"/>'
            f'<path d="{stitch}" fill="none" stroke="#9a9aa0" stroke-width="0.3" stroke-dasharray="1.2 0.9"/>')
    # the cable over the lip: redraw the cable on top of the lip as a plain vector stroke
    c0 = to2((117.96 - 26.5, 7.74, 0.875), A); c1 = to2((117.96 - 48.5, 7.74, 0.875), A)
    over += f'<line x1="{c0[0]:.2f}" y1="{c0[1]:.2f}" x2="{c1[0]:.2f}" y2="{c1[1]:.2f}" stroke="#3c3c40" stroke-width="3.6" stroke-linecap="round"/>'
    over += f'<line x1="{c0[0]:.2f}" y1="{c0[1]:.2f}" x2="{c1[0]:.2f}" y2="{c1[1]:.2f}" stroke="#ffffff" stroke-width="2.9" stroke-linecap="round"/>'
    crop = (min(L, c1[0]) - 4, T - 6, Rr + 4, B + 6)
    svg, box = R.styled_svg(res, {'kit': R.OBJ}, 'First charge: board and battery inside an open soft LiPo-safe pouch, USB-C cable into the XIAO', png='charge-pouch-fill.png', overlay=over, crop=crop)
    svg = svg.replace('<image ', under + '<image ', 1)
    R.fill_png(grp, A, box, 10, f'{out}/charge-pouch-fill.png')
    open(f'{out}/charge-pouch.svg', 'w').write(svg); print('charge-pouch ok')

if 'phone' in which:
    A = (-0.9, 1.5, 1)
    # a modern iPhone lying flat: no Home button, Dynamic Island, one round button shape on a blank app screen
    PW, PL_, PT_ = 71.5, 147.0, 7.8
    ph = cq.Workplane().box(PL_, PT_, PW).edges('|Y').fillet(11).edges('>Y or <Y').fillet(1.4).translate((0, PT_ / 2, 0))
    scr = cq.Workplane().box(PL_ - 7, 0.4, PW - 7).edges('|Y').fillet(8.5).translate((0, PT_ - 0.1, 0))
    ph = ph.cut(scr)
    isl = cq.Workplane().box(6.5, 0.6, 20).edges('|Y').fillet(3.2).translate((PL_ / 2 - 10.5, PT_ - 0.3, 0))
    ring = cq.Workplane("XZ").circle(11).circle(9.6).extrude(-0.3).translate((-6, PT_ - 0.3 + 0.3, 0))
    side = [cq.Workplane().box(9, 1.2, 1.0).translate((-PL_ / 2 + 38, 4.2, -PW / 2 - 0.3)),
            cq.Workplane().box(14, 1.2, 1.0).translate((-PL_ / 2 + 38, 4.2, PW / 2 + 0.3))]
    ph = ph.cut(isl)
    phone = [ph, ring] + side
    # the locked case v1 (hardware/raily-pin-public/case/keyring_case.py) lying front up next to it, long axis along x like the phone
    import os
    sys.path.insert(0, os.environ.get('KEYRING_CASE', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'case')))
    import keyring_case as K
    kp = K.P(); kb, kinfo = K.parts(kp)
    body = [kb[n] for n in ('tray', 'loop', 'button', 'cover', 'led') if n in kb] + K.screws(kp, kinfo)[0::2]
    dx, dy, dz = -162.7 + 30.0, 8.1, 95.0                          # back face (y -8.1) onto the table
    grp = [('phone', phone), ('key', [w.translate((dx, dy, dz)) for w in body])]
    # signal waves between them
    c = to2((22.0, 10.0, 58.0), A)
    ov = ''.join(f'<path d="M{c[0] - r * 0.8:.2f},{c[1] - r * 0.6:.2f} A{r:.2f},{r:.2f} 0 0,1 {c[0] + r * 0.8:.2f},{c[1] - r * 0.6:.2f}" fill="none" stroke="#9a9aa0" stroke-width="{0.9 if i % 2 else 1.3}" stroke-linecap="round" transform="rotate(20 {c[0]:.2f} {c[1]:.2f})"/>' for i, r in enumerate((4, 7, 10, 13)))
    render('app-phone', grp, A, 'The Raily Device app on an iPhone next to the keyring', ov, (6, 6, 6, 6))
if 'phone-only' in which:
    A = (-0.9, 1.5, 1)
    PW, PL_, PT_ = 71.5, 147.0, 7.8
    ph = cq.Workplane().box(PL_, PT_, PW).edges('|Y').fillet(11).edges('>Y or <Y').fillet(1.4).translate((0, PT_ / 2, 0))
    ph = ph.cut(cq.Workplane().box(PL_ - 7, 0.4, PW - 7).edges('|Y').fillet(8.5).translate((0, PT_ - 0.1, 0)))
    ph = ph.cut(cq.Workplane().box(6.5, 0.6, 20).edges('|Y').fillet(3.2).translate((PL_ / 2 - 10.5, PT_ - 0.3, 0)))
    ring = cq.Workplane("XZ").circle(11).circle(9.6).extrude(-0.3).translate((-6, PT_, 0))
    side = [cq.Workplane().box(9, 1.2, 1.0).translate((-PL_ / 2 + 38, 4.2, -PW / 2 - 0.3)), cq.Workplane().box(14, 1.2, 1.0).translate((-PL_ / 2 + 38, 4.2, PW / 2 + 0.3))]
    c = to2((10.0, 10.0, 48.0), A)
    ov = ''.join(f'<path d="M{c[0] - r * 0.8:.2f},{c[1] - r * 0.6:.2f} A{r:.2f},{r:.2f} 0 0,1 {c[0] + r * 0.8:.2f},{c[1] - r * 0.6:.2f}" fill="none" stroke="#9a9aa0" stroke-width="{0.9 if i % 2 else 1.3}" stroke-linecap="round" transform="rotate(20 {c[0]:.2f} {c[1]:.2f})"/>' for i, r in enumerate((4, 7, 10, 13)))
    render('app-phone-only', [('phone', [ph, ring] + side)], A, 'The Raily Device app on an iPhone', ov, (6, 6, 20, 6))

import sys, cadquery as cq, render as R, parts as PT
out = sys.argv[1]; which = sys.argv[2:]
E = R.expansion(); INNER, REST = R.split_sockets(E)
HS = R.headers(R.SOCK_Y); XS = R.xiao(R.SOCK_Y + 2.54)            # seated XIAO
A = (-1, 1.1, 1)                                                   # board camera (front edge, USB-C at the front-left)
B = (1, 1.1, 1)                                                    # battery camera (right edge, JST opening)
W = lambda ws: cq.Workplane().add(cq.Compound.makeCompound([v for w in ws for v in w.vals()]))

def to2(p, V): return R.to2d(p, V)
def ring(p, V, r=2.2): x, y = to2(p, V); return f'<circle cx="{x:.2f}" cy="{y:.2f}" r="{r}" fill="none" stroke="{R.BLUE}" stroke-width="0.5"/>'
def arrow(p0, p1, V):
    (x0, y0), (x1, y1) = to2(p0, V), to2(p1, V)
    import math; a = math.atan2(y1 - y0, x1 - x0); L = 2.6
    h1 = (x1 - L * math.cos(a - .5), y1 - L * math.sin(a - .5)); h2 = (x1 - L * math.cos(a + .5), y1 - L * math.sin(a + .5))
    return (f'<line x1="{x0:.2f}" y1="{y0:.2f}" x2="{x1 - 1.6*math.cos(a):.2f}" y2="{y1 - 1.6*math.sin(a):.2f}" stroke="{R.BLUE}" stroke-width="0.7" stroke-linecap="round"/>'
            f'<polygon points="{x1:.2f},{y1:.2f} {h1[0]:.2f},{h1[1]:.2f} {h2[0]:.2f},{h2[1]:.2f}" fill="{R.BLUE}"/>')
def label(p, V, text, dx=0, dy=0, anchor='middle', color='#3c3c40', size=3.6):
    x, y = to2(p, V)
    return f'<text x="{x + dx:.2f}" y="{y + dy:.2f}" font-family="Inter Guide, sans-serif" font-size="{size}" font-weight="700" fill="{color}" text-anchor="{anchor}">{text}</text>'

def scene(name, groups, styles, V, label_, overlay='', crop=None, base=0.0024, pxmm=12):
    res = R.project_groups(groups, V)
    svg, box = R.styled_svg(res, styles, label_, png=f'{name}-fill.png', overlay=overlay, crop=crop, base=base)
    R.fill_png(groups, V, box, pxmm, f'{out}/{name}-fill.png', )
    open(f'{out}/{name}.svg', 'w').write(svg); print(name, 'ok', flush=True)

# the no-solder motor chain on the bench (owner, 2026-10-09): one Grove-to-Qwiic cable, 100 mm (Adafruit 4528), from the
# UART port (D6/D7) straight into the DA7280. No hub. On the bench the motor lies loose in front of the board.
YB = -6.4                                                          # bench level: the board's lowest part
MOT, mot = PT.da7280_module(191.0, YB, 36.0)
pu, back_u = PT.plug_grove(PT.UART['x'], PT.UART['y_mid'], PT.UART['z_open'] - 3.0, dirz=1)
qm = mot['qwiic_left']; pq2, bq2 = PT.plug_qwiic(qm[0] + 2.0, qm[1], qm[2], dirx=-1)
# about 100 mm between the plugs: out of the port, a wide bow toward the bench front, then into the motor's left socket
B1 = (back_u[0] + 1.0, back_u[1] + 3.0, back_u[2] + 12.0)
B2 = (back_u[0] - 4.0, YB + 1.8, back_u[2] + 34.0)
B3 = (bq2[0] - 16.0, YB + 1.8, bq2[2] + 8.0)
cable = PT.wires_var(back_u, bq2, (B1, B2, B3), ((2.0, 0, 0), (0, 0, 1.0)), r=0.36)
CHAIN = MOT + [pu, pq2] + cable
CBEND = B2                                                         # a point on the cable for its callout
back_h = B1                                                        # the way the cable leaves the port (button scene stub)
LRA = (191.0 + 12.7, YB + 1.6 + 3.2, 36.0 + 29.2 - 6.1)            # top of the motor (LRA) on the DA7280

if 'motor' in which:
    def call(p, t, sdx, sdy, anchor, size=4.7):
        x, y = to2(p, A); tx, ty = x + sdx, y + sdy
        ex = tx + (1.2 if anchor == 'start' else -1.2 if anchor == 'end' else 0)
        return (f'<line x1="{x:.2f}" y1="{y:.2f}" x2="{ex:.2f}" y2="{ty + 1.2 if sdy < 0 else ty - size:.2f}" stroke="{R.BLUE}" stroke-width="0.3"/>'
                f'<circle cx="{x:.2f}" cy="{y:.2f}" r="0.6" fill="{R.BLUE}"/>'
                f'<text x="{tx:.2f}" y="{ty:.2f}" font-family="Inter Guide, sans-serif" font-size="{size}" font-weight="700" fill="#1d1d1f" text-anchor="{anchor}">{t}</text>')
    mid = lambda a, b: tuple((a[i] + b[i]) / 2 for i in range(3))
    ov = (ring((PT.UART['x'], 1.2, PT.UART['z_open']), A, 4.5) + call((PT.UART['x'], 3.6, PT.UART['z_open'] - 4.0), 'UART port', -4, -26, 'end') +
          call(CBEND, 'Grove-to-Qwiic cable 100 mm', 8, 12, 'start') + call(LRA, 'Haptic motor DA7280', 12, 12, 'start'))
    grp = [('rest', [REST, INNER, HS, XS]), ('chain', CHAIN)]
    res = R.project_groups(grp, A); st = {'rest': R.CTX, 'chain': R.OBJ}
    _, g = R.styled_svg(res, st, '', pad=0)
    crop = (g[0] - 12, g[1] - 30, g[0] + g[2] + 56, g[1] + g[3] + 16)
    svg, box = R.styled_svg(res, st, 'The motor chain: one Grove-to-Qwiic cable from the UART port to the DA7280', png='motor-chain-fill.png', overlay=ov, crop=crop)
    R.fill_png(grp, A, box, 10, f'{out}/motor-chain-fill.png')
    open(f'{out}/motor-chain.svg', 'w').write(svg); print('motor-chain ok')

if 'usb' in which:
    U = PT.usb_c_plug(117.96, 7.74, 0.875)
    scene('usb-nobatt', [('rest', [REST, INNER, HS] + CHAIN), ('xiao', [XS]), ('usb', U)],
          {'rest': R.CTX, 'xiao': R.OBJ, 'usb': R.OBJ}, A, 'USB-C cable into the XIAO; the battery stays unplugged',
          arrow((100, 7.74, 0.875), (112, 7.74, 0.875), A))

if 'button' in which:
    b = PT.BUTTON_D1
    # crop to the board: project the board outline corners, the motor stays out of frame
    cs = [to2((x, y, z), A) for x in (119.5, 177.5) for z in (-21.5, 23.0) for y in (-3.2, 9.0)]
    xs = [c[0] for c in cs]; ys = [c[1] for c in cs]
    bcrop = (min(xs) - 2, min(ys) - 2, max(xs) + 2, max(ys) + 2)
    rx, ry = to2((b['x'], b['y'] + 0.5, b['z']), A)
    # the cable runs out of frame: cut it where it crosses the right edge, no motor module in this scene
    xr = rx + 5
    lerp = lambda t: tuple(back_u[i] + t * (back_h[i] - back_u[i]) for i in range(3))
    t = next(t / 100 for t in range(5, 100) if to2(lerp(t / 100), A)[0] >= xr)
    e = lerp(t); stub = PT.wires(back_u, e, ((back_u[0] + e[0]) / 2, back_u[1] + 1.2, (back_u[2] + e[2]) / 2))
    bcrop = (bcrop[0], bcrop[1], to2(e, A)[0] - 1.1, bcrop[3])
    tx, ty = bcrop[2] - 1, ry - 15                                  # label in the empty top-right corner, on a leader
    lead = f'<line x1="{rx + 2.2:.2f}" y1="{ry - 2.2:.2f}" x2="{tx - 6:.2f}" y2="{ty + 1.5:.2f}" stroke="{R.BLUE}" stroke-width="0.35"/>'
    txt = f'<text x="{tx:.2f}" y="{ty:.2f}" font-family="Inter Guide, sans-serif" font-size="3.6" font-weight="700" fill="#3c3c40" text-anchor="end">button</text>'
    scene('button-d1', [('rest', [REST, INNER, HS, XS] + [pu] + stub), ], {'rest': R.CTX}, A, crop=bcrop, label_='The user button on the board edge', overlay=ring((b['x'], b['y'] + 0.5, b['z']), A, 3.0) + lead + txt)

if 'jst' in which:
    import math
    J = PT.JST
    jst_solid = [v for c in E.vals() for v in c.Solids() if abs(v.Volume() - 130.2) < 0.5]
    sw_solid = [v for c in E.vals() for v in c.Solids() if abs(v.Volume() - 88.2) < 0.5]
    others = [v for c in E.vals() for v in c.Solids() if abs(v.Volume() - 130.2) >= 0.5 and abs(v.Volume() - 88.2) >= 0.5]
    Wk = lambda xs: cq.Workplane().add(cq.Compound.makeCompound(xs))
    zc = (J['z_plus'] + J['z_minus']) / 2
    PL = PT.jst_plug(J['x_open'], J['y_mid'], zc)
    ws = PT.jst_wire_starts(J['x_open'], J['y_mid'], zc)
    def wire(p0, col):
        a = to2(p0, B); b = to2((p0[0] + 10, p0[1] - 3, p0[2] + 6), B); c = to2((p0[0] + 24, p0[1] - 6, p0[2] + 4), B)
        return f'<path d="M{a[0]:.2f},{a[1]:.2f} Q{b[0]:.2f},{b[1]:.2f} {c[0]:.2f},{c[1]:.2f}" fill="none" stroke="{col}" stroke-width="0.9" stroke-linecap="round"/>'
    def lab(z, sdx, sdy, t, col):
        p0 = to2((J['x_open'] + 0.1, J['y_mid'], z), B)               # the pin's window on the socket face
        p1 = (p0[0] + sdx, p0[1] + sdy)                                # label in free space (screen offset)
        return (f'<line x1="{p0[0]:.2f}" y1="{p0[1]:.2f}" x2="{p1[0]:.2f}" y2="{p1[1] - 1.8 if sdy > 0 else p1[1] + 0.6:.2f}" stroke="{col}" stroke-width="0.3"/>'
                f'<circle cx="{p0[0]:.2f}" cy="{p0[1]:.2f}" r="0.45" fill="{col}"/>'
                f'<text x="{p1[0]:.2f}" y="{p1[1] + (1.5 if sdy > 0 else 0):.2f}" font-family="Inter Guide, sans-serif" font-size="4.4" font-weight="700" fill="{col}" text-anchor="middle">{t}</text>')
    ov = (wire(ws['+'], '#d32f2f') + wire(ws['-'], '#1d1d1f') +
          lab(J['z_plus'], -1.2, 6.5, '+', R.BLUE) + lab(J['z_minus'], 6.5, -1.2, '−', '#3c3c40') +
          arrow((J['x_open'] + 10.5, J['y_mid'], zc), (J['x_open'] + 2.0, J['y_mid'], zc), B))
    scene('battery-jst', [('rest', [Wk(others), INNER, HS, XS]), ('jst', [Wk(jst_solid)]), ('plug', PL)], {'rest': R.CTX, 'jst': R.OBJ, 'plug': R.OBJ}, B,
          'The battery plug going into the board socket; red wire on the pin printed +', ov)

if 'switch' in which:
    V = (0.35, 1.2, -1)                        # from behind the back edge, where the slide switch sits
    sw_solid = [v for c in E.vals() for v in c.Solids() if abs(v.Volume() - 88.2) < 0.5]
    rest = [v for c in E.vals() for v in c.Solids() if abs(v.Volume() - 88.2) >= 0.5]
    Wk = lambda xs: cq.Workplane().add(cq.Compound.makeCompound(xs))
    x_on, x_off, zb, yt = 130.3, 139.1, -21.5, 1.0
    (ax, ay), (bx, by) = to2((x_on, yt, zb), V), (to2((x_off, yt, zb), V))
    ov = (label((x_off + 1.5, yt, zb - 1.5), V, 'OFF', color=R.BLUE, size=3.4, dy=-4) + label((x_on - 1.5, yt, zb - 1.5), V, 'ON', color='#9a9aa0', size=3.4, dy=-4) +
          arrow((x_on + 2, yt + 3.5, zb - 1.5), (x_off - 0.5, yt + 3.5, zb - 1.5), V))
    px, py = to2(((x_on + x_off) / 2, yt, zb), V)
    crop = (px - 26, py - 20, px + 26, py + 12)
    res = R.project_groups([('rest', [Wk(rest), HS, XS]), ('sw', [Wk(sw_solid)])], V)
    svg, box = R.styled_svg(res, {'rest': R.CTX, 'sw': R.OBJ}, 'Power switch, slide it to OFF', png='switch-off-fill.png', overlay=ov, crop=crop)
    R.fill_png([('rest', [Wk(rest), HS, XS]), ('sw', [Wk(sw_solid)])], V, box, 14, f'{out}/switch-off-fill.png')
    open(f'{out}/switch-off.svg', 'w').write(svg); print('switch ok')

if 'overview' in which:
    # everything connected on the bench: board + XIAO, motor on its cable, LiPo pouch on the JST; callouts on leaders
    J = PT.JST; zc = (J['z_plus'] + J['z_minus']) / 2
    PL = PT.jst_plug(J['x_open'], J['y_mid'], zc, gap=0)
    ws = PT.jst_wire_starts(J['x_open'], J['y_mid'], zc, gap=0)
    BX0, BX1, BZ0, BZ1, BY = 204.0, 234.0, -26.0, -2.0, -3.2         # 30 x 24 x 5 LiPo pouch, flat on the bench
    bat = cq.Workplane().box(BX1 - BX0, 5.0, BZ1 - BZ0).edges('|Y').fillet(2.0).edges('>Y').fillet(0.8).translate(((BX0 + BX1) / 2, BY + 2.5, (BZ0 + BZ1) / 2))
    def wire2(p0, p1, bend):
        pts = [cq.Vector(*p0), cq.Vector(*bend), cq.Vector(*p1)]
        path = cq.Workplane().spline(pts, includeCurrent=False)
        prof = cq.Workplane(cq.Plane(origin=pts[0], normal=(pts[1] - pts[0]).normalized())).circle(0.5)
        return prof.sweep(path)
    bw = [wire2(ws[k], (BX0, BY + 2.5, -8 + dz), (ws[k][0] + 10, ws[k][1] - 1.5, ws[k][2] + dz * 2 - 2)) for k, dz in (('+', 1.2), ('-', -1.2))]
    LED = (121.4, 7.4, 6.6); USB = (119.5, 7.74, 0.9); SW = (134.7, 1.2, -21.5); BT = (PT.BUTTON_D1['x'], 0.8, PT.BUTTON_D1['z'])
    BAT = ((BX0 + BX1) / 2, BY + 5.0, (BZ0 + BZ1) / 2); XI = (131.0, 7.6, 3.5)
    def call(p, t, sdx, sdy, anchor):
        x, y = to2(p, A); tx, ty = x + sdx, y + sdy
        ex = tx + (1.2 if anchor == 'start' else -1.2 if anchor == 'end' else 0)
        return (f'<line x1="{x:.2f}" y1="{y:.2f}" x2="{ex:.2f}" y2="{ty + 1.2 if sdy < 0 else ty - 5.5:.2f}" stroke="{R.BLUE}" stroke-width="0.3"/>'
                f'<circle cx="{x:.2f}" cy="{y:.2f}" r="0.6" fill="{R.BLUE}"/>'
                f'<text x="{tx:.2f}" y="{ty:.2f}" font-family="Inter Guide, sans-serif" font-size="5.5" font-weight="700" fill="#1d1d1f" text-anchor="{anchor}">{t}</text>')
    OLED = (154.2, -0.6, -1.1)                                      # the display top face (fused into the board solid in Seeed's STEP)
    JS = (172.7, 3.8, 11.0)                                         # top of the JST battery socket
    ov = (call(USB, 'USB-C', -14, 18, 'end') + call(LED, 'Light', -3, 30, 'end') + call(SW, 'Power switch', -6, -16, 'end') +
          call(XI, 'XIAO Sense', 6, -26, 'middle') + call(BT, 'Button · side press', 36, -40, 'start') +
          call(BAT, 'Battery', 6, -16, 'start') +
          call(LRA, 'Motor DA7280', 14, 6, 'start'))
    grp = [('board', [REST, INNER, HS, XS] + PL), ('motor', CHAIN), ('bat', [bat] + bw)]
    res = R.project_groups(grp, A)
    st = {'board': R.OBJ, 'motor': R.OBJ, 'bat': R.OBJ}
    _, g = R.styled_svg(res, st, '', pad=0)                          # geometry box, then room for the callouts on each side
    crop = (g[0] - 40, g[1] - 12, g[0] + g[2] + 44, g[1] + g[3] + 34)       # g = (x0, y0, W, H)
    svg, box = R.styled_svg(res, st, 'The keyring parts connected on the bench, each one named', png='overview-fill.png', overlay=ov, crop=crop)
    R.fill_png(grp, A, box, 10, f'{out}/overview-fill.png')
    open(f'{out}/overview.svg', 'w').write(svg); print('overview ok')

if 'cable' in which:
    # box tile (owner, 2026-10-09): the Grove-to-Qwiic cable, 100 mm (Adafruit 4528): Grove plug left, Qwiic plug right
    gp, gb = PT.plug_grove(0.0, 0.0, 0.0, dirz=1)                 # Grove plug, wires leave along +z
    qp, qb = PT.plug_qwiic(62.0, 0.0, 44.0, dirx=-1)                # Qwiic plug, wires leave along -x
    tw = PT.wires_var(gb, qb, ((1.0, 0.0, 22.0), (24.0, 0.0, 30.0), (44.0, 0.0, 44.0)),
                      ((2.0, 0, 0), (0.9, 0, -1.6), (0.6, 0, -1.4), (0.6, 0, -0.9), (0, 0, -1.0)), r=0.4)
    _, g = R.styled_svg(R.project_groups([('cable', [gp, qp] + tw)], A), {'cable': R.OBJ}, '', pad=1.5)
    cx, cy, w = g[0] + g[2] / 2, g[1] + g[3] / 2, g[2]          # pad to the 3:2 of the other box tiles
    scene('grove-qwiic-cable', [('cable', [gp, qp] + tw)], {'cable': R.OBJ}, A, 'Grove-to-Qwiic cable, 100 mm',
          crop=(cx - w / 2, cy - w / 3, cx + w / 2, cy + w / 3), pxmm=10)

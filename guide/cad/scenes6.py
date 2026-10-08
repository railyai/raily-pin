"""XIAO header pages (owner decision 2026-09-28): the pins are soldered OFF the Expansion Board, short ends down.

    cd hardware/raily-pin-public/guide/cad && python scenes6.py ../art [strip xiao solder clip module seat wrong right]

Same renderer, camera and style as scenes2/scenes3: one HLR pass, CAD edges on face fills, blue vector marks.
The breadboard and the flush cutters are parametric (parts.py); the headers are render.headers().
Overlay sizes are given in millimetres on paper: `fig` is the figure box on the page (see build_full.py).
"""
import sys, math, cadquery as cq, render as R, parts as PT

out = sys.argv[1]
which = sys.argv[2:] or ['strip', 'xiao', 'solder', 'clip', 'module', 'seat', 'wrong', 'right']
A = (-1, 1.1, 1)                     # the board camera of every other board figure
F = (0, 0, 1)                        # front section, looking at the near socket row
FONT = 'Inter Guide, sans-serif'; TXT = '#1d1d1f'; RED = '#d32f2f'
CX = (R.SOCK_X0 + R.SOCK_X1) / 2; CZ = sum(R.ROW_Z) / 2
Y0 = R.SOCK_Y                        # spacer bottom when seated (on the sockets or the breadboard)
XB = Y0 + 2.54                       # XIAO bottom, on the spacer
XT = XB + 1.19                       # XIAO top face (PCB 1.24 mm, from the STEP)
CUT = 1.5                            # clipped tail over the XIAO top (the guide says 2 mm or less)
SHORT, LONG = 3.0, 6.0
IN_BB = 0.05                         # short ends inside the breadboard: hidden by HLR, and left out so their face fills
                                     # do not paint over the breadboard top (painter's algorithm)
SHADE = {'sp': 0.8, 'sock': 0.8, 'inner': 0.8}   # black plastic: header spacers and the board's inner sockets
hdr = lambda y0, **kw: (R.headers(y0, spacer=False, **kw), R.headers(y0, pins=False))
W = lambda ws: cq.Workplane().add(cq.Compound.makeCompound([v for w in ws for v in w.vals()]))

# breadboard piece: holes on the 2.54 grid, both header rows 6 pitches apart across the centre channel
BX = [R.PIN_X[0] + k * 2.54 for k in range(-1, 8)]
BZ = [R.ROW_Z[0] + j * 2.54 for j in (-1, 0, 1, 4, 5, 6, 7)]
BB = PT.breadboard(Y0, BX[0] - 1.9, BX[-1] + 1.9, BX, BZ, R.ROW_Z[0] + 2.5 * 2.54)


class Ov:
    """Vector marks in paper millimetres for one figure. k = paper mm per view unit."""
    def __init__(self, V, k):
        self.V, self.k, self.s = V, k, ''

    def u(self, mm): return mm / self.k
    def p(self, q): return R.to2d(q, self.V)

    def ring(self, q, r_mm=3.2, col=R.BLUE):
        x, y = self.p(q)
        self.s += f'<circle cx="{x:.2f}" cy="{y:.2f}" r="{self.u(r_mm):.2f}" fill="none" stroke="{col}" stroke-width="{self.u(0.6):.3f}"/>'

    def arrow(self, q0, q1, col=R.BLUE):
        (x0, y0), (x1, y1) = self.p(q0), self.p(q1)
        a = math.atan2(y1 - y0, x1 - x0); L = self.u(3.2); h = 0.45
        h1 = (x1 - L * math.cos(a - h), y1 - L * math.sin(a - h)); h2 = (x1 - L * math.cos(a + h), y1 - L * math.sin(a + h))
        self.s += (f'<line x1="{x0:.2f}" y1="{y0:.2f}" x2="{x1 - L * 0.8 * math.cos(a):.2f}" y2="{y1 - L * 0.8 * math.sin(a):.2f}" '
                   f'stroke="{col}" stroke-width="{self.u(1.0):.3f}" stroke-linecap="round"/>'
                   f'<polygon points="{x1:.2f},{y1:.2f} {h1[0]:.2f},{h1[1]:.2f} {h2[0]:.2f},{h2[1]:.2f}" fill="{col}"/>')

    def text(self, x, y, t, anchor='start', col=TXT, pt=10.0):
        fs = self.u(pt * 0.3528)
        self.s += f'<text x="{x:.2f}" y="{y:.2f}" font-family="{FONT}" font-size="{fs:.2f}" font-weight="700" fill="{col}" text-anchor="{anchor}" stroke="#fff" stroke-width="{self.u(0.9):.3f}" paint-order="stroke" stroke-linejoin="round">{t}</text>'

    def dim(self, q0, q1, t, side=1, col=R.BLUE, gap_mm=1.2, lead=None):
        """Vertical dimension between two 3D points on one vertical line; label to the right (side=1) or left,
        or at lead = (dx, dy) paper mm from the dimension's middle, on a leader."""
        (x, y0), (_, y1) = self.p(q0), self.p(q1)
        tk = self.u(1.2); sw = self.u(0.35)
        if lead:
            ym = (y0 + y1) / 2; lx, ly = x + self.u(lead[0]), ym + self.u(lead[1])
            self.s += f'<line x1="{x:.2f}" y1="{ym:.2f}" x2="{lx:.2f}" y2="{ly:.2f}" stroke="{col}" stroke-width="{sw:.3f}"/>'
            self.text(lx + (self.u(0.8) if lead[0] > 0 else -self.u(0.8)), ly + self.u(1.25), t, 'start' if lead[0] > 0 else 'end')
        self.s += (f'<line x1="{x:.2f}" y1="{y0:.2f}" x2="{x:.2f}" y2="{y1:.2f}" stroke="{col}" stroke-width="{sw:.3f}"/>'
                   f'<line x1="{x - tk:.2f}" y1="{y0:.2f}" x2="{x + tk:.2f}" y2="{y0:.2f}" stroke="{col}" stroke-width="{sw:.3f}"/>'
                   f'<line x1="{x - tk:.2f}" y1="{y1:.2f}" x2="{x + tk:.2f}" y2="{y1:.2f}" stroke="{col}" stroke-width="{sw:.3f}"/>')
        if not lead: self.text(x + side * self.u(gap_mm + 0.8), (y0 + y1) / 2 + self.u(1.25), t, 'start' if side > 0 else 'end')

    def badge(self, x, y, good):
        r = self.u(3.4); col = R.BLUE if good else RED; w = self.u(0.9)
        self.s += f'<circle cx="{x:.2f}" cy="{y:.2f}" r="{r:.2f}" fill="{col}"/>'
        if good:
            self.s += (f'<polyline points="{x - r * .45:.2f},{y + r * .02:.2f} {x - r * .1:.2f},{y + r * .38:.2f} {x + r * .5:.2f},{y - r * .35:.2f}" '
                       f'fill="none" stroke="#fff" stroke-width="{w:.3f}" stroke-linecap="round" stroke-linejoin="round"/>')
        else:
            d = r * 0.4
            self.s += (f'<path d="M{x - d:.2f},{y - d:.2f} L{x + d:.2f},{y + d:.2f} M{x + d:.2f},{y - d:.2f} L{x - d:.2f},{y + d:.2f}" '
                       f'stroke="#fff" stroke-width="{w:.3f}" stroke-linecap="round"/>')


def fade(c, f=0.14, sides='lrb'):
    x0, y0, x1, y1 = c; Wd, H = x1 - x0, y1 - y0
    g = ('<defs><linearGradient id="fl" x1="0" x2="1"><stop offset="0" stop-color="#fff"/><stop offset="1" stop-color="#fff" stop-opacity="0"/></linearGradient>'
         '<linearGradient id="fr" x1="1" x2="0"><stop offset="0" stop-color="#fff"/><stop offset="1" stop-color="#fff" stop-opacity="0"/></linearGradient>'
         '<linearGradient id="fb" x1="0" x2="0" y1="1" y2="0"><stop offset="0" stop-color="#fff"/><stop offset="1" stop-color="#fff" stop-opacity="0"/></linearGradient></defs>')
    if 'l' in sides: g += f'<rect x="{x0}" y="{y0}" width="{Wd * f}" height="{H}" fill="url(#fl)"/>'
    if 'r' in sides: g += f'<rect x="{x1 - Wd * f}" y="{y0}" width="{Wd * f}" height="{H}" fill="url(#fr)"/>'
    if 't' in sides: g += f'<linearGradient id="ft" x1="0" x2="0" y1="0" y2="1"><stop offset="0" stop-color="#fff"/><stop offset="1" stop-color="#fff" stop-opacity="0"/></linearGradient><rect x="{x0}" y="{y0}" width="{Wd}" height="{H * f}" fill="url(#ft)"/>'
    if 'b' in sides: g += f'<rect x="{x0}" y="{y1 - H * f}" width="{Wd}" height="{H * f}" fill="url(#fb)"/>'
    return g


def scene(name, groups, styles, V, label, marks, fig, crop=None, pad=1.5, fades='', pxmm=16):
    """marks(ov, box) draws the overlay; crop = (x0, y0, x1, y1) in view units or None for the geometry box."""
    res = R.project_groups(groups, V)
    if callable(crop):
        def bb(names):
            pts = [p for n in names for o, i in [res[n]] for pl in o + i for p in pl]
            return (min(p[0] for p in pts), min(p[1] for p in pts), max(p[0] for p in pts), max(p[1] for p in pts))
        crop = crop(bb)
    _, box = R.styled_svg(res, styles, '', crop=crop, pad=pad)
    k = min(fig[0] / box[2], fig[1] / box[3])
    ov = Ov(V, k); marks(ov, box)
    c = (box[0], box[1], box[0] + box[2], box[1] + box[3])
    extra = (fade(c, sides=fades) if fades else '') + ov.s
    svg, box = R.styled_svg(res, styles, label, png=f'{name}-fill.png', overlay=extra, crop=c, base=0.30 / (box[2] * k))
    R.fill_png(groups, V, box, pxmm, f'{out}/{name}-fill.png', shade=SHADE)
    open(f'{out}/{name}.svg', 'w').write(svg); print(name, 'ok', f'{box[2]:.1f} x {box[3]:.1f}', f'k={k:.2f}', flush=True)


def around(pts, V, l, t, r, b):
    """Crop box around projected 3D points with margins (view units)."""
    ps = [R.to2d(q, V) for q in pts]
    xs = [p[0] for p in ps]; ys = [p[1] for p in ps]
    return (min(xs) - l, min(ys) - t, max(xs) + r, max(ys) + b)


FIG_A = (79, 86)      # page «Solder the XIAO pins», 2 x 2
FIG_B = (79, 84)      # page «Seat the XIAO», top row
FIG_C = (79, 60)      # page «Seat the XIAO», wrong / right

# 1. header strips over a breadboard: short end down, dimensions on the near strip
if 'strip' in which:
    L = 7.0; y0 = Y0 + L
    H, S = hdr(y0, below=SHORT, above=LONG)
    def m(o, box):
        xe = R.SOCK_X1 + 1.6; z = R.ROW_Z[1]
        o.dim((xe, y0 + 2.54, z), (xe, y0 + 2.54 + LONG, z), 'long end 6 mm')
        o.dim((xe, y0 - SHORT, z), (xe, y0, z), 'short end 3 mm')
        o.arrow((R.SOCK_X0 - 2.4, y0 + 4.5, R.ROW_Z[1]), (R.SOCK_X0 - 2.4, Y0 + 1.0, R.ROW_Z[1]))
    crop = lambda bb: (lambda h: (h[0] - 5, h[1] - 2, h[2] + 26, h[3] + 13))(bb(['hdr', 'sp']))
    scene('hdr-strip', [('bb', [BB]), ('hdr', [H]), ('sp', [S])], {'bb': R.CTX, 'hdr': R.OBJ, 'sp': R.OBJ}, A,
          'Two header strips over a breadboard, short ends down; the long end 6 mm, the short end 3 mm', m, FIG_A, crop=crop, fades='lrb')

# 2. XIAO onto the long ends, chip side up
if 'xiao' in which:
    L = 9.0
    H, S = hdr(Y0, below=IN_BB, above=LONG); X = R.xiao(XB + L)
    def m(o, box):
        o.arrow((CX, XB + L + 13, CZ), (CX, XB + L + 4.2, CZ))
    crop = lambda bb: (lambda h: (h[0] - 4, h[1] - 9, h[2] + 4, h[3] + 12))(bb(['hdr', 'sp']))
    scene('hdr-xiao', [('bb', [BB]), ('hdr', [H, X]), ('sp', [S])], {'bb': R.CTX, 'hdr': R.OBJ, 'sp': R.OBJ}, A,
          'The XIAO lowered onto the long ends of the headers, chip side up, spacers flat under it', m, FIG_A, crop=crop, fades='lrb')

# 3. solder the 14 pins on top
if 'solder' in which:
    H, S = hdr(Y0, below=IN_BB, above=LONG); X = R.xiao(XB)
    pad = (R.PIN_X[0], XT + 0.05, R.ROW_Z[1] + 0.35)
    I = R.iron((pad[0], pad[1] + 0.15, pad[2] + 0.35), direction=(0.75, 0.75, 0.15))
    def m(o, box): o.ring(pad, 2.6)
    crop = lambda bb: (lambda h: (h[0] - 4, h[1] - 12, h[2] + 12, h[3] + 8))(bb(['hdr', 'sp']))
    scene('hdr-solder', [('bb', [BB]), ('hdr', [H, X]), ('sp', [S]), ('iron', [I])], {'bb': R.CTX, 'hdr': R.OBJ, 'sp': R.OBJ, 'iron': R.OBJ}, A,
          'Soldering iron on the corner pin, on top of the XIAO; the headers stand in the breadboard', m, FIG_A, crop=crop, fades='lrbt')

# 4. clip the tails flush, 2 mm or less; the last tail still long, in the cutter
if 'clip' in which:
    last = (1, 6)
    H, S = hdr(Y0, below=IN_BB, above=lambda ri, i: LONG if (ri, i) == last else 1.19 + CUT)
    X = R.xiao(XB)
    pin = (R.PIN_X[6], XT + CUT, R.ROW_Z[1])
    d = cq.Vector(1, 0, 0.35).normalized()
    C = PT.flush_cutter((pin[0] - d.x * 2.5, pin[1], pin[2] - d.z * 2.5), direction=(d.x, 0, d.z))
    def m(o, box):
        q = (R.PIN_X[3], 0, R.ROW_Z[1] + 1.4)
        o.dim((q[0], XT, q[2]), (q[0], XT + CUT, q[2]), '≤ 2 mm', lead=(-7, 12))
    crop = lambda bb: (lambda h: (h[0] - 3, h[1] - 4, h[2] + 15, h[3] + 7))(bb(['hdr', 'sp']))
    scene('hdr-clip', [('bb', [BB]), ('hdr', [H, X]), ('sp', [S]), ('cut', C)], {'bb': R.CTX, 'hdr': R.OBJ, 'sp': R.OBJ, 'cut': R.OBJ}, A,
          'Flush cutters clip the last long tail; the clipped tails stand 2 mm or less over the XIAO', m, FIG_A, crop=crop, fades='lrb')

# 5. the result: one piece, the XIAO with its pins
if 'module' in which:
    H, S = hdr(Y0); X = R.xiao(XB)
    scene('hdr-module', [('m', [H, X]), ('sp', [S])], {'m': R.OBJ, 'sp': R.OBJ}, A, 'The XIAO with its pins: one piece, short ends below the spacers',
          lambda o, b: None, FIG_B, pad=3)

# board parts for 6 and the section views
if {'seat', 'wrong', 'right'} & set(which):
    E = R.expansion(); INNER, REST = R.split_sockets(E)

# 6. into the board's inner sockets, all the way down, USB-C at the edge without the screen
if 'seat' in which:
    L = 12
    H, S = hdr(Y0 + L); X = R.xiao(XB + L)
    def m(o, box): o.arrow((CX, XB + L + 15, CZ), (CX, XB + L + 6.5, CZ))
    crop = around([(119.5, -3.2, 22.1), (119.5, -3.2, -20.4), (158, -3.2, 22.1), (158, 0, -20.4), (CX, XB + L + 16, CZ)], A, 2, 2, 0, 1)
    scene('hdr-seat', [('rest', [REST]), ('inner', [INNER]), ('m', [H, X]), ('sp', [S])], {'rest': R.CTX, 'inner': R.OBJ, 'm': R.OBJ, 'sp': R.OBJ}, A,
          'The XIAO with its pins above the two inner socket rows, arrow down; USB-C at the board edge away from the screen', m, FIG_B,
          crop=crop, fades='r')


def front_section(zc):
    """Board and inner sockets cut at the near socket row's centre plane: everything nearer the viewer is removed."""
    half = cq.Workplane().box(400, 400, 200).translate((0, 0, zc - 100)).val()
    keep = []
    for s in [s for c in E.vals() for s in c.Solids()]:
        b = s.BoundingBox()
        if b.xmin > 150 or b.zmin >= zc:
            continue
        if b.xlen < 0.7 and b.ylen > 5 and any(abs((b.zmin + b.zmax) / 2 - z) < 0.4 for z in R.ROW_Z):
            continue                                   # the socket contacts: the header pin takes their place
        keep.append(s if b.zmax <= zc else s.intersect(half))
    inner = [s for s in keep if 15 < s.BoundingBox().xlen < 19 and abs(s.BoundingBox().xmin - R.SOCK_X0) < 0.1
             and any(abs((s.BoundingBox().zmin + s.BoundingBox().zmax) / 2 - z) < 1.4 for z in R.ROW_Z)]
    rest = [s for s in keep if s not in inner]
    return W([cq.Workplane().add(cq.Compound.makeCompound(rest))]), W([cq.Workplane().add(cq.Compound.makeCompound(inner))])


# 7. wrong (long ends down: legs bottom out, gap, XIAO too high) and right (spacer on the sockets), front section
if {'wrong', 'right'} & set(which):
    BOARD, SOCK = front_section(R.ROW_Z[1])
    top = XB + 3.0 + 1.19 + 3.0
    crop = around([(116.0, -3.4, 0), (142.5, top + 1.5, 0)], F, 0, 0, 0, 0)
    for name, good in (('wrong', False), ('right', True)):
        if name not in which:
            continue
        g = 0.0 if good else 3.0                       # long ends down: the legs bottom out about 3 mm deep
        H, S = hdr(Y0 + g, below=SHORT if good else LONG, above=3.0 if good else SHORT)
        X = R.xiao(XB + g)
        def m(o, box, good=good, g=g):
            o.badge(box[0] + box[2] - o.u(4.2), box[1] + o.u(4.2), good)
            if not good:
                o.ring((R.PIN_X[3], Y0 + g / 2, 0), 3.4 * 1.0 + 1.6, RED)
        scene(f'hdr-{name}', [('board', [BOARD]), ('sock', [SOCK]), ('m', [H, X]), ('sp', [S])], {'board': R.CTX, 'sock': R.OBJ, 'm': R.OBJ, 'sp': R.OBJ}, F,
              'Wrong: long ends down, bare legs under the spacer, the XIAO too high' if not good else
              'Right: short ends down, spacer on the sockets, XIAO on the spacer', m, FIG_C, crop=crop, fades='r')
print('done')

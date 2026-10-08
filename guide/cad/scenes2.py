import sys, render as R
V = (-1, 1.1, 1)
out = sys.argv[1]
E = R.expansion()
INNER, REST = R.split_sockets(E)
print('inner strips', len(INNER.val().Solids()))

def ring(p, r=1.6):
    x, y = R.to2d(p, V)
    return f'<circle cx="{x:.2f}" cy="{y:.2f}" r="{r}" fill="none" stroke="{R.BLUE}" stroke-width="0.45"/>'

def arrow(p0, p1):
    (x0, y0), (x1, y1) = R.to2d(p0, V), R.to2d(p1, V)
    return (f'<line x1="{x0:.2f}" y1="{y0:.2f}" x2="{x1:.2f}" y2="{y1 - 2.2:.2f}" stroke="{R.BLUE}" stroke-width="0.7" stroke-linecap="round"/>'
            f'<polygon points="{x1:.2f},{y1:.2f} {x1 - 1.4:.2f},{y1 - 2.6:.2f} {x1 + 1.4:.2f},{y1 - 2.6:.2f}" fill="{R.BLUE}"/>')

def fade(c, f=0.16):
    x0, y0, x1, y1 = c; W, H = x1 - x0, y1 - y0
    g = ('<defs><linearGradient id="fl" x1="0" x2="1"><stop offset="0" stop-color="#fff"/><stop offset="1" stop-color="#fff" stop-opacity="0"/></linearGradient>'
         '<linearGradient id="fr" x1="1" x2="0"><stop offset="0" stop-color="#fff"/><stop offset="1" stop-color="#fff" stop-opacity="0"/></linearGradient>'
         '<linearGradient id="fb" x1="0" x2="0" y1="1" y2="0"><stop offset="0" stop-color="#fff"/><stop offset="1" stop-color="#fff" stop-opacity="0"/></linearGradient></defs>')
    return (g + f'<rect x="{x0}" y="{y0}" width="{W*f}" height="{H}" fill="url(#fl)"/>'
            f'<rect x="{x1-W*f}" y="{y0}" width="{W*f}" height="{H}" fill="url(#fr)"/>'
            f'<rect x="{x0}" y="{y1-H*f}" width="{W}" height="{H*f}" fill="url(#fb)"/>')

def scene(name, groups, styles, label, overlay='', crop=None, base=0.0024, pxmm=14):
    res = R.project_groups(groups, V)
    svg, box = R.styled_svg(res, styles, label, png=f'{name}-fill.png', overlay=overlay, crop=crop, base=base)
    R.fill_png(groups, V, box, pxmm, f'{out}/{name}-fill.png')
    open(f'{out}/{name}.svg', 'w').write(svg)

# tile: the whole board is the object
if 'tile' in sys.argv: scene('board-tile', [('board', [E])], {'board': R.OBJ}, 'XIAO Expansion Board, sockets empty', pxmm=10)

# seat: XIAO + headers + inner sockets dark, rest of the board context
lift = 15
H = R.headers(R.SOCK_Y + lift); X = R.xiao(R.SOCK_Y + lift + 2.54)
cx = (R.SOCK_X0 + R.SOCK_X1) / 2; cz = sum(R.ROW_Z) / 2
if 'seat' in sys.argv: scene('seat-vec', [('rest', [REST]), ('inner', [INNER]), ('xiao', [H, X])], {'rest': R.CTX, 'inner': R.OBJ, 'xiao': R.OBJ},
      'XIAO with headers lifted above the two inner socket rows, USB-C out past the board edge',
      arrow((cx, R.SOCK_Y + lift + 16, cz), (cx, R.SOCK_Y + lift + 7.5, cz)))

# solder: wider frame, whole XIAO, both header rows, whole iron
H = R.headers(R.SOCK_Y); X = R.xiao(R.SOCK_Y + 2.54)
pad = (R.PIN_X[0], R.SOCK_Y + 2.54 + 1.2 + 0.05, R.ROW_Z[1] + 0.35)
I = R.iron((pad[0], pad[1] + 0.15, pad[2] + 0.35), direction=(0.75, 0.75, 0.15))
px, py = R.to2d(pad, V)
crop = (px - 16, py - 40, px + 38, py + 13)
scene('solder-vec', [('rest', [REST]), ('inner', [INNER]), ('xiao', [H, X]), ('iron', [I])],
      {'rest': R.CTX, 'inner': R.CTX, 'xiao': R.OBJ, 'iron': R.OBJ},
      'Headers in the inner sockets, XIAO on top, USB-C out past the board edge, iron on the corner pad', fade(crop) + ring(pad), crop=crop, base=0.0026, pxmm=12)
print('ok')

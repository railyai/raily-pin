"""Historical: the 18.1e-B cover figures. Edition B now uses case_b2_figs.py (cover 18.2-B, no part codes).

Take the case B (DA7280) figures from 3D printing into the guide as art/kcb-*.svg|png and add vector marks:
hole numbers 1-9 on the dowel step, the two locator dowels (2, 6) on the face step, a side-button callout on the closed view.

    python case_b_figs.py "<~/Desktop/Raily keyring 3D/guide-art/B>" ../art

The figures are drawn by 3D printing's guide_figures.py with this renderer (ISO (0.45, 1, 0.9), up +x); the hole positions
come from their *-holes.json sidecars, in SVG viewBox units, numbered like the owner sheet P18-1e-B-sborka."""
import sys, os, re, json, shutil, render as R
src, dst = sys.argv[1], sys.argv[2]
ISO, UP = (0.45, 1.0, 0.9), (1, 0, 0)
for n in ('step-1', 'step-3', 'step-4', 'step-5', 'step-6a', 'step-6b', 'step-6c', 'step-7', 'step-8', 'step-9', 'step-10', 'closed', 'hanging',
          'colour-v1-iso', 'colour-v5-iso'):
    shutil.copy(os.path.join(src, n + '.svg'), os.path.join(dst, f'kcb-{n}.svg'))
for n in ('step-6a', 'step-6b', 'step-6c'):   # close-ups: their line work runs past the crop, clip it to the viewBox
    q = os.path.join(dst, f'kcb-{n}.svg'); t = open(q).read(); vx_, vy_, vw_, vh_ = re.search(r'viewBox="([^"]+)"', t).group(1).split()
    i = t.index('>') + 1
    t = (t[:i] + f'<defs><clipPath id="clip-{n}"><rect x="{vx_}" y="{vy_}" width="{vw_}" height="{vh_}"/></clipPath></defs><g clip-path="url(#clip-{n})">'
         + t[i:].replace('</svg>', '</g></svg>'))
    open(q, 'w').write(t)
shutil.copy(os.path.join(src, 'closed.png'), os.path.join(dst, 'kcb-closed-tile.png'))
shutil.copy(os.path.join(src, 'closed.svg'), os.path.join(dst, 'kcb-closed-plain.svg'))   # for the phone page, no marks

def widen(svg, l, t, r, b):
    vb = re.search(r'viewBox="([^"]+)"', svg).group(1)
    x0, y0, w, h = map(float, vb.split())
    return svg.replace(f'viewBox="{vb}"', f'viewBox="{x0 - l:.2f} {y0 - t:.2f} {w + l + r:.2f} {h + t + b:.2f}"', 1)

def num(x, y, t, size=3.2):
    return (f'<text x="{x:.2f}" y="{y:.2f}" font-family="Inter Guide, sans-serif" font-size="{size}" font-weight="700" fill="{R.BLUE}" '
            f'stroke="#ffffff" stroke-width="0.9" paint-order="stroke" text-anchor="middle">{t}</text>')

def put(name, overlay, pad=(0, 0, 0, 0)):
    p = os.path.join(dst, f'kcb-{name}.svg'); svg = widen(open(p).read(), *pad)
    open(p, 'w').write(svg.replace('</svg>', overlay + '</svg>'))

# step 1: every hole numbered, the number just above-right of the hole
h1 = json.load(open(os.path.join(src, 'step-1-holes.json')))['holes']
put('step-1', ''.join(num(x + 3.0, y - 1.8, k, size=4.0) for k, (x, y) in ((k, v['svg']) for k, v in h1.items())))
def arrow(x0, y0, x1, y1, w=0.35, head=1.3):
    import math
    a = math.atan2(y1 - y0, x1 - x0); c, s_ = math.cos(a), math.sin(a)
    bx, by = x1 - head * c, y1 - head * s_
    pts = f'{x1:.2f},{y1:.2f} {bx - head * 0.5 * s_:.2f},{by + head * 0.5 * c:.2f} {bx + head * 0.5 * s_:.2f},{by - head * 0.5 * c:.2f}'
    return (f'<line x1="{x0:.2f}" y1="{y0:.2f}" x2="{bx:.2f}" y2="{by:.2f}" stroke="{R.BLUE}" stroke-width="{w}" stroke-linecap="round"/>'
            f'<polygon points="{pts}" fill="{R.BLUE}"/>')

# step 1 inset (owner: show the hole and the dowel close up): two half-sections of hole 9 at one scale, the dowel above
# its hole, then seated with the protrusion marked; the rasters are cropped to their content and embedded
import base64, io
from PIL import Image, ImageChops
def crop(name):
    im = Image.open(os.path.join(src, name + '.png')).convert('RGB')
    bb = ImageChops.difference(im, Image.new('RGB', im.size, (255, 255, 255))).point(lambda v: 255 if v > 40 else 0).getbbox()
    buf = io.BytesIO(); im.crop(bb).save(buf, 'PNG')
    return bb, im.crop(bb).size, 'data:image/png;base64,' + base64.b64encode(buf.getvalue()).decode()
(b1, (w1, h1), u1), (b2, (w2, h2), u2) = crop('step-1-d1'), crop('step-1-d2')
p2 = json.load(open(os.path.join(src, 'step-1-d2-points.json')))['points']
h9 = json.load(open(os.path.join(src, 'step-1-hole9.json')))
F, G, gap = 220, 120, 300                       # label size, clear space inside the frame (owner: at least 4 px on the page), arrow gap; crop pixels
ix2, iy2 = w1 + gap, h1 - h2                    # d2 bottom-aligned with d1
pt = lambda q: (ix2 + p2[q]['png'][0] - b2[0], iy2 + p2[q]['png'][1] - b2[1])
(tx, ty), (fx, fy) = pt('dowel_top'), pt('frame_surface')
dx = ix2 + w2 + 60; W = dx + 50 + 3.8 * F           # content right edge: the «2.7 mm» label, Inter 700 is about 3.6 em wide
T = -1.0 * F                                    # content top: the «9» cap height
fx0, fy0, fw, fh = -G, T - G, W + 2 * G, h1 - T + 2 * G
m = 20                                          # margin outside the frame line
svg = (f'<svg class="iso" viewBox="{fx0 - m} {fy0 - m} {fw + 2 * m} {fh + 2 * m}" role="img" aria-label="Hole 9 close up" xmlns="http://www.w3.org/2000/svg">'
       f'<rect x="{fx0}" y="{fy0}" width="{fw}" height="{fh}" rx="70" fill="#ffffff" stroke="{R.BLUE}" stroke-width="8"/>'
       f'<image href="{u1}" x="0" y="0" width="{w1}" height="{h1}"/><image href="{u2}" x="{ix2}" y="{iy2}" width="{w2}" height="{h2}"/>'
       f'<text x="20" y="{-F * 0.15:.0f}" font-family="Inter Guide, sans-serif" font-size="{F}" font-weight="700" fill="{R.BLUE}">9</text>'
       + arrow(w1 + 40, iy2 + h2 * 0.45, ix2 - 20, iy2 + h2 * 0.45, w=12, head=60))
for yy, xx in ((ty, tx), (fy, fx)):
    svg += f'<line x1="{xx + 40:.0f}" y1="{yy:.0f}" x2="{dx + 30:.0f}" y2="{yy:.0f}" stroke="#86868b" stroke-width="5"/>'
svg += (f'<line x1="{dx:.0f}" y1="{ty:.0f}" x2="{dx:.0f}" y2="{fy:.0f}" stroke="#1d1d1f" stroke-width="7"/>'
        f'<text x="{dx + 50:.0f}" y="{(ty + fy) / 2 + F * 0.35:.0f}" font-family="Inter Guide, sans-serif" font-size="{F}" font-weight="700" fill="#1d1d1f">2.7 mm</text></svg>')
open(os.path.join(dst, 'kcb-dowel-inset.svg'), 'w').write(svg)
hx, hy = h9['step1_hole9']['svg']
put('step-1', f'<circle cx="{hx:.2f}" cy="{hy:.2f}" r="1.7" fill="none" stroke="{R.BLUE}" stroke-width="0.4"/>')
# step 6c: the Qwiic socket the cable goes into (J1, nearer the board), ringed
p6 = json.load(open(os.path.join(src, 'step-6c-points.json')))['points']
x, y = p6['qwiic_used_J1']['svg']
x, y = x + 0.5, y - 2.9                          # the sidecar point is the socket's board-edge foot; its body sits above it in this view
put('step-6c', f'<rect x="{x - 5.3:.2f}" y="{y - 5.1:.2f}" width="10.6" height="10.4" rx="2.0" fill="none" stroke="{R.BLUE}" stroke-width="0.45" stroke-dasharray="1.1 0.7"/>')
# step 4: the two locator dowels (2 and 6) ringed and numbered
h4 = json.load(open(os.path.join(src, 'step-4-holes.json')))['holes']
ov = ''
for k in ('2', '6'):
    x, y = h4[k]['svg']
    ov += f'<circle cx="{x:.2f}" cy="{y:.2f}" r="1.9" fill="none" stroke="{R.BLUE}" stroke-width="0.35" stroke-dasharray="0.9 0.6"/>' + num(x + 4.0, y - 1.6, k, size=3.8)
put('step-4', ov)
# step 3 (nuts): a short arrow beside each nut, toward its pillar slot. Nut centres read off step-3.png (1600 px wide);
# the pillar centres come from step-3-holes.json, both mapped to the SVG viewBox.
h3 = json.load(open(os.path.join(src, 'step-3-holes.json')))
vx, vy, vw, _ = h3['viewBox']; k = vw / h3['png_width']
nuts = {'1': (365, 2630), '2': (365, 1500), '3': (1265, 2440), '4': (1265, 1305)}
ov = ''
for n, (px, py) in nuts.items():
    nx, ny = vx + px * k, vy + py * k; qy = h3['holes'][n]['svg'][1]
    ov += arrow(nx + 3.6, ny, nx + 3.6, ny + (qy - ny) * 0.75, w=0.45, head=1.6)
put('step-3', ov)
# step 4 (face on): the face moves onto the frame; three short strokes at the tip say «press until it clicks»
h4v = json.load(open(os.path.join(src, 'step-4-holes.json')))['viewBox']
ty = h4v[1] + 0.6; x0, x1 = h4v[0] + 12.0, h4v[0] + 33.0
ov = arrow(x0, ty, x1, ty, w=0.45, head=1.8)
for dx, dy in ((1.3, -1.5), (1.9, 0.0), (1.3, 1.5)):
    ov += f'<line x1="{x1 + dx * 0.9:.2f}" y1="{ty + dy * 0.9:.2f}" x2="{x1 + dx * 1.8:.2f}" y2="{ty + dy * 1.8:.2f}" stroke="{R.BLUE}" stroke-width="0.4" stroke-linecap="round"/>'
put('step-4', ov, pad=(0, 3, 0, 0))
# closed: side button, aimed at the nub the finger presses (over D1, x 168.8, near the tongue's loop end; closed-points.json)
x, y = json.load(open(os.path.join(src, 'closed-points.json')))['points']['button_nub']['svg']; size = 6.0
co = (f'<line x1="{x:.2f}" y1="{y:.2f}" x2="{x + 9:.2f}" y2="{y + 7 - size:.2f}" stroke="{R.BLUE}" stroke-width="0.3"/>'
      f'<circle cx="{x:.2f}" cy="{y:.2f}" r="0.7" fill="{R.BLUE}"/>'
      f'<text x="{x + 10:.2f}" y="{y + 7:.2f}" font-family="Inter Guide, sans-serif" font-size="{size}" font-weight="700" fill="#1d1d1f">Side button</text>')
put('closed', co, pad=(2, 2, 41, 4))
print('case B figures ok')

"""Edition B with cover 18.2-B (owner, 2026-10-03): take the figures from case/guide_render_b2.py into the guide as
art/kcb-*.svg|png and add the vector marks. Replaces case_b_figs.py for this edition.

    python case_b2_figs.py <figures from guide_render_b2.py> ../art

The cover page follows the owner's order: dowels into the FACE first (kcb-face-dowels, holes numbered 1-9 like the
owner sheet P18-2G-sborka), then the frame onto them (kcb-frame-on), then the nuts (kcb-nuts, the cover still face down).
The dowel rack S / M / L is the inset (kcb-rack). No ТЕСТ joint, no hole-9 protrusion inset, no part codes."""
import sys, os, re, json, math, shutil, render as R
src, dst = sys.argv[1], sys.argv[2]
for n in ('step-6a', 'step-6b', 'step-6c', 'step-7', 'step-8', 'step-9', 'step-10', 'closed', 'hanging',
          'colour-v1-iso', 'colour-v5-iso', 'face-dowels', 'frame-on', 'nuts', 'rack'):
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


def num(x, y, t, size=3.2, fill=R.BLUE, anchor='middle'):
    return (f'<text x="{x:.2f}" y="{y:.2f}" font-family="Inter Guide, sans-serif" font-size="{size}" font-weight="700" fill="{fill}" '
            f'stroke="#ffffff" stroke-width="0.9" paint-order="stroke" text-anchor="{anchor}">{t}</text>')


def put(name, overlay, pad=(0, 0, 0, 0)):
    p = os.path.join(dst, f'kcb-{name}.svg'); svg = widen(open(p).read(), *pad)
    open(p, 'w').write(svg.replace('</svg>', overlay + '</svg>'))


def arrow(x0, y0, x1, y1, w=0.35, head=1.3):
    a = math.atan2(y1 - y0, x1 - x0); c, s_ = math.cos(a), math.sin(a)
    bx, by = x1 - head * c, y1 - head * s_
    pts = f'{x1:.2f},{y1:.2f} {bx - head * 0.5 * s_:.2f},{by + head * 0.5 * c:.2f} {bx + head * 0.5 * s_:.2f},{by - head * 0.5 * c:.2f}'
    return (f'<line x1="{x0:.2f}" y1="{y0:.2f}" x2="{bx:.2f}" y2="{by:.2f}" stroke="{R.BLUE}" stroke-width="{w}" stroke-linecap="round"/>'
            f'<polygon points="{pts}" fill="{R.BLUE}"/>')


J = lambda n: json.load(open(os.path.join(src, n)))
# face-dowels: every hole numbered, the number just above-left of the hole (the lifted dowel sits to its right)
h = J('face-dowels-holes.json')['holes']
put('face-dowels', ''.join(num(v['svg'][0] - 3.4, v['svg'][1] - 2.0, k, size=4.0) for k, v in h.items()))
# frame-on: the frame moves onto the face (right to left); three short strokes at the tip say «press until it sits»
vb = J('frame-on-holes.json')['viewBox']
ty = vb[1] + 0.6; x0, x1 = vb[0] + vb[2] - 12.0, vb[0] + vb[2] - 33.0
ov = arrow(x0, ty, x1, ty, w=0.45, head=1.8)
for dx, dy in ((1.3, -1.5), (1.9, 0.0), (1.3, 1.5)):
    ov += f'<line x1="{x1 - dx * 0.9:.2f}" y1="{ty + dy * 0.9:.2f}" x2="{x1 - dx * 1.8:.2f}" y2="{ty + dy * 1.8:.2f}" stroke="{R.BLUE}" stroke-width="0.4" stroke-linecap="round"/>'
put('frame-on', ov, pad=(0, 3, 0, 0))
# nuts: a short arrow beside each nut, toward its pillar (both projected by guide_render_b2.py)
pn = J('nuts-points.json')['points']
ov = ''
for i in range(1, 5):
    (nx, ny), (px, py) = pn[f'nut{i}']['svg'], pn[f'pillar{i}']['svg']
    ox = 3.6 if px >= nx - 0.5 else -3.6
    ov += arrow(nx + ox, ny, nx + ox + (px - nx) * 0.7, ny + (py - ny) * 0.7, w=0.45, head=1.6)
put('nuts', ov)
# rack: each row's size in mm beside its last dowel (the letter is raised on the row's tab); a frame like the old inset
rk = J('rack-points.json')
ov = ''
for lt, d in (('S', '3.00'), ('M', '3.05'), ('L', '3.10')):
    x, y = rk['points'][f'{lt}_end']['svg']
    ov += num(x + 2.5, y + 2.3, f'{lt} {d}', size=6.5, fill='#1d1d1f', anchor='start')
x0_, y0_, w_, h_ = rk['viewBox']
fr = (x0_ - 1.0, y0_ - 1.0, w_ + 33.0 + 2.0, h_ + 2.0)
ov = (f'<rect x="{fr[0]:.2f}" y="{fr[1]:.2f}" width="{fr[2]:.2f}" height="{fr[3]:.2f}" rx="3" fill="none" stroke="{R.BLUE}" stroke-width="0.45"/>'
      + ov)
p_ = os.path.join(dst, 'kcb-rack.svg'); svg = widen(open(p_).read(), 1.4, 1.4, 34.4, 1.4)
i_ = re.search(r'<svg[^>]*>', svg).end()          # the frame under the drawing, the labels over it
svg = svg[:i_] + ov.split('<text')[0] + svg[i_:]
svg = svg.replace('</svg>', ''.join('<text' + t for t in ov.split('<text')[1:]) + '</svg>')
open(p_, 'w').write(svg)
# step 6c: the Qwiic socket the cable goes into (J1, nearer the board), ringed
x, y = J('step-6c-points.json')['points']['qwiic_used_J1']['svg']
x, y = x + 0.5, y - 2.9                          # the sidecar point is the socket's board-edge foot; its body sits above it in this view
put('step-6c', f'<rect x="{x - 5.3:.2f}" y="{y - 5.1:.2f}" width="10.6" height="10.4" rx="2.0" fill="none" stroke="{R.BLUE}" stroke-width="0.45" stroke-dasharray="1.1 0.7"/>')
# closed: side button, aimed at the nub the finger presses (closed-points.json)
x, y = J('closed-points.json')['points']['button_nub']['svg']; size = 6.0
co = (f'<line x1="{x:.2f}" y1="{y:.2f}" x2="{x + 9:.2f}" y2="{y + 7 - size:.2f}" stroke="{R.BLUE}" stroke-width="0.3"/>'
      f'<circle cx="{x:.2f}" cy="{y:.2f}" r="0.7" fill="{R.BLUE}"/>'
      f'<text x="{x + 10:.2f}" y="{y + 7:.2f}" font-family="Inter Guide, sans-serif" font-size="{size}" font-weight="700" fill="#1d1d1f">Side button</text>')
put('closed', co, pad=(2, 2, 41, 4))
print('case B (18.2-B) figures ok')

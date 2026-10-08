"""Take the locked keyring case figures (hardware/raily-pin-public/case/guide-figures, made by guide_figures.py
from keyring_case.py) into the guide as art/kc-*.svg|png, and add vector callouts to the closed view.

    python case_figs.py <case/guide-figures> <guide/art>

The case figures use this folder's renderer and camera frame (Expansion Board STEP frame, ISO (0.45, 1, 0.9),
up = +x), so callout anchors are projected with render.to2d from the case's own coordinates."""
import sys, re, shutil, os, render as R
src, dst = sys.argv[1], sys.argv[2]
ISO, UP = (0.45, 1.0, 0.9), (1, 0, 0)
for n in ['step-1', 'step-2', 'step-3', 'step-4', 'step-5', 'step-5b', 'step-6', 'exploded', 'hanging', 'colour-v1-iso', 'colour-v5-iso']:
    shutil.copy(os.path.join(src, n + '.svg'), os.path.join(dst, f'kc-{n}.svg'))
shutil.copy(os.path.join(src, 'colour-v1-iso.png'), os.path.join(dst, 'kc-cover-v1.png'))
shutil.copy(os.path.join(src, 'closed.png'), os.path.join(dst, 'kc-closed-tile.png'))

def callouts(svg, calls, margins, size=5.4):
    x0, y0, w, h = map(float, re.search(r'viewBox="([^"]+)"', svg).group(1).split())
    l, t, r, b = margins
    svg = svg.replace(f'viewBox="{re.search(chr(118) + "iewBox=" + chr(34) + "([^" + chr(34) + "]+)", svg).group(1)}"',
                      f'viewBox="{x0 - l:.2f} {y0 - t:.2f} {w + l + r:.2f} {h + t + b:.2f}"', 1)
    ov = ''
    for p, text, sdx, sdy, anchor in calls:
        x, y = R.to2d(p, ISO, UP); tx, ty = x + sdx, y + sdy
        ex = tx + (1.0 if anchor == 'start' else -1.0 if anchor == 'end' else 0)
        ov += (f'<line x1="{x:.2f}" y1="{y:.2f}" x2="{ex:.2f}" y2="{ty - size * 0.35:.2f}" stroke="{R.BLUE}" stroke-width="0.3"/>'
               f'<circle cx="{x:.2f}" cy="{y:.2f}" r="0.7" fill="{R.BLUE}"/>'
               f'<text x="{tx:.2f}" y="{ty:.2f}" font-family="Inter Guide, sans-serif" font-size="{size}" font-weight="700" fill="#1d1d1f" text-anchor="{anchor}">{text}</text>')
    return svg.replace('</svg>', ov + '</svg>')

# closed view: side button (tray-button body centre in x, y on its +z outer face) and the LED (cover-led body top); case v1.1
svg = open(os.path.join(src, 'closed.svg')).read()
svg = callouts(svg, [((162.72, -1.37, 26.65), 'Side button', 8, 7, 'start'),
                     ((122.27, 13.66, -4.82), 'LED', -10, 9, 'end')], (22, 2, 44, 5))
open(os.path.join(dst, 'kc-closed.svg'), 'w').write(svg)
print('case figures ok')

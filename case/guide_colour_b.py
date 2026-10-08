#!/usr/bin/env python3
"""Colour maps for edition B (tray 17.1 + cover 18.1e): every printed piece in its filament colour, front and iso,
for each shipping colourway (SHIPPING). Same renderer as guide_figures.py, no text in the raster.

    DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib python guide_colour_b.py <out_dir>
"""
import sys

import guide_figures as G
import keyring_case as K


def main(out):
    G.OUT = out
    p17 = K.version_p('v1.3', closure='back', proto=17)
    b17, info17 = K.parts(p17)
    p = K.version_p('v1.3-p18.1e', closure='back', proto=18)
    b, info = K.parts(p)
    screws_ = K.screws(p, info)[0::2]
    pieces = [(n, b17[n]) for n in ('tray', 'button', 'loop') if n in b17] + [(n, b[n]) for n in ('cover', 'led') if n in b]
    print('pieces', [n for n, _ in pieces])
    for sc in K.SHIPPING:
        cols = [K.FILAMENTS[K.body_filament(p, n, sc)]['hex'] for n, _ in pieces] + ['#8A8D93'] * len(screws_)
        ws = [w for _, w in pieces] + screws_
        G.figure(f'colour-{sc}-front', [('case', ws, 'obj')], view=(0, 1, 0), colours=cols)
        G.figure(f'colour-{sc}-iso', [('case', ws, 'obj')], view=G.ISO, colours=cols)


if __name__ == '__main__':
    main(sys.argv[1])

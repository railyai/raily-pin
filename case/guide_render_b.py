"""Guide renders without the part codes («18.1e-3a» on the face back, «17.1» on the tray bottom): render-only.
The models and every print file are unchanged; this wraps version_p so the figure scripts build their parts with
proto=None and proto_label=None (the codes are the only geometry those two fields drive).

    DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib python guide_render_b.py <out_dir> [main|close|colour|all]
"""
import os
import sys
CASE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, CASE)

os.chdir(CASE)
import keyring_case as K

_orig = K.version_p


def version_p_nocodes(name='v1', **kw):
    kw['proto'] = None
    kw['proto_label'] = None
    return _orig(name, **kw)


K.version_p = version_p_nocodes

import guide_figures_da7280 as B
import guide_figures_b_closeups as C
import guide_colour_b as CB

out = sys.argv[1]
what = sys.argv[2] if len(sys.argv) > 2 else 'all'
if what in ('main', 'all'):
    B.main(out, 'B')
if what in ('close', 'all'):
    C.main(out)
if what in ('colour', 'all'):
    CB.main(out)

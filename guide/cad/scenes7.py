"""Box tiles drawn from CAD: countersunk M2 x 16 screws with M2 nuts, and a square of double-sided foam tape.

    python scenes7.py ../art screws tape"""
import sys, cadquery as cq, render as R
out = sys.argv[1]; which = sys.argv[2:]
V = (-1, 1.1, 1)

def tile(name, groups, label, pad=1.5, pxmm=24):
    res = R.project_groups(groups, V)
    st = {g[0]: R.OBJ for g in groups}
    svg, box = R.styled_svg(res, st, label, png=f'{name}-fill.png', pad=pad)
    R.fill_png(groups, V, box, pxmm, f'{out}/{name}-fill.png')
    open(f'{out}/{name}.svg', 'w').write(svg); print(name, 'ok', flush=True)

def screw_cs(x, y, z):
    """ISO 7046-1 M2 x 16 countersunk, cross recess H0, lying along +x: head (dk 3.8, 90 deg) at x, tip at x + 16."""
    shank = cq.Workplane('YZ').circle(1.0).extrude(16.0).translate((x, y, z)).faces('>X').chamfer(0.3)
    head = cq.Workplane('YZ').circle(1.9).workplane(offset=0.9).circle(1.0).loft().translate((x - 0.9, y, z))
    for a in (0, 90):
        head = head.cut(cq.Workplane().box(0.8, 2.6, 0.5).rotate((0, 0, 0), (1, 0, 0), a).translate((x - 0.9, y, z)))
    return shank.union(head)

def nut(x, y, z):
    """ISO 4032 M2 hex nut, s 4.0, m 1.6, lying flat."""
    return cq.Workplane('XZ').polygon(6, 4.0 / 0.866).extrude(-1.6).translate((x, y, z)).faces('>Y or <Y').chamfer(0.2).cut(
        cq.Workplane('XZ').circle(1.0).extrude(-1.6).translate((x, y, z)))

if 'screws' in which:
    parts = [screw_cs(0, 1.0, 0), screw_cs(-4, 1.0, 7), nut(22, 0, 1.0), nut(24, 0, 7.5)]
    tile('screws-cs', [('hw', parts)], '4 M2 x 16 countersunk screws and 4 M2 nuts')

if 'tape' in which:
    foam = cq.Workplane().box(20, 1.0, 20).edges('|Y').fillet(0.6).translate((0, 0.5, 0))
    liner = cq.Workplane().box(20, 0.12, 20).translate((0, 1.06, 0))
    # the peel corner of the liner lifted
    liner = liner.cut(cq.Workplane().box(9, 1, 9).rotate((0, 0, 0), (0, 1, 0), 45).translate((10, 1.06, 10)))
    flap = (cq.Workplane('XZ').polyline([(0, 0), (6.4, 0), (0, 6.4)]).close().extrude(-0.12)
            .rotate((0, 0, 0), (0, 0, 1), 0).translate((3.6, 1.12, 3.6)).rotate((3.6, 1.12, 10), (1, 0, -1), -35))
    tile('foam-tape', [('tape', [foam, liner, flap])], 'A square of double-sided foam tape')

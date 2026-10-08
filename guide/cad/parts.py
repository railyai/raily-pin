"""Parametric parts with no public CAD: Grove vibration motor module, Grove cable, USB-C plug, JST-PH plug.
Dimensions from the Raily Blueprints 03-06. The Grove shroud on the motor module is Seeed's own shroud solid,
copied from the Expansion Board STEP."""
import cadquery as cq, math
import render as R

_E = cq.importers.importStep('expansion.step')
GROVE = [v for v in _E.solids().vals() if abs(v.Volume() - 149.7) < 0.5 and v.BoundingBox().zmin > 12 and v.BoundingBox().xmin < 150][0]
# A0/D0 shroud on the board: x 143.5..153.5, opening at z = 21.36 (front edge), pin wall z 14.9, inside floor y -1.0
A0 = dict(x=148.53, z_open=21.36, y_mid=1.2)
JST = dict(x_open=176.9, y_mid=1.2, z_plus=12.0, z_minus=10.0)   # opening faces +x; + at the front pin (photo silkscreen)
SWITCH = dict(x=134.7, y=0.5, z=-21.5)
BUTTON_D1 = dict(x=168.75, y=0.3, z=21.2)


def plug_grove(x, y, z, dirz=1):
    """HY2.0 4-pin plug body 8.4 x 4.4 x 7.0, entering along -z; returns (solid, back-face centre)."""
    body = cq.Workplane().box(8.2, 4.2, 7.0).translate((x, y, z + dirz * 3.5))
    latch = cq.Workplane().box(3.0, 0.8, 4.0).translate((x, y + 2.5, z + dirz * 4.5))
    return body.union(latch), (x, y, z + dirz * 7.0)


def wires(p0, p1, bend, r=0.42, axis=0, pitch=1.0):
    """Four wires from p0 to p1 through a bend point, side by side along x (axis=0) or z (axis=2)."""
    out = []
    for k in range(4):
        off = (k - 1.5) * pitch
        d = [off if axis == 0 else 0.0, 0.0, off if axis == 2 else 0.0]
        pts = [cq.Vector(p0[0] + d[0], p0[1], p0[2] + d[2]), cq.Vector(bend[0] + d[0], bend[1], bend[2] + d[2]), cq.Vector(p1[0] + d[0], p1[1], p1[2] + d[2])]
        path = cq.Workplane().spline(pts, includeCurrent=False)
        prof = cq.Workplane(cq.Plane(origin=pts[0], normal=(pts[1] - pts[0]).normalized())).circle(r)
        out.append(prof.sweep(path))
    return out


def motor_module(x, y, z):
    """Grove vibration motor module, 20 x 20 PCB (24 overall with the shroud), coin motor 10 x 3.4.
    Placed flat; its Grove shroud opening faces -z at the module's back edge."""
    pcb = cq.Workplane().box(20, 1.6, 20).translate((x, y + 0.8, z))
    pcb = pcb.cut(cq.Workplane().cylinder(4, 1.0, direct=(0, 1, 0)).translate((x - 8, y, z + 8)))
    pcb = pcb.cut(cq.Workplane().cylinder(4, 1.0, direct=(0, 1, 0)).translate((x + 8, y, z + 8)))
    motor = cq.Workplane().cylinder(3.4, 5.0, direct=(0, 1, 0)).translate((x + 2, y + 1.6 + 1.7, z + 2))
    g = cq.Workplane().add(GROVE).rotate((0, 0, 0), (0, 1, 0), 180)
    gb = cq.Compound.makeCompound(g.vals()).BoundingBox()
    g = g.translate((x - (gb.xmin + gb.xmax) / 2, (y + 1.6) - (-1.6), (z - 10) - gb.zmin))
    return [pcb, motor, g]


def usb_c_plug(x, y, z):
    """USB-C plug entering along +x into a receptacle whose mouth is at (x, y, z); cable leaves toward -x."""
    shell = cq.Workplane().box(6.5, 2.4, 8.3).edges('|X').fillet(1.1).translate((x - 3.25, y, z))
    over = cq.Workplane().box(14, 5.5, 11).edges('|X').fillet(2.0).translate((x - 6.5 - 7, y, z))
    boot = cq.Workplane().box(6, 3.6, 4).edges('|X').fillet(1.2).translate((x - 20.5 - 3, y, z))
    cab = cq.Workplane(cq.Plane(origin=(x - 26.5, y, z), normal=(-1, 0, 0))).circle(1.6).extrude(22)
    return [shell, over, boot, cab]


def jst_plug(x, y, z, gap=12):
    """JST-PH 2-pin plug housing (6.0 x 4.5 x 6.0), mating face toward -x at x + gap, two contact windows,
    latch ramp on top, two wire exits at the back. Returns the solids; wires are drawn as coloured overlays."""
    x0 = x + gap
    body = cq.Workplane().box(6.0, 4.5, 6.0).translate((x0 + 3.0, y, z))
    for dz in (-1.0, 1.0):   # PH pitch 2.0
        body = body.cut(cq.Workplane().box(1.0, 1.4, 1.2).translate((x0 + 0.5, y, z + dz)))
        body = body.union(cq.Workplane().box(1.2, 1.3, 1.3).translate((x0 + 6.6, y - 0.2, z + dz)))
    ramp = cq.Workplane().box(3.2, 0.9, 3.0).translate((x0 + 3.2, y + 2.7, z))
    return [body.union(ramp)]


def jst_wire_starts(x, y, z, gap=12):
    return {'+': (x + gap + 7.2, y - 0.2, z + 1.0), '-': (x + gap + 7.2, y - 0.2, z - 1.0)}


def breadboard(y_top, x0, x1, xs, zs, z_channel, depth=8.5, hole=0.9, hole_depth=5.0):
    """A piece of solderless breadboard: a block with its top at y_top, square holes at (x, z) for x in xs,
    z in zs, and the centre channel along x at z_channel. Only a jig to keep header pins straight."""
    z0, z1 = min(zs) - 1.9, max(zs) + 1.9
    body = cq.Workplane().box(x1 - x0, depth, z1 - z0).translate(((x0 + x1) / 2, y_top - depth / 2, (z0 + z1) / 2))
    for x in xs:
        for z in zs:
            body = body.cut(cq.Workplane().box(hole, hole_depth + 0.1, hole).translate((x, y_top - hole_depth / 2 + 0.05, z)))
    groove = cq.Workplane().box(x1 - x0 + 2, 1.2, 1.8).translate(((x0 + x1) / 2, y_top - 0.6, z_channel))
    return body.cut(groove)


def flush_cutter(tip, direction=(1, 0, 0.3), thick=2.2):
    """Flush cutters lying flat, flat face down at tip[1], jaws open around a pin 2.5 mm from the tip.
    direction: the way the tool points away from the jaws, in the x-z plane."""
    d = cq.Vector(direction[0], 0, direction[2]).normalized()
    outline = [(0, 0.9), (4, 2.4), (9, 3.6), (13, 3.6), (40, 8.0), (41, 6.2), (15, 1.6), (15, -1.6),
               (41, -6.2), (40, -8.0), (13, -3.6), (9, -3.6), (4, -2.4), (0, -0.9), (6, 0)]
    pl = cq.Plane(origin=tip, xDir=(d.x, 0, d.z), normal=(0, 1, 0))
    body = cq.Workplane(pl).polyline(outline).close().extrude(thick)
    pivot = cq.Workplane(pl).center(11.5, 0).circle(1.6).extrude(thick + 0.5)
    return [body.union(pivot)]


# ---- the no-solder motor chain (2026-09-30): Grove UART port -> Grove 5 cm -> Grove-Qwiic hub -> Qwiic 50 mm -> DA7280
# UART Grove shroud on the board: the one next to A0/D0 on the side-button edge (x 155.2..165.2 in the Seeed STEP)
UART = dict(x=160.2, z_open=21.36, y_mid=1.2)


def plug_qwiic(x, y, z, dirx=1):
    """JST-SH 4-pin (Qwiic) plug body 4.25 long x 2.9 high x 5.9 wide, entering along -x*dirx; returns (solid, back-face centre)."""
    body = cq.Workplane().box(4.25, 2.9, 5.9).edges('|X').fillet(0.3).translate((x + dirx * 2.125, y, z))
    return body, (x + dirx * 4.25, y, z)


def _socket(x0, x1, y0, h, z0, z1, open_side):
    """A small wire-to-board socket shell with its mouth on one side ('-x', '+x', '-z', '+z')."""
    sx, sz = x1 - x0, z1 - z0
    body = cq.Workplane().box(sx, h, sz).translate(((x0 + x1) / 2, y0 + h / 2, (z0 + z1) / 2))
    wall = 0.5
    if open_side in ('-x', '+x'):
        cav = cq.Workplane().box(sx, h - 2 * wall, sz - 2 * wall)
        cx = x0 + (sx / 2 - wall if open_side == '+x' else -sx / 2 + wall) + sx / 2
        cav = cav.translate((cx, y0 + h / 2, (z0 + z1) / 2))
    else:
        cav = cq.Workplane().box(sx - 2 * wall, h - 2 * wall, sz)
        cz = z0 + (sz / 2 - wall if open_side == '+z' else -sz / 2 + wall) + sz / 2
        cav = cav.translate(((x0 + x1) / 2, y0 + h / 2, cz))
    return body.cut(cav)


def da7280_module(x0, y0, z0):
    """SparkFun Qwiic Haptic Driver DA7280 (ROB-17590), 25.4 x 29.2 board lying flat, corner at (x0, z0), bottom at y0.
    Layout from the board drawing: LRA dia 10 between two holes at the far short edge (+z), a slot in the middle,
    a Qwiic socket on each long side (mouths facing out, -x and +x), a third hole and 7 pads at the near edge."""
    W, L, T = 25.4, 29.2, 1.6
    pcb = cq.Workplane().box(W, T, L).translate((x0 + W / 2, y0 + T / 2, z0 + L / 2))
    for u, v in ((4.0, L - 6.3), (21.5, L - 6.3), (4.0, L - 26.8)):
        pcb = pcb.cut(cq.Workplane('XZ').circle(1.65).extrude(-T).translate((x0 + u, y0, z0 + v)))
    pcb = pcb.cut(cq.Workplane().box(13.5, T * 3, 3.6).edges('|Y').fillet(1.7).translate((x0 + W / 2, y0, z0 + L - 15.2)))
    for k in range(7):
        pcb = pcb.cut(cq.Workplane('XZ').circle(0.5).extrude(-T).translate((x0 + 8.0 + k * 2.54 if k < 3 else x0 + 16.0 + (k - 3) * 2.54, y0, z0 + 1.3)))
    lra = cq.Workplane('XZ').circle(5.0).extrude(-3.2).translate((x0 + W / 2, y0 + T, z0 + L - 6.1)).faces('>Y').edges().chamfer(0.4)
    left = _socket(x0 + 2.5, x0 + 6.7, y0 + T, 2.9, z0 + L - 19.6, z0 + L - 13.6, '-x')
    right = _socket(x0 + W - 6.7, x0 + W - 2.5, y0 + T, 2.9, z0 + L - 19.6, z0 + L - 13.6, '+x')
    chip = cq.Workplane().box(2.2, 0.8, 2.2).rotate((0, 0, 0), (0, 1, 0), 45).translate((x0 + W / 2, y0 + T + 0.4, z0 + L - 21.4))
    return [pcb, lra, left, right, chip], dict(qwiic_left=(x0 + 2.5, y0 + T + 1.45, z0 + L - 16.6), qwiic_right=(x0 + W - 2.5, y0 + T + 1.45, z0 + L - 16.6))


def qwiic_hub(x0, y0, z0):
    """Seeed Grove-Qwiic Hub (103020292), 25.4 x 17.8 board lying flat, corner at (x0, z0), bottom at y0: one Grove socket
    in the middle with its mouth on the -z edge, a Qwiic socket at each end (mouths facing -x and +x). Simplified outline."""
    W, L, T = 25.4, 17.8, 1.6
    pcb = cq.Workplane().box(W, T, L).edges('|Y').fillet(1.0).translate((x0 + W / 2, y0 + T / 2, z0 + L / 2))
    for u, v in ((2.6, 2.6), (W - 2.6, 2.6), (2.6, L - 2.6), (W - 2.6, L - 2.6)):
        pcb = pcb.cut(cq.Workplane('XZ').circle(1.3).extrude(-T).translate((x0 + u, y0, z0 + v)))
    grove = _socket(x0 + W / 2 - 5.0, x0 + W / 2 + 5.0, y0 + T, 5.2, z0, z0 + 7.4, '-z')
    qa = _socket(x0 + 0.4, x0 + 4.65, y0 + T, 2.9, z0 + L / 2 - 3.0, z0 + L / 2 + 3.0, '-x')
    qb = _socket(x0 + W - 4.65, x0 + W - 0.4, y0 + T, 2.9, z0 + L / 2 - 3.0, z0 + L / 2 + 3.0, '+x')
    return [pcb, grove, qa, qb], dict(grove=(x0 + W / 2, y0 + T + 2.6, z0), qwiic_left=(x0 + 0.4, y0 + T + 1.45, z0 + L / 2), qwiic_right=(x0 + W - 0.4, y0 + T + 1.45, z0 + L / 2))

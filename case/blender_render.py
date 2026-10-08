"""Blender render of the Raily Keyring case (render only; geometry and colours come from keyring_case.py).

    python keyring_case.py stage2                 # writes build/stage2/*.stl + scene.json (all schemes)
    Blender -b -P blender_render.py -- <build dir> <out.png> <camera> <scheme> [samples]
    camera: front | back | iso | tab | led
    scheme: a key of keyring_case.SCHEMES, or 'ocean'

Matte PLA (owner decision). Two light settings, from the brandbook ads:
  studio  - soft white studio (4K/01-fold-sequence-white): pale background, large soft key, soft shadow
  gallery - dark gallery (4K/01-gallery-dark): near-black room, blue rim light, reflective dark floor,
            the RGB LED lit
All PLA gets a short subsurface scatter so the thin LED skin passes light while walls stay opaque.
CAD frame (x long axis, y front, z width, mm) -> Blender (X = cad z, Y = -cad y, Z = cad x, metres).
"""
import json
import math
import os
import sys

import bpy
from mathutils import Matrix, Vector

argv = sys.argv[sys.argv.index('--') + 1:]
SRC, OUT, CAM = argv[0], argv[1], argv[2]
SCHEME = argv[3] if len(argv) > 3 else 'white_deep'
SAMPLES = int(argv[4]) if len(argv) > 4 else 128
S = json.load(open(os.path.join(SRC, 'scene.json')))
C = Vector(S['centre'])
if SCHEME == 'ocean':
    COLS, LIGHT = S['colours'], 'studio'
else:
    COLS, LIGHT = S['schemes'][SCHEME]['colours'], S['schemes'][SCHEME]['light']
GALLERY = LIGHT == 'gallery'
LED_ON = CAM == 'led' or GALLERY

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
M = Matrix.Scale(0.001, 4) @ Matrix(((0, 0, 1, 0), (0, -1, 0, 0), (1, 0, 0, 0), (0, 0, 0, 1))) @ Matrix.Translation(-C)


def to_b(p):
    return M @ Vector(p)


def srgb(h):
    c = [int(h[i:i + 2], 16) / 255 for i in (1, 3, 5)]
    return tuple(v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4 for v in c) + (1.0,)


def mat(name, hexcol=None, stops=None, rough=0.62, metal=0.0, emit=None, emit_power=400.0, sss=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes['Principled BSDF']
    b.inputs['Roughness'].default_value = rough
    b.inputs['Metallic'].default_value = metal
    b.inputs['Specular IOR Level'].default_value = 0.35
    if sss:
        b.inputs['Subsurface Weight'].default_value = 1.0
        b.inputs['Subsurface Scale'].default_value = sss      # metres: mean free path in the plastic
        b.inputs['Subsurface Radius'].default_value = (1.0, 1.0, 1.0)
    if emit:
        b.inputs['Emission Color'].default_value = srgb(emit)
        b.inputs['Emission Strength'].default_value = emit_power
    if stops:     # single-filament gradient along the case length (approximation of the Ocean spool)
        tc = nt.nodes.new('ShaderNodeTexCoord')
        sep = nt.nodes.new('ShaderNodeSeparateXYZ')
        mr = nt.nodes.new('ShaderNodeMapRange')
        mr.inputs['From Min'].default_value = S['box'][0]
        mr.inputs['From Max'].default_value = S['box'][1] + 9.0
        ramp = nt.nodes.new('ShaderNodeValToRGB')
        els = ramp.color_ramp.elements
        els[0].position, els[0].color = 0.0, srgb(stops[0])
        els[1].position, els[1].color = 1.0, srgb(stops[-1])
        for i, h in enumerate(stops[1:-1], start=1):
            e = els.new(i / (len(stops) - 1)); e.color = srgb(h)
        nt.links.new(tc.outputs['Object'], sep.inputs['Vector'])
        nt.links.new(sep.outputs['X'], mr.inputs['Value'])
        nt.links.new(mr.outputs['Result'], ramp.inputs['Fac'])
        nt.links.new(ramp.outputs['Color'], b.inputs['Base Color'])
    elif hexcol:
        b.inputs['Base Color'].default_value = srgb(hexcol)
    return m


def load(name, material, smooth=1.5):
    path = os.path.join(SRC, f'{name}.stl')
    if not os.path.exists(path):
        return None
    bpy.ops.wm.stl_import(filepath=path, forward_axis='Y', up_axis='Z')
    ob = bpy.context.selected_objects[0]
    ob.name = name
    ob.matrix_world = M
    ob.data.materials.append(material)
    # smooth only across edges flatter than `smooth` degrees; facet creases (4-24 deg) stay sharp
    try:
        bpy.ops.object.shade_smooth_by_angle(angle=math.radians(smooth), keep_sharp_edges=True)
    except Exception:
        bpy.ops.object.shade_flat()
    return ob


for name, hexcol in COLS.items():
    if name == 'shelf':
        continue                        # inside, never visible
    # subsurface only on the thin LED skin: on the other bodies it would light up the seams between colour zones
    led_td = S['schemes'].get(SCHEME, {}).get('led_td', 1.5)       # TD (mm) of the LED skin's spool
    m = mat(name, stops=S['ocean']) if SCHEME == 'ocean' else mat(name, hexcol, sss=0.00027 * led_td if name == 'led' else 0.0)
    load(name, m)
load('screws', mat('screws', '#8A8D93', rough=0.35, metal=1.0), smooth=30)
for name, e in S.get('extras', {}).items():      # parts seen through the case (v2: the OLED glass under the window)
    load(name, mat(name, e['hex'], rough=e.get('rough', 0.5)), smooth=30)
if LED_ON:
    load('led_die', mat('led_die', '#101010', emit='#30FFB0' if not GALLERY else '#7FA2FF'))

world = bpy.data.worlds.new('studio'); scene.world = world
world.use_nodes = True
bg = world.node_tree.nodes['Background']


def area(name, loc, size, power, target=(0, 0, 0), shadow=True, color=(1, 1, 1), size_y=None):
    d = bpy.data.lights.new(name, 'AREA'); d.energy = power; d.use_shadow = shadow; d.color = color
    if size_y:
        d.shape = 'RECTANGLE'; d.size = size; d.size_y = size_y
    else:
        d.size = size
    o = bpy.data.objects.new(name, d); scene.collection.objects.link(o)
    o.location = loc
    o.rotation_euler = (Vector(target) - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()


zmin = min((bpy.data.objects['tray'].matrix_world @ Vector(c)).z for c in bpy.data.objects['tray'].bound_box)
bpy.ops.mesh.primitive_plane_add(size=6, location=(0, 0, zmin))
floor = bpy.context.active_object

if GALLERY:
    bg.inputs['Color'].default_value = (0.010, 0.013, 0.030, 1)
    bg.inputs['Strength'].default_value = 0.4
    fm = mat('floor', '#0B0E16', rough=0.22)
    floor.data.materials.append(fm)
    blue = (0.30, 0.42, 1.0)
    area('key', (-0.30, -0.40, 0.35), 0.8, 2.4, color=(0.85, 0.9, 1.0))          # soft cool key
    area('front', (0.10, -0.60, 0.05), 1.0, 0.8, shadow=False, color=(0.75, 0.82, 1.0))
    area('rimL', (-0.30, 0.25, 0.10), 0.05, 5, shadow=False, color=blue, size_y=0.8)
    area('rimR', (0.30, 0.25, 0.10), 0.05, 5, shadow=False, color=blue, size_y=0.8)
    area('top', (0.0, 0.10, 0.45), 0.6, 1.2, shadow=False, color=blue)
    scene.render.film_transparent = False
else:
    bg.inputs['Color'].default_value = (0.82, 0.83, 0.86, 1)
    bg.inputs['Strength'].default_value = 0.2 if CAM != 'led' else 0.08
    floor.is_shadow_catcher = True
    floor.visible_glossy = False
    dim = 0.25 if CAM == 'led' else 1.0
    # one directional key high on the left, like a window: the facets read by light and shadow
    area('key', (-0.30, -0.12, 0.45), 0.25, 10 * dim)
    area('fill', (0.40, -0.30, 0.15), 1.4, 1.2 * dim, shadow=False)
    area('rim', (0.10, 0.30, 0.25), 0.8, 2.0, shadow=False)
    scene.render.film_transparent = True

cam_d = bpy.data.cameras.new('cam'); cam_d.lens = 100; cam_d.clip_start = 0.001
cam = bpy.data.objects.new('cam', cam_d); scene.collection.objects.link(cam)
h = (S['box'][1] - S['box'][0] + 9.0) / 1000
dist = h / (0.36 * 0.72)
target = Vector((0, 0, 0.004))
scene.render.resolution_x, scene.render.resolution_y = 1400, 1800
if CAM == 'tab':
    t = S['tab']
    target = to_b(((t['hx0'] + t['hx1']) / 2, t['yc'], t['zout']))
    cam.location = target + Vector((0.075, -0.06, 0.035))
    scene.render.resolution_x, scene.render.resolution_y = 1600, 1200
elif CAM == 'led':
    L = S['led']
    target = to_b((L['x'], 13.0, L['z']))
    cam.location = target + Vector((0.025, -0.12, 0.04))
    scene.render.resolution_x, scene.render.resolution_y = 1600, 1200
elif CAM == 'front':
    cam.location = target + Vector((0.06, -1.0, 0.10)).normalized() * dist
elif CAM == 'back':
    cam.location = target + Vector((-0.40, 0.84, 0.30)).normalized() * dist
else:
    cam.location = target + Vector((0.40, -0.84, 0.30)).normalized() * dist
cam.rotation_euler = (target - cam.location).to_track_quat('-Z', 'Y').to_euler()
scene.camera = cam

scene.render.engine = 'CYCLES'
scene.cycles.samples = SAMPLES
scene.cycles.use_denoising = True
try:
    prefs = bpy.context.preferences.addons['cycles'].preferences
    prefs.compute_device_type = 'METAL'
    prefs.get_devices()
    for dv in prefs.devices:
        dv.use = True
    scene.cycles.device = 'GPU'
except Exception:
    scene.cycles.device = 'CPU'
scene.view_settings.view_transform = os.environ.get('KC_VIEW', 'Standard')
scene.view_settings.exposure = float(os.environ.get('KC_EXPOSURE', '-0.35'))   # keeps the pale fronts out of clipping
scene.view_settings.look = 'None'
scene.render.filepath = OUT
bpy.ops.render.render(write_still=True)
print('wrote', OUT)

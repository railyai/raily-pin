#!/usr/bin/env python3
"""Bambu Studio projects for the Raily Keyring case, built from keyring_case.py.

    python bambu/build_bambu.py prepare [printer ...]   # profiles/, src/, jobs/ and run.sh
    bash bambu/run.sh                                   # slices every job with the Bambu Studio CLI (one at a time)
    python bambu/build_bambu.py stats                   # slice stats per plate -> bambu/stats.json, stats.md
    python bambu/build_bambu.py verify                  # every saved project: part -> filament slot as planned

KC_VERSION=v2 (a key of keyring_case.VERSIONS) builds that case version instead of v1: sources in src-v2/, jobs in
jobs-v2/, slices in out-v2/, projects, stats and previews in v2/, the script in run-v2.sh.

Profiles are Bambu Studio's own system presets (BBL bundle of the installed app), flattened here because the
CLI wants complete configs: printer "<model> 0.4 nozzle", process "0.16mm High Quality" (or the model's 0.16 mm
equivalent), filament = "Bambu PLA Matte" for that printer with Polymaker Panchroma Matte's density (1.37 g/cm3,
TDS V2.1) and colour. Every other filament value is Bambu PLA Matte's.

Parts are exported in print orientation (front = +Z): tray floor down, cover with the faceted front on top
(its pillars stand on the plate), shelf standing on its rib-side edge (v1.2 bay hold; flat with the posts up before). Colour regions are separate volumes of
one object, so the project carries the filament per region.
"""
import glob
import json
import os
import sys

import cadquery as cq

HERE = os.path.dirname(os.path.abspath(__file__))
CASE = os.path.dirname(HERE)
sys.path.insert(0, CASE)
import keyring_case as K   # noqa: E402

APP = '/Applications/BambuStudio.app'
CLI = APP + '/Contents/MacOS/BambuStudio'
BBL = APP + '/Contents/Resources/profiles/BBL'
VERSION = os.environ.get('KC_VERSION', 'v1')
if VERSION not in K.VERSIONS:
    sys.exit(f'unknown KC_VERSION {VERSION!r}; known: {", ".join(K.VERSIONS)}')
_SFX = '' if VERSION == 'v1' else f'-{VERSION}'
SRC = os.path.join(HERE, 'src' + _SFX)
PROF = os.path.join(HERE, 'profiles')
JOBS = os.path.join(HERE, 'jobs' + _SFX)
OUT = os.path.join(HERE, 'out' + _SFX)
ROOT = HERE if VERSION == 'v1' else os.path.join(HERE, VERSION)      # projects, stats, previews

# model -> (machine preset, label, bed x, bed y). Process and filament are picked by compatibility.
PRINTERS = dict(
    x1c=('Bambu Lab X1 Carbon 0.4 nozzle', 'X1 Carbon / X1E / X1'),
    p1s=('Bambu Lab P1S 0.4 nozzle', 'P1S'),
    p1p=('Bambu Lab P1P 0.4 nozzle', 'P1P'),
    p2s=('Bambu Lab P2S 0.4 nozzle', 'P2S'),
    a1=('Bambu Lab A1 0.4 nozzle', 'A1'),
    a1m=('Bambu Lab A1 mini 0.4 nozzle', 'A1 mini'),
    h2d=('Bambu Lab H2D 0.4 nozzle', 'H2D'),
    h2s=('Bambu Lab H2S 0.4 nozzle', 'H2S'),
)
PROCESS_PREF = ('0.16mm High Quality', '0.16mm Balanced Quality', '0.16mm Optimal', '0.16mm Standard')
SPOOLS = (('pm_cotton_white', 'Cotton White'), ('pm_pastel_periwinkle', 'Pastel Periwinkle'))   # AMS slot 1, 2
# Process changes after the owner's first print (A1 mini, 2026-09-28): travel avoids crossing walls (no strings through
# the countersinks and slits), plain Z lift before travel instead of the sloped 'Auto Lift' that drags a hair across
# a hole, a textured PEI plate (the owner's; the preset default is the Cool Plate at 35 C), and 0.1 mm elephant-foot
# compensation so the first layer does not close the countersinks' bed edge.
# #13 fence variants under the shelf edge: (mode, height, tab width)
FENCE_VARIANTS = {'A': ('full', 1.5, 0.0), 'B': ('full', 2.0, 0.0), 'C': ('ends', 1.5, 3.0), 'D': ('ends+mid', 1.5, 3.0),
                  'E': ('ends', 1.5, 4.0)}
PROTO = None             # prototype number for fit / test builds (--proto-number); None in production builds
# DFAM rules (2026-09-28, dfam/fdm-design-rules.md §F): the BBL max_bridge_length 0 makes tree support prop every
# bridge; bridges go unsupported up to 20 mm. counterbore_hole_bridging takes lowercase keys (none | partiallybridge |
# sacrificiallayer): the parts carry their own bridging, so 'none' (Studio >= 2.8 reads it; 2.6 ignores it).
PROCESS_OVERRIDES = {'reduce_crossing_wall': '1', 'max_travel_detour_distance': '0', 'z_hop_types': ['Normal Lift'],
                     'elefant_foot_compensation': '0.1', 'curr_bed_type': 'Textured PEI Plate',
                     'max_bridge_length': '20', 'bridge_no_support': '1', 'counterbore_hole_bridging': 'none'}
FAST_PROCESS = ('0.20mm Standard',)
# --material petg: the same geometry in PETG (owner, 2026-09-28; the spools arrive later). Bambu has no PETG Matte
# system preset for the A1 mini, so its PETG HF is the base (240 C nozzle); the textured PEI plate at 70 C. The
# profiles go to profiles/<printer>-petg/ and every job of the run uses them.
MATERIALS = {'pla': dict(filament=('Bambu PLA Matte @',), suffix='', density=None, overrides={}),
             'petg': dict(filament=('Bambu PETG HF @',), suffix='-petg', density=None,
                          overrides={'textured_plate_temp': ['70'], 'textured_plate_temp_initial_layer': ['70']})}
MATERIAL = 'pla'


def prof_dir(printer):
    """profiles/<printer> (PLA) or profiles/<printer>-petg: the directory a job loads."""
    return os.path.join(PROF, printer + MATERIALS[MATERIAL]['suffix'])   # test plates the lead wants fastest (#13): 0.20 mm layers


# ---------------------------------------------------------------- profiles
def _index():
    idx = {}
    for cat in ('machine', 'process', 'filament'):
        for f in glob.glob(os.path.join(BBL, cat, '**', '*.json'), recursive=True):
            try:
                d = json.load(open(f))
            except Exception:
                continue
            if 'name' in d:
                idx[(cat, d['name'])] = (f, d)
    return idx


def flatten(idx, cat, name):
    chain = []
    n = name
    while n:
        f, d = idx[(cat, n)]
        chain.append(d)
        n = d.get('inherits')
    out = {}
    for d in reversed(chain):
        # 'include' pulls in template presets (the A1 mini's start G-code, change-filament G-code, ...). Without them
        # the CLI falls back to fdm_machine_common's generic start G-code (M109 S205, no plate or nozzle routine),
        # which the v1 projects up to PR #1180 carried
        for inc in d.get('include', []):
            out.update({k: v for k, v in flatten(idx, cat, inc).items() if k not in ('name', 'from', 'instantiation')})
        out.update(d)
    out.pop('inherits', None)
    out.pop('include', None)
    out['name'] = name
    out['from'] = 'system'
    return out


def pick(idx, cat, machine, prefixes):
    for pre in prefixes:
        for (c, n), (f, d) in sorted(idx.items()):
            if c != cat or not n.startswith(pre) or d.get('instantiation') == 'false':
                continue
            cp = flatten(idx, cat, n).get('compatible_printers', [])
            if machine in cp:
                return n
    raise SystemExit(f'no {cat} for {machine} with {prefixes}')


def write_profiles(key, process_pref=PROCESS_PREF, dirname=None):
    """profiles/<key>/ (or profiles/<dirname>/ with another process, e.g. a1m-fast)."""
    idx = _index()
    machine, label = PRINTERS[key]
    d = os.path.join(PROF, (dirname or key) + MATERIALS[MATERIAL]['suffix']); os.makedirs(d, exist_ok=True)
    m = flatten(idx, 'machine', machine)
    proc_name = pick(idx, 'process', machine, process_pref)
    pr = flatten(idx, 'process', proc_name)
    pr.update(PROCESS_OVERRIDES)
    mat = MATERIALS[MATERIAL]
    fil_name = pick(idx, 'filament', machine, mat['filament'])
    json.dump(m, open(os.path.join(d, 'machine.json'), 'w'), indent=1)
    json.dump(pr, open(os.path.join(d, 'process.json'), 'w'), indent=1)
    files = []
    for i, (k, short) in enumerate(SPOOLS):
        f = flatten(idx, 'filament', fil_name)
        if MATERIAL == 'pla':
            f.update({'name': f'Polymaker Panchroma Matte {short} ({label})', 'from': 'User', 'inherits': fil_name,
                      'filament_vendor': ['Polymaker'], 'filament_density': [str(K.DATASHEET['pm_matte_density'])],
                      'filament_colour': [K.FILAMENTS[k]['hex']], 'filament_settings_id': [f'Polymaker Panchroma Matte {short}']})
        else:                     # system PETG values, our colours; the plate temperature per MATERIALS
            f.update({'name': f'PETG {short} ({label})', 'from': 'User', 'inherits': fil_name,
                      'filament_colour': [K.FILAMENTS[k]['hex']], 'filament_settings_id': [f'PETG {short}']})
            f.update(mat['overrides'])
        f.pop('setting_id', None)
        p = os.path.join(d, f'filament-{i + 1}.json')
        json.dump(f, open(p, 'w'), indent=1)
        files.append(p)
    return dict(machine=machine, process=proc_name, filament=fil_name, label=label,
                bed=m.get('printable_area'), files=files)


# ---------------------------------------------------------------- parts in print orientation
def src_dir(closure):
    """Part STLs: src/<closure>, or src-proto/<closure> for a numbered prototype, so a numbered part never reaches a
    production job."""
    return os.path.join(SRC if PROTO is None else SRC + '-proto', closure)


def _orient(w):
    """Case frame -> print frame: +y (front) becomes +Z; x stays the long axis."""
    return w.rotate((0, 0, 0), (1, 0, 0), 90)


def export_parts(closure, p=None, d=None, only=None):
    """src/<closure>/: one STL per colour region, each printed part dropped to Z = 0 as a group.
    p, d, only: another parameter set, source folder and part groups (the wall test plate)."""
    p = p or K.version_p(VERSION, closure=closure, proto=PROTO)
    b, info = K.parts(p)
    d = d or src_dir(closure); os.makedirs(d, exist_ok=True)
    groups = dict(tray=['tray', 'button', 'loop'], cover=['cover', 'led', 'support_fins'], shelf=['shelf', 'shelf_ribs'], frame=['frame', 'frame_tie'],
                  seat=['seat'])
    designed = p.cover_fins > 0 or (p.cover_split and not p.cover_onepiece)   # designed support / split cover: no painted support
    if only:
        groups = {g: v for g, v in groups.items() if g in only}
    meta = {}
    Ld = K.MEASURED['xiao_rgb_led']
    lx, lz = sum(Ld['x']) / 2, sum(Ld['z']) / 2
    blocker = K._cyl(lx, lz, 3.0, info['split'] - 3.0, info['facets']['top'] + 1.0)   # no support under the LED skin
    if p.face_bosses:             # #20: support only under the face's back (it floats on the bosses): blocked in the
        #  bosses and their nut slots, in the LED pocket, the dowel holes and the label; none on the crown (build plate only)
        sp_ = info['split']
        blocker = K._cyl(lx, lz, 3.0, sp_ - 0.01, info['facets']['top'] + 1.0)
        for d_ in info['cover_split']['dowels']:
            blocker = blocker.union(K._cyl(d_[0], d_[1], 2.4, sp_ - 0.01, sp_ + 3.0))
        zc_ = (info['box'][4] + info['box'][5]) / 2
        blocker = blocker.union(K._box(137.0, 143.0, sp_ - 0.01, sp_ + 0.6, zc_ - 9.0, zc_ + 9.0))
        xm_ = (info['box'][0] + info['box'][1]) / 2
        for bo in info['cover_split']['bosses']:
            hx, hz = bo['at']
            xa, xb = (hx - 4.8, hx + 10.8) if hx < xm_ else (hx - 10.8, hx + 4.8)
            blocker = blocker.union(K._box(xa, xb, K.MEASURED['pcb_y'][1], sp_ + 0.01, hz - 4.8, hz + 4.8))
        bx_ = info['box']
        enf_face = K._box(bx_[0] - 1, bx_[1] + 1, K.MEASURED['pcb_y'][1], sp_ + 0.3, bx_[4] - 1, bx_[5] + 1)
    if p.cover_onepiece:          # #19: the blocker only from the line up (under it the USB-end lip tip needs support);
        blocker = K._cyl(lx, lz, 3.0, info['split'] - 0.01, info['facets']['top'] + 1.0)
        # nor in the pillars' nut slots (45 degree roofs, they print without it)
        for hx, hz in info['fastening']['holes']:
            blocker = blocker.union(K._cyl(hx, hz, p.nut_boss_d / 2 + 0.8, K.MEASURED['pcb_y'][1] - 1, info['split'] + 0.5))
    X0, X1, Y0, Y1, Z0, Z1 = info['box']
    lp = info['loop']
    # supports only where painted: under the cover's inner face (lip, pillars' plate, catches) and under the loop
    enf_cover = K._box(X0 - 1, X1 + 1, K.MEASURED['pcb_y'][1] + p.pillar_gap, info['split'] + 0.3, Z0 - 1, Z1 + 1)
    enf_tray = K._box(X1 - 0.3, lp['end_x'] + 1, Y0, (Y0 + Y1) / 2 + p.loop_t / 2 + 0.3, Z0 - 1, Z1 + 1)
    helpers = ('led_blocker', 'support_enforcer')
    for g, names in groups.items():
        ws = {n: _orient(b[n]) for n in names if n in b}
        if not ws:
            continue
        if g == 'cover' and p.face_bosses:
            ws['led_blocker'] = _orient(blocker)
            ws['support_enforcer'] = _orient(enf_face)
        if g == 'cover' and not designed:
            ws['led_blocker'] = _orient(blocker)
            ws['support_enforcer'] = _orient(enf_cover)
        if g == 'tray' and (not designed or (p.loop_support and not p.loop_gusset)):
            ws['support_enforcer'] = _orient(enf_tray)
        if g == 'shelf' and p.shelf_support and not p.seat_split:
            # 17.2: grid support under the plate only (between the fence and the far edge); the fence and posts need none
            k_ = K.packing(p)
            sh_ = k_['parts']['shelf']
            fx1 = sh_[0] + (p.shelf_edge_clr or 0.2) + p.fence_t
            ybed = b['shelf'].val().BoundingBox().ymin
            ws['support_enforcer'] = _orient(K._box(fx1 + 0.3, X1, ybed - 0.5, sh_[2] + 0.1, Z0 - 1, Z1 + 1))
        if g == 'frame':
            # 10.3b: the parting face down, the pillars and the lip up
            ws = {n: b[n].rotate((0, 0, 0), (1, 0, 0), -90) for n in names if n in b}
        if g == 'shelf' and (not p.seat_posts or p.seat_split):
            # #13: no seat posts, the fence under the edge: top down, the fence stands up from the plate. No support.
            ws = {n: b[n].rotate((0, 0, 0), (1, 0, 0), -90) for n in names if n in b}
        if g == 'shelf' and info.get('hold') and not p.motor_posts:
            # v1.2: the shelf stands on its rib-side edge (case -x down): the leaf, its hook and the rails are then
            # (y, z) profiles grown straight up, and the leaf bends in the layer plane. No support.
            ws = {n: b[n].rotate((0, 0, 0), (0, 1, 0), -90) for n in names if n in b}
        zmin = min(w.val().BoundingBox().zmin for n, w in ws.items() if n not in helpers)
        for n, w in ws.items():
            w = w.translate((0, 0, -zmin))
            cq.exporters.export(w, os.path.join(d, f'{g}-{n}.stl'), tolerance=0.02, angularTolerance=0.12)
        bb = cq.Compound.makeCompound([v for n, w in ws.items() if n not in helpers for v in w.vals()]).BoundingBox()
        meta[g] = dict(size=(round(bb.xlen, 2), round(bb.ylen, 2), round(bb.zlen, 2)), min=(round(bb.xmin, 3), round(bb.ymin, 3)),
                       regions=list(ws))
    if not only:
        coupon = K.led_coupon(p)
        cq.exporters.export(coupon, os.path.join(SRC, 'led-coupon.stl'), tolerance=0.02, angularTolerance=0.12)
    json.dump(meta, open(os.path.join(d, 'parts.json'), 'w'), indent=1)
    return meta


# ---------------------------------------------------------------- plates
def region_filament(scheme, region, group=None):
    """1 = Cotton White, 2 = Pastel Periwinkle (AMS slots). Helper volumes take the part's own filament,
    so a one-colour plate stays one-colour (no prime tower)."""
    body = group if region in ('led_blocker', 'support_enforcer') else region
    k = K.body_filament(K.P(scheme=scheme), body, scheme)
    return 1 + [s for s, _ in SPOOLS].index(k)


# Supports only inside the painted zones (support_enforcer volumes): under the cover's inner face and under the
# loop. Grid supports, 4 mm apart: half the time of tree supports under the flat inner face, nothing reaches into
# the button or latch slits, and the LED skin has a blocker.
SUPPORT = {'enable_support': '1', 'support_type': 'normal(manual)', 'support_on_build_plate_only': '1',
           'support_base_pattern': 'rectilinear', 'support_base_pattern_spacing': '4'}
SHELF_SUPPORT = dict(SUPPORT, support_interface_top_layers='2', support_top_z_distance='0.2',
                     support_interface_spacing='0.5', support_base_pattern_spacing='2.5')
TOWER = 40.0          # prime tower footprint reserved on placed two-colour plates (35 mm + brim)
PROJECTS = ('v1', 'v5', 'v5-batch', 'v1-noams')


def part_objects(closure, scheme, group, first_index, count=1, at=None, src=None):
    """Assemble-list entries for `count` copies of one printed part; copy i is object first_index + i.
    at: [(x, y)] lower-left corner of each copy on the plate (None = let the slicer arrange).
    src: the part folder (default src/<closure>)."""
    src = src or src_dir(closure)
    meta = json.load(open(os.path.join(src, 'parts.json')))[group]
    objs = []
    for region in meta['regions']:
        o = dict(path=os.path.join(src, f'{group}-{region}.stl'), count=count,
                 filaments=[region_filament(scheme, region, group)],
                 assemble_index=[first_index + i for i in range(count)])
        if region in ('led_blocker', 'support_enforcer'):
            o['subtype'] = 'support_blocker' if region == 'led_blocker' else 'support_enforcer'
        if at:
            # the CLI moves copy 0 by pos[0], then clones copy 0 (already moved) and moves clone i by pos[i],
            # taking all three axes at index i: so pos[i] is the offset from copy 0 and pos_z needs every entry
            x0, y0 = at[0][0] - meta['min'][0], at[0][1] - meta['min'][1]
            o['pos_x'] = [x0] + [x - at[0][0] for x, _ in at[1:]]
            o['pos_y'] = [y0] + [y - at[0][1] for _, y in at[1:]]
            o['pos_z'] = [0.0] * count
        objs.append(o)
    # painted support only where the part carries an enforcer volume (designed-support versions carry none)
    params = [dict(assemble_index=first_index + i, print_params=dict(SUPPORT)) for i in range(count)] \
        if group in ('tray', 'cover') and 'support_enforcer' in meta['regions'] else []
    if group == 'shelf' and 'support_enforcer' in meta['regions']:
        # 17.2: grid (not tree), 2 interface layers, 0.2 top gap: the #7-loop support the owner found easy to remove
        params = [dict(assemble_index=first_index + i, print_params=dict(SHELF_SUPPORT)) for i in range(count)]
    if group == 'tray' and 'support_enforcer' in meta['regions'] and K.version_p(VERSION).loop_support:
        # #17: painted grid support under the loop only, plus Arachne for the tongue's ties
        params = [dict(assemble_index=first_index + i, print_params=dict(SUPPORT, wall_generator='arachne')) for i in range(count)]
    if group == 'tray' and 'support_enforcer' not in meta['regions']:
        # designed support (#10): Arachne keeps the 0.45 breakaway ties and the tongue's thin features (DFAM §F)
        params = [dict(assemble_index=first_index + i, print_params={'wall_generator': 'arachne'}) for i in range(count)]
    return objs, params


def bed_size(printer):
    pts = [tuple(float(v) for v in q.split('x'))
           for q in json.load(open(os.path.join(prof_dir(printer), 'machine.json')))['printable_area']]
    return max(p[0] for p in pts), max(p[1] for p in pts)


def common_area(printer):
    """The bed area every nozzle reaches (H2D: 25..325 of 350 in x), else the printable area."""
    m = json.load(open(os.path.join(prof_dir(printer), 'machine.json')))
    bw, bh = bed_size(printer)
    x0, y0, x1, y1 = 0.0, 0.0, bw, bh
    for area in m.get('extruder_printable_area') or []:
        pts = [tuple(float(v) for v in q.split('x')) for q in area.split(',') if q]
        x0, y0 = max(x0, min(p[0] for p in pts)), max(y0, min(p[1] for p in pts))
        x1, y1 = min(x1, max(p[0] for p in pts)), min(y1, max(p[1] for p in pts))
    return x0, y0, x1, y1


def tower_pos(printer):
    x0, y0, x1, y1 = common_area(printer)
    return x1 - 10 - TOWER, y1 - 10 - TOWER


def keep_outs(printer, tower):
    m = json.load(open(os.path.join(prof_dir(printer), 'machine.json')))
    bw, bh = bed_size(printer)
    out = []
    ex = [tuple(float(v) for v in q.split('x')) for q in m.get('bed_exclude_area', []) if q]
    if ex:
        out.append((min(p[0] for p in ex), min(p[1] for p in ex), max(p[0] for p in ex) + 2, max(p[1] for p in ex) + 2))
    cx0, cy0, cx1, cy1 = common_area(printer)
    if cx0 > 0:
        out.append((-50, -50, cx0 + 2, bh + 50))
    if cx1 < bw:
        out.append((cx1 - 2, -50, bw + 50, bh + 50))
    if tower:
        tx, ty = tower_pos(printer)
        out.append((tx - 4, ty - 4, tx + TOWER + 4, ty + TOWER + 4))
    return out


def pack(printer, closure, parts, tower, gap=6.0, margin=6.0, step=1.0, keep_gap=6.0):
    """First fit, bottom-left first, of [(group, count)] footprints on the bed, around keep-outs.
    gap: between parts; keep_gap: between a part and a keep-out (prime tower, excluded bed area).
    Returns {group: [(x, y)]} lower-left corners."""
    meta = json.load(open(os.path.join(SRC, closure, 'parts.json')))
    bw, bh = bed_size(printer)
    placed = [(x0 - keep_gap, y0 - keep_gap, x1 + keep_gap, y1 + keep_gap) for x0, y0, x1, y1 in keep_outs(printer, tower)]
    out = {}
    for group, count in parts:
        w, h = meta[group]['size'][:2]
        for _ in range(count):
            spot = None
            y = margin
            while spot is None and y + h <= bh - margin:
                x = margin
                while x + w <= bw - margin:
                    if all(x + w <= a or x >= c or y + h <= b or y >= d for a, b, c, d in placed):
                        spot = (x, y)
                        break
                    x += step
                y += step
            if spot is None:
                raise SystemExit(f'{printer}: {count} x {group} do not fit')
            out.setdefault(group, []).append(spot)
            placed.append((spot[0] - gap, spot[1] - gap, spot[0] + w + gap, spot[1] + h + gap))
    return out


def plate(name, parts, sequence='by object', layout=None):
    """parts: [(closure, scheme, group, count)]; layout: {group: [(x, y)]} from pack() or None (arrange)."""
    objs, params, k = [], [], 1
    for closure, scheme, group, count in parts:
        o, pa = part_objects(closure, scheme, group, k, count, at=(layout or {}).get(group))
        objs += o; params += pa; k += count
    return {'plate_name': name, 'need_arrange': layout is None, 'plate_params': {'print_sequence': sequence},
            'objects': objs, 'assembled_params': params}


def coupon_plate():
    return {'plate_name': 'LED coupon (print first)', 'need_arrange': True, 'plate_params': {},
            'objects': [dict(path=os.path.join(SRC, 'led-coupon.stl'), count=1, filaments=[2])]}


# The cover's grid support reaches past the cover's outline on its first layer (up to about 6 mm). The slicer's arrange
# puts a full case's parts 2 mm from the plate edge (and keeps them apart as print-by-object needs; a layout of our
# own with 8 mm edges failed that check on every 256 mm plate). On the 256 mm plates of the X1, P1S and P1P the cover's
# support then ran off the plate (v2, and v1.1 at its final depth), so there a case splits into a tray + shelf plate
# and a cover plate, as on the A1 mini. Batch covers stand 14 mm apart (6 mm let the supports collide).
EDGE, COVER_GAP = 8.0, 14.0
SPLIT_PLATES = ('x1c', 'p1s', 'p1p', 'a1m')


def case_plate(scheme, closure, label):
    return plate(f'{scheme.upper()} case {label}', [(closure, scheme, 'tray', 1), (closure, scheme, 'cover', 1),
                                                    (closure, scheme, 'shelf', 1)])


def case_plates(printer, scheme, closure, label):
    """One full case per plate, printed by object. On the A1 mini (180 mm bed) a full case does not fit by
    object with the tool-head clearance, so the case splits into a tray + shelf plate and a cover plate; so it does on the
    X1, P1S and P1P (SPLIT_PLATES)."""
    if printer not in SPLIT_PLATES:
        return [case_plate(scheme, closure, label)]
    return [plate(f'{scheme.upper()} case {label}: tray + shelf', [(closure, scheme, 'tray', 1), (closure, scheme, 'shelf', 1)]),
            plate(f'{scheme.upper()} case {label}: cover', [(closure, scheme, 'cover', 1)])]


def batch_count(printer):
    return 3 if printer == 'a1m' else 5


def projects(printer):
    n = batch_count(printer)
    trays = pack(printer, 'back', [('tray', n), ('shelf', n)], tower=True, margin=EDGE if n == 5 else 6.0)
    covers = pack(printer, 'back', [('cover', n)], tower=False, gap=COVER_GAP if n == 5 else 6.0, margin=EDGE if n == 5 else 6.0)
    return {
        'v1': [coupon_plate()] + case_plates(printer, 'v1', 'back', 'A (screws)') + case_plates(printer, 'v1', 'snap', 'B (snap-fit)'),
        'v5': [coupon_plate()] + case_plates(printer, 'v5', 'back', 'A (screws)') + case_plates(printer, 'v5', 'snap', 'B (snap-fit)'),
        'v5-batch': [plate(f'V5 {n} trays + {n} shelves (A)', [('back', 'v5', 'tray', n), ('back', 'v5', 'shelf', n)],
                           'by layer', trays),
                     plate(f'V5 {n} covers (A)', [('back', 'v5', 'cover', n)], 'by layer', covers)],
        'v1-noams': [plate('White: tray + shelf (A)', [('back', 'v1', 'tray', 1), ('back', 'v1', 'shelf', 1)]),
                     plate('Periwinkle: cover (A)', [('back', 'v1', 'cover', 1)]),
                     plate('White: tray + shelf (B)', [('snap', 'v1', 'tray', 1), ('snap', 'v1', 'shelf', 1)]),
                     plate('Periwinkle: cover (B)', [('snap', 'v1', 'cover', 1)])],
    }


# Wall test plate (owner, 2026-09-28): three closure-A trays side by side on the A1 mini, printed by layer, all in
# filament 1, each with its wall / floor thickness raised on the floor's inner face. New USB-C window, new button
# tongue and the countersink bosses in all three.
WALLTEST = (('1.0', dict(wall=1.0, floor=0.8)), ('1.25', dict(wall=1.25, floor=1.0)), ('1.6', dict(wall=1.6, floor=1.2)))
WT_SRC = os.path.join(HERE, 'src-walltest')


def walltest(printer='a1m', gap=8.0):
    """Exports the three trays and returns the plate, the trays stacked along the bed's Y, centred."""
    metas = {}
    for label, kw in WALLTEST:
        p = K.P(closure='back', label=label, **kw)
        metas[label] = export_parts('back', p, os.path.join(WT_SRC, label), only=('tray',))['tray']
    bw, bh = bed_size(printer)
    total = sum(m['size'][1] for m in metas.values()) + gap * (len(metas) - 1)
    y = (bh - total) / 2
    objs, params, k = [], [], 1
    for label, _ in WALLTEST:
        w, h = metas[label]['size'][:2]
        o, pa = part_objects('back', 'v1', 'tray', k, 1, at=[((bw - w) / 2, y)], src=os.path.join(WT_SRC, label))
        objs += o; params += pa; k += 1
        y += h + gap
    return {'plate_name': 'Wall test: trays 1.0 / 1.25 / 1.6 (A)', 'need_arrange': False,
            'plate_params': {'print_sequence': 'by layer'}, 'objects': objs, 'assembled_params': params}


def final_path(printer, project):
    return os.path.join(ROOT, f'{project}.3mf') if printer == 'x1c' else \
        os.path.join(ROOT, 'printers', printer, f'{project}.3mf')


def write_job(name, printer, plates, final, process_extra=None):
    """jobs/<name>.json + a per-job process file carrying the prime tower spot of each plate, then two CLI
    runs: slice (stats, G-code and a sliced 3MF in out/<name>/; retried, the CLI fails now and then) and an
    unsliced project 3MF at `final` (what goes into the repository and onto MakerWorld)."""
    os.makedirs(JOBS, exist_ok=True)
    jp = os.path.join(JOBS, f'{name}.json')
    json.dump({'plates': plates}, open(jp, 'w'), indent=1)
    pd = prof_dir(printer)
    proc = json.load(open(os.path.join(pd, 'process.json')))
    tx, ty = tower_pos(printer)
    proc.update(process_extra or {})
    proc['wipe_tower_x'] = [str(tx)] * len(plates)
    proc['wipe_tower_y'] = [str(ty)] * len(plates)
    pp = os.path.join(JOBS, f'{name}-process.json')
    json.dump(proc, open(pp, 'w'), indent=1)
    od = os.path.join(OUT, name)
    fil = ';'.join(os.path.join(pd, f'filament-{i + 1}.json') for i in range(len(SPOOLS)))
    base = (f'"{CLI}" --debug 2 --load-assemble-list "{jp}" --load-settings "{pd}/machine.json;{pp}" '
            f'--load-filaments "{fil}" --allow-multicolor-oneplate')
    fd = os.path.dirname(final)
    os.makedirs(fd, exist_ok=True)
    check = f"import json,sys; sys.exit(json.load(open('{od}/result.json'))['return_code'] != 0)"
    show = f"import json; d=json.load(open('{od}/result.json')); print(d['return_code'], d['error_string'])"
    return (f'mkdir -p "{od}" "{fd}"; rm -f "{od}"/plate_*.gcode "{od}/result.json"\n'
            f'for try in 1 2 3; do\n'
            f'  {base} --slice 0 --outputdir "{od}" --export-3mf "{name}-sliced.3mf" > "{od}/cli.log" 2>&1\n'
            f'  python3 -c "{check}" && break\n'
            f'done\n'
            f'{base} --outputdir "{fd}" --export-3mf "{os.path.basename(final)}" > "{od}/cli-project.log" 2>&1\n'
            f'rm -f "{fd}/result.json"\n'
            f'echo "{name}: $(python3 -c "{show}")"\n')


def fix_project(final, sliced):
    """The unsliced export leaves settings empty that the slice fills in (on the H2D: extruder_nozzle_stats, without
    which the CLI refuses to slice the project: "No valid nozzle found"). Copy every such value from the sliced
    project into the saved one."""
    import zipfile
    name = 'Metadata/project_settings.config'
    with zipfile.ZipFile(sliced) as z:
        s = json.loads(z.read(name))
    with zipfile.ZipFile(final) as z:
        items = [(i, z.read(i.filename)) for i in z.infolist()]
    u = json.loads(dict((i.filename, d) for i, d in items)[name])
    filled = [k for k, v in s.items() if k in u and u[k] in ([], '', ['']) and v not in ([], '', [''])]
    if not filled:
        return filled
    for k in filled:
        u[k] = s[k]
    tmp = final + '.tmp'
    with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED) as z:
        for i, d in items:
            z.writestr(i, json.dumps(u, indent=4).encode() if i.filename == name else d)
    os.replace(tmp, final)
    return filled


# keys the 2.6 CLI does not know and drops from the saved projects: written into project_settings.config afterwards
PROJECT_ONLY = {'counterbore_hole_bridging': 'none'}


def patch_settings(path, values=None):
    """Set process keys in a saved (or sliced) project's Metadata/project_settings.config."""
    import zipfile
    values = values or PROJECT_ONLY
    name = 'Metadata/project_settings.config'
    with zipfile.ZipFile(path) as z:
        items = [(i, z.read(i.filename)) for i in z.infolist()]
    u = json.loads(dict((i.filename, d) for i, d in items)[name])
    u.update(values)
    tmp = path + '.tmp'
    with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED) as z:
        for i, d in items:
            z.writestr(i, json.dumps(u, indent=4).encode() if i.filename == name else d)
    os.replace(tmp, path)
    return values


def set_layer_profiles(path, profiles):
    """Variable layer height per object: Metadata/layer_heights_profile.txt, one 'object_id=N|z;h;z;h...' line per
    object (N = the object's 1-based order in the project, Slic3r's format that Bambu Studio reads)."""
    import zipfile
    name = 'Metadata/layer_heights_profile.txt'
    with zipfile.ZipFile(path) as z:
        items = [(i, z.read(i.filename)) for i in z.infolist() if i.filename != name]
    text = ''.join(f'object_id={n}|' + ';'.join(f'{v:.6f}' for v in prof) + '\n' for n, prof in sorted(profiles.items()))
    tmp = path + '.tmp'
    with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED) as z:
        for i, d in items:
            z.writestr(i, d)
        z.writestr(name, text)
    os.replace(tmp, path)
    return text


# #14 crown-finish coupon: B and C print the crown at 0.08 with a top-surface set (ironing on the topmost surface,
# monotonic top lines, more top shells, slower outer wall and top, seam at the back), gyroid sparse infill
CROWN_FINISH = {'ironing_type': 'topmost', 'top_surface_pattern': 'monotonicline', 'top_shell_layers': '8',
                'top_shell_thickness': '1.0', 'outer_wall_speed': '60', 'top_surface_speed': '60',
                'seam_position': 'back', 'sparse_infill_pattern': 'gyroid', 'sparse_infill_density': '15%',
                'layer_height': '0.08'}    # the whole coupon (4.8 tall; the per-layer profile file is not read by the CLI)


def force_single_filament(path, slot=None):
    """Single-colour plates (the lead, 2026-09-29: desktop Studio took objects without their own extruder as slot 1, so
    the P10-frame dowels and tie printed in the other colour, 31 changes and a tower): every object AND every part gets
    the same extruder. slot None: the one filament the sliced project's slice_info lists; a sliced file must list exactly
    one filament and 0 changes (AssertionError otherwise). Returns the slot."""
    import re
    import zipfile
    with zipfile.ZipFile(path) as z:
        items = [(i, z.read(i.filename)) for i in z.infolist()]
    files = dict((i.filename, d) for i, d in items)
    si = files.get('Metadata/slice_info.config')
    if si is not None:
        si = si.decode()
        ids = sorted(set(int(v) for v in re.findall(r'<filament id="(\d+)"', si)))
    if si is not None and ids:        # a sliced file
        assert len(ids) == 1, f'{path}: slice_info lists filaments {ids}'
        ch = re.findall(r'key="filament_change_times" value="(\d+)"', si)
        assert all(c == '0' for c in ch), f'{path}: filament changes {ch}'
        assert slot is None or slot == ids[0], f'{path}: sliced with filament {ids[0]}, not {slot}'
        slot = slot or ids[0]
    assert slot, 'no slot: pass one for an unsliced project'
    ms = files['Metadata/model_settings.config'].decode()
    ms = re.sub(r'\s*<metadata key="extruder" value="\d+"/>', '', ms)          # idempotent: one per object / part
    ms = re.sub(r'(<(?:object|part) [^>]*>)(\s*)',
                lambda m: m.group(1) + m.group(2) + f'<metadata key="extruder" value="{slot}"/>' + m.group(2), ms)
    tmp = path + '.tmp'
    with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED) as z:
        for i, d in items:
            z.writestr(i, ms.encode() if i.filename == 'Metadata/model_settings.config' else d)
    os.replace(tmp, path)
    return slot


# the lightest face shell (lead, 2026-09-29, from the face-alone slices: 9.19 g -> 6.36 g): 10% gyroid, 2 bottom and 6 top
# layers at 0.08 (0.48 top skin), ironing on every top surface (the pillow crown's contour loops)
SHELL_G1 = {'sparse_infill_density': '10%', 'bottom_shell_layers': '2', 'top_shell_thickness': '0', 'top_shell_layers': '6',
            'ironing_type': 'top'}
# #18 (owner, 2026-09-29): the G1 gyroid showed through its 0.48 top and was felt by touch: 10 top layers at 0.08 (0.8)
# over 15% gyroid (the face-next-options table: 8.3 g, 55 min at the current crown)
SHELL_T08 = dict(SHELL_G1, sparse_infill_density='15%', top_shell_layers='10')
FACE_SHELLS = {'G1': SHELL_G1, 'T08': SHELL_T08}


def verify_project(final, job):
    """Read a saved project back: every plate must hold the planned number of copies of each part, and every part
    the filament slot the job planned for its source STL (slot 1 = Cotton White body, slot 2 = Pastel Periwinkle
    front). Returns the mismatches."""
    import zipfile
    import xml.etree.ElementTree as ET
    missing = [f for f in (final, job) if not os.path.exists(f)]
    if missing:
        return [f'missing {m}: run prepare and the run script first' for m in missing]
    plan = json.load(open(job))['plates']
    with zipfile.ZipFile(final) as z:
        cfg = ET.fromstring(z.read('Metadata/model_settings.config'))
    objs = {}
    for o in cfg.findall('object'):
        parts = []
        oe = {m.get('key'): m.get('value') for m in o.findall('metadata')}.get('extruder', '1')
        for pt in o.findall('part'):      # a part without its own extruder prints with the object's
            md = {m.get('key'): m.get('value') for m in pt.findall('metadata')}
            parts.append((md.get('source_file'), pt.get('subtype'), md.get('extruder', oe)))
        objs[o.get('id')] = parts
    errs = []
    plates = cfg.findall('plate')
    if len(plates) != len(plan):
        errs.append(dict(plate=None, got=len(plates), expected=len(plan)))
    for pl, want in zip(plates, plan):
        ids = [m.get('value') for mi in pl.findall('model_instance') for m in mi.findall('metadata') if m.get('key') == 'object_id']
        # one entry per placed copy, so a lost copy of a batch plate shows as a mismatch
        got = sorted((f, e) for i in ids for f, st, e in objs.get(i, []) if st == 'normal_part')
        exp = sorted((os.path.basename(o['path']), str(o['filaments'][0])) for o in want['objects'] if 'subtype' not in o
                     for _ in range(o.get('count', 1)))
        if got != exp:
            errs.append(dict(plate=want['plate_name'], got=got, expected=exp))
    return errs


def slicer_used_g(sliced):
    """{plate: {filament id: grams}} from a sliced project's slice_info (the slicer's total, flush included)."""
    import re
    import zipfile
    if not os.path.exists(sliced):
        return {}
    si = zipfile.ZipFile(sliced).read('Metadata/slice_info.config').decode()
    out = {}
    for pl in re.findall(r'<plate>.*?</plate>', si, re.S):
        m = re.search(r'key="index" value="(\d+)"', pl)
        if not m:
            continue
        n = int(m.group(1))
        out[n] = {int(i): float(g) for i, g in re.findall(r'<filament id="(\d+)"[^>]*used_g="([\d.]+)"', pl)}
    return out


def write_stats_md(rows):
    """stats.md: one table per printer. Grams from the G-code: part (model + support) per colour, and purge
    (flush at each change + prime tower) for both colours together."""
    names = {1: 'White', 2: 'Periwinkle'}
    out = ['# Slice stats per plate' + ('' if VERSION == 'v1' else f' (case {VERSION})'), '',
           'Bambu Studio 02.06.00.51 CLI, system presets, 0.16 mm layers, Panchroma Matte (density 1.37). '
           'Grams come from the G-code extrusion: *part* = model + support, *purge* = the flush at each colour '
           'change + the prime tower (P2S and H2S flush in firmware, `M620.10`: their purge is the slicer\'s filament total '
           'minus the G-code extrusion). Times are the slicer estimate. Generated by `build_bambu.py stats`.', '']
    for key, (machine, label) in PRINTERS.items():
        rs = [r for r in rows if r['printer'] == key]
        if not rs:
            continue
        out += [f'## {label}', '', '| Project | Plate | Time | Colour changes | White, part | Periwinkle, part | '
                'Support (in the part grams) | Purge | Total |', '| --- | --- | --- | --- | --- | --- | --- | --- | --- |']
        for r in rs:
            g = r['grams']
            part = {i: sum(v for k, v in g.get(f'filament_{i}', {}).items() if k in ('model', 'support')) for i in (1, 2)}
            sup = sum(g.get(f'filament_{i}', {}).get('support', 0) for i in (1, 2))
            purge = sum(g.get(f'filament_{i}', {}).get('purge', 0) for i in (1, 2))
            tot = sum(v for f in g.values() for v in f.values())
            h, m = divmod(r['minutes'], 60)
            out.append(f"| {r['project']} | {r['plate']}. {r['name']} | {h} h {m:02d} m | {r['changes']} | "
                       f"{part[1]:.1f} g | {part[2]:.1f} g | {sup:.1f} g | {purge:.1f} g | {tot:.1f} g |")
        out.append('')
    os.makedirs(ROOT, exist_ok=True)
    open(os.path.join(ROOT, 'stats.md'), 'w').write('\n'.join(out))


def main():
    global PROTO, MATERIAL
    if '--material' in sys.argv:          # --material pla | petg
        i = sys.argv.index('--material')
        MATERIAL = sys.argv[i + 1]
        if MATERIAL not in MATERIALS:
            sys.exit(f'unknown material {MATERIAL!r}; known: {", ".join(MATERIALS)}')
        del sys.argv[i:i + 2]
    # --proto-number N: fit and test builds only. The number goes on every printed part (debossed, hidden: tray
    # floor, shelf underside, coupon front); the production and published builds never pass it.
    if '--proto-number' in sys.argv:
        i = sys.argv.index('--proto-number')
        PROTO = int(sys.argv[i + 1])
        del sys.argv[i:i + 2]
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'prepare'
    if PROTO is not None and cmd not in ('fit', 'shelftest', 'shelf2b', 'shelf3', 'shelf3b', 'shelf3c', 'traya', 'fitcoupon', 'fencevariants', 'crowncoupon', 'pegcoupon', 'cover2', 'frame', 'crownsection', 'shelfC', 'buttoncoupon', 'edgecoupon', 'confirm', 'walltest', 'parts'):
        sys.exit(f'--proto-number is for fit / test builds only, not {cmd!r}')
    # printer keys become paths and shell words in run.sh: accept known keys only
    if cmd in ('profiles', 'prepare', 'previews', 'fix'):
        bad = [k for k in sys.argv[2:] if k not in PRINTERS]
        if bad:
            sys.exit(f'unknown printer {bad}; known: {", ".join(PRINTERS)}')
    if cmd == 'profiles':
        for key in sys.argv[2:] or PRINTERS:
            print(key, json.dumps({k: v for k, v in write_profiles(key).items() if k != 'files'}))
    elif cmd == 'edgecoupon':    # five bottom-edge profiles on the A1 mini, white, by layer
        d = os.path.join(HERE, 'src-edgecoupon'); os.makedirs(d, exist_ok=True)
        objs = []
        for i, (label, prof) in enumerate(K.EDGE_PROFILES):
            stl = os.path.join(d, f'edge-{i + 1}.stl')
            cq.exporters.export(K.edge_coupon(prof, label), stl, tolerance=0.01, angularTolerance=0.1)
            objs.append(dict(path=stl, count=1, filaments=[1], assemble_index=[i + 1],
                             pos_x=[30.0 + (i % 3) * 42.0], pos_y=[50.0 + (i // 3) * 40.0], pos_z=[0.0]))
        plates = [{'plate_name': 'Bottom-edge coupon: C1.2 C0.8 R1 R2.5 C+r', 'need_arrange': False,
                   'plate_params': {'print_sequence': 'by layer'}, 'objects': objs, 'assembled_params': []}]
        final = os.path.join(HERE, 'edgecoupon', 'a1m-edge-coupon.3mf')
        open(os.path.join(HERE, 'run-edgecoupon.sh'), 'w').write('#!/bin/bash\n' + write_job('a1m-edgecoupon', 'a1m', plates, final))
        print('bambu/run-edgecoupon.sh ->', final)
    elif cmd == 'confirm':       # one case A on the A1 mini for a confirmation print: tray + shelf plate, cover plate
        plates = case_plates('a1m', 'v1', 'back', 'A (screws)')
        final = os.path.join(HERE, 'walltest', 'a1m-confirm.3mf')
        open(os.path.join(HERE, 'run-confirm.sh'), 'w').write('#!/bin/bash\n' + write_job('a1m-confirm', 'a1m', plates, final))
        print('bambu/run-confirm.sh ->', final)
    elif cmd == 'buttoncoupon':  # side-button variants on a strip of the +z wall (A1 mini), white, by layer
        d = os.path.join(HERE, 'src-buttoncoupon'); os.makedirs(d, exist_ok=True)
        stl = os.path.join(d, 'button-coupon.stl')
        cq.exporters.export(K.button_coupon(PROTO), stl, tolerance=0.01, angularTolerance=0.1)
        bw, bh = bed_size('a1m')
        L = K.BC['first'] + K.BC['cell'] * len(K.BC_VARIANTS)
        objs = [dict(path=stl, count=1, filaments=[1], assemble_index=[1],
                     pos_x=[(bw - L) / 2], pos_y=[bh / 2 - 4.0], pos_z=[0.0])]
        plates = [{'plate_name': 'Side-button coupon: ' + ' '.join(K.BC_VARIANTS), 'need_arrange': False,
                   'plate_params': {'print_sequence': 'by layer'}, 'objects': objs, 'assembled_params': []}]
        final = os.path.join(HERE, 'edgecoupon', 'a1m-button-coupon.3mf')
        open(os.path.join(HERE, 'run-buttoncoupon.sh'), 'w').write('#!/bin/bash\n' + write_job('a1m-buttoncoupon', 'a1m', plates, final))
        print('bambu/run-buttoncoupon.sh ->', final, K.button_coupon_mechanics())
    elif cmd == 'fencevariants':  # #13: the #11 shelf with under-edge fence variants A-E, top down, one A1 mini plate
        write_profiles('a1m', FAST_PROCESS, 'a1m-fast')
        objs = []
        for i, (v, fence) in enumerate(FENCE_VARIANTS.items()):
            p = K.version_p('v1.1-fence', proto=PROTO, under_fence=fence, part_suffix=v)
            d = os.path.join(HERE, 'src-fence', v)
            export_parts('back', p, d, only=('shelf',))
            objs.append(dict(path=os.path.join(d, 'shelf-shelf.stl'), count=1, filaments=[1], assemble_index=[i + 1]))
        plates = [{'plate_name': 'Shelf fence variants ' + ' '.join(FENCE_VARIANTS), 'need_arrange': True,
                   'plate_params': {'print_sequence': 'by layer'}, 'objects': objs, 'assembled_params': []}]
        final = os.path.join(HERE, 'walltest', 'a1m-fencevariants.3mf')
        open(os.path.join(HERE, 'run-fencevariants.sh'), 'w').write('#!/bin/bash\n' + write_job('a1m-fencevariants', 'a1m-fast', plates, final))
        print('bambu/run-fencevariants.sh ->', final)
    elif cmd == 'crowncoupon':   # #14: three crown-finish coupons (A current, B 0.08 + top set, C = B + round creases)
        d = os.path.join(HERE, 'src-crown'); os.makedirs(d, exist_ok=True)
        objs, params = [], []
        bw, bh = bed_size('a1m')
        for i, (v, kw) in enumerate((('A', {}), ('B', {}), ('C', {'crease_r': 1.5}))):
            p = K.version_p('v1', led_land=4.6, **kw)
            c, ci = K.crown_coupon(p, label=f'{PROTO}{v}' if PROTO is not None else None)
            w = c.rotate((0, 0, 0), (1, 0, 0), 90)         # crown up: case +y -> +Z
            bb = w.val().BoundingBox()
            w = w.translate((-bb.xmin, -bb.ymin, -bb.zmin))
            stl = os.path.join(d, f'crown-{v}.stl')
            cq.exporters.export(w, stl, tolerance=0.01, angularTolerance=0.1)
            objs.append(dict(path=stl, count=1, filaments=[2], assemble_index=[i + 1],
                             pos_x=[(bw - bb.xlen) / 2], pos_y=[20.0 + i * (bb.ylen + 12.0)], pos_z=[0.0]))
            if v != 'A':
                params.append(dict(assemble_index=i + 1, print_params=dict(CROWN_FINISH)))
        plates = [{'plate_name': 'Crown finish 14A 14B 14C', 'need_arrange': False,
                   'plate_params': {'print_sequence': 'by layer'}, 'objects': objs, 'assembled_params': params}]
        final = os.path.join(HERE, 'walltest', 'a1m-crowncoupon.3mf')
        open(os.path.join(HERE, 'run-crowncoupon.sh'), 'w').write('#!/bin/bash\n' + write_job('a1m-crowncoupon', 'a1m', plates, final))
        json.dump(ci, open(os.path.join(d, 'coupon.json'), 'w'))
        print('bambu/run-crowncoupon.sh ->', final, ci)
    elif cmd == 'shelfC':        # 10.2C alone (one piece: 13.2B fence + 11.2 posts, breakaway ribs), white, fast, top up
        write_profiles('a1m', FAST_PROCESS, 'a1m-fast')
        p = K.version_p(VERSION, closure='back', proto=PROTO, seat_split=False, shelf_ribs=True, part_suffix='C')
        d = os.path.join(src_dir('back') + '-shelfC'); os.makedirs(d, exist_ok=True)
        meta = export_parts('back', p, d, only=('shelf',))
        objs = [dict(path=os.path.join(d, f'shelf-{r}.stl'), count=1, filaments=[1], assemble_index=[1])
                for r in meta['shelf']['regions']]
        plates = [{'plate_name': '10.2C shelf + seat, one piece', 'need_arrange': True,
                   'plate_params': {'print_sequence': 'by layer'}, 'objects': objs, 'assembled_params': []}]
        final = os.path.join(ROOT, 'walltest', 'a1m-shelfC.3mf')
        open(os.path.join(HERE, f'run-shelfC{_SFX}.sh'), 'w').write('#!/bin/bash\n' + write_job(
            f'a1m-shelfC{_SFX}', 'a1m-fast', plates, final, process_extra={'enable_prime_tower': '0'}))
        print(f'bambu/run-shelfC{_SFX}.sh ->', final, meta)
    elif cmd in ('shelf2b', 'shelf3', 'shelf3b', 'shelf3c'):   # 17.2b alone: 13.2B source + 11.2 posts, fitted to the printed 17.1; white, 0.16, top up
        # posts up, the fence (0.95 foot) on the bed with a 5 mm brim, the plate on painted grid support, first layer 50 %
        # shelf3: 17.3 (DA7280 on the plate, three ribbed pins, the partition ledge on the same grid support)
        S, inf = dict(shelf2b=K.shelf_17_2b, shelf3=K.shelf_17_3, shelf3b=K.shelf_17_3b, shelf3c=K.shelf_17_3c)[cmd](proto=None if os.environ.get('KC_NOLABEL') else (PROTO if PROTO is not None else 17))
        d = os.path.join(HERE, f'src-{cmd}'); os.makedirs(d, exist_ok=True)
        p17 = inf['p17']
        sh = K.packing(p17)['parts']['shelf']
        box17 = K.outer_box(p17)
        ybed = S.val().BoundingBox().ymin
        fx1 = sh[0] + (p17.shelf_edge_clr or 0.2) + p17.fence_t
        enf = K._box(fx1 + 0.3, box17[1], ybed - 0.5, sh[2] + 0.1, box17[4] - 1, box17[5] + 1)
        if cmd in ('shelf3', 'shelf3b', 'shelf3c'):   # under the partition ledge too (outboard of the fence)
            lx0, lx1, ly0, _ = inf['ledge']
            enf = enf.union(K._box(lx0 - 0.3, fx1 + 0.3, ybed - 0.5, ly0 + 0.1, inf['place']['zlo'] - 1.5,
                                   inf['place']['zlo'] + K.DA7280['size'][1] + 1.5))
        # the slicer drops its brim once support is on (Studio 2.6 CLI, tested): a designed 5 mm brim, one 0.2 layer,
        # 0.1 off the fence feet (the slicer brim's gap); a print aid snapped off after printing, not part geometry
        fb = S.val().intersect(K._box(150, 260, ybed - 0.01, ybed + 0.05, -60, 60).val())
        brim = None
        for f_ in fb.Solids():
            o = f_.BoundingBox()
            ring = K._box(o.xmin - 5, o.xmax + 5, ybed, ybed + 0.2, o.zmin - 5, o.zmax + 5).cut(
                K._box(o.xmin - 0.1, o.xmax + 0.1, ybed - 1, ybed + 1, o.zmin - 0.1, o.zmax + 0.1))
            brim = ring if brim is None else brim.union(ring)
        for f_ in fb.Solids():
            o = f_.BoundingBox()
            brim = brim.cut(K._box(o.xmin - 0.1, o.xmax + 0.1, ybed - 1, ybed + 1, o.zmin - 0.1, o.zmax + 0.1))
        ws = {'shelf': _orient(S), 'brim': _orient(brim), 'support_enforcer': _orient(enf)}
        zmin = ws['shelf'].val().BoundingBox().zmin
        for n, w in ws.items():
            cq.exporters.export(w.translate((0, 0, -zmin)), os.path.join(d, f'{cmd}-{n}.stl'), tolerance=0.02, angularTolerance=0.12)
        objs = [dict(path=os.path.join(d, f'{cmd}-shelf.stl'), count=1, filaments=[1], assemble_index=[1]),
                dict(path=os.path.join(d, f'{cmd}-brim.stl'), count=1, filaments=[1], assemble_index=[1]),
                dict(path=os.path.join(d, f'{cmd}-support_enforcer.stl'), count=1, filaments=[1], assemble_index=[1],
                     subtype='support_enforcer')]
        pp = dict(SHELF_SUPPORT, brim_type='outer_only', brim_width='5', initial_layer_speed='25', initial_layer_infill_speed='52')
        plates = [{'plate_name': '17.2b shelf + seat (13.2B + 11.2)' if cmd == 'shelf2b' else f'17.3{dict(shelf3b="b", shelf3c="c").get(cmd, "")} shelf (DA7280, 3 ribbed pins)',
                   'need_arrange': True, 'plate_params': {'print_sequence': 'by layer'}, 'objects': objs,
                   'assembled_params': [dict(assemble_index=1, print_params=pp)]}]
        final = os.path.join(ROOT, 'walltest', f'a1m-{cmd}.3mf')
        open(os.path.join(HERE, f'run-{cmd}{_SFX}.sh'), 'w').write('#!/bin/bash\n' + write_job(
            f'a1m-{cmd}{_SFX}', 'a1m', plates, final,
            process_extra={'enable_prime_tower': '0', 'brim_type': 'outer_only', 'brim_width': '5',
                           'initial_layer_speed': ['25'], 'initial_layer_infill_speed': ['52']}))   # the CLI ignores these per object
        print(f'bambu/run-{cmd}{_SFX}.sh ->', final, inf['trims'], inf['key'])
    elif cmd == 'crownsection':  # #16: three full-width crown slices, 14C finish, blue, one filament
        d = os.path.join(HERE, 'src-crown16'); os.makedirs(d, exist_ok=True)
        objs, params = [], []
        bw, bh = bed_size('a1m')
        variants = (('A', dict(crease_r=20.0)), ('B', dict(crease_r=40.0)), ('C', dict(crown_style='pillow')))
        info16 = {}
        for i, (v, kw) in enumerate(variants):
            p = K.version_p('v1.2', **kw)
            c, ci = K.crown_section(p, label=f'{PROTO}{v}' if PROTO is not None else None)
            w = c.rotate((0, 0, 0), (1, 0, 0), 90)
            bb = w.val().BoundingBox()
            w = w.translate((-bb.xmin, -bb.ymin, -bb.zmin))
            stl = os.path.join(d, f'crown16-{v}.stl')
            cq.exporters.export(w, stl, tolerance=0.01, angularTolerance=0.1)
            objs.append(dict(path=stl, count=1, filaments=[2], assemble_index=[i + 1],
                             pos_x=[30.0 + i * (bb.xlen + 14.0)], pos_y=[(bh - bb.ylen) / 2], pos_z=[0.0]))
            pp = dict(CROWN_FINISH)
            pp.update(SHELL_G1)          # #16 re-slice (lead): the lightest shell + ironing on every top
            params.append(dict(assemble_index=i + 1, print_params=pp))
            info16[v] = dict(kw=kw, **ci)
        plates = [{'plate_name': 'Crown sections 16A 16B 16C', 'need_arrange': False,
                   'plate_params': {'print_sequence': 'by layer'}, 'objects': objs, 'assembled_params': params}]
        final = os.path.join(HERE, 'walltest', 'a1m-crown16.3mf')
        open(os.path.join(HERE, 'run-crown16.sh'), 'w').write('#!/bin/bash\n' + write_job(
            'a1m-crown16', 'a1m', plates, final, process_extra={'enable_prime_tower': '0'}))
        print('bambu/run-crown16.sh ->', final, info16)
    elif cmd == 'pegcoupon':     # #15: the 10.3a/b dowel press fit, three clearances, white, 0.16
        d = os.path.join(HERE, 'src-peg'); os.makedirs(d, exist_ok=True)
        blk, peg, pi = K.peg_coupon(PROTO if PROTO is not None else 15)
        bs, ps = os.path.join(d, 'peg-block.stl'), os.path.join(d, 'peg.stl')
        cq.exporters.export(blk, bs, tolerance=0.005, angularTolerance=0.05)
        cq.exporters.export(peg, ps, tolerance=0.005, angularTolerance=0.05)
        bw, bh = bed_size('a1m')
        L = pi['block'][0]
        objs = [dict(path=bs, count=1, filaments=[1], assemble_index=[1], pos_x=[(bw - L) / 2], pos_y=[bh / 2 - 6.0], pos_z=[0.0]),
                dict(path=ps, count=3, filaments=[1], assemble_index=[2, 3, 4],
                     pos_x=[(bw - L) / 2 + 8.0, 10.0, 20.0], pos_y=[bh / 2 + 12.0, 0.0, 0.0], pos_z=[0.0, 0.0, 0.0])]
        plates = [{'plate_name': 'Peg coupon ' + ' '.join(str(h) for h in pi['holes']), 'need_arrange': False,
                   'plate_params': {'print_sequence': 'by layer'}, 'objects': objs, 'assembled_params': []}]
        final = os.path.join(HERE, 'walltest', 'a1m-pegcoupon.3mf')
        open(os.path.join(HERE, 'run-pegcoupon.sh'), 'w').write('#!/bin/bash\n' + write_job('a1m-pegcoupon', 'a1m', plates, final))
        print('bambu/run-pegcoupon.sh ->', final, pi)
    elif cmd == 'fitcoupon':     # #18.1e: face-hole strip (3 rib crests, face print settings) + frame-hole strip (the
        # new stops: neck ring vs solid floor) + 4 round and 2 D-flat dowels on a sprue; blue, one plate: the fit is felt before any full cover prints
        p = K.version_p(os.environ.get('KC_VERSION', 'v1.3-p18.1e'), closure='back')
        face, frame, fi = K.fit_coupon(p)
        spr, _ = K.dowel_sprue(p, 4, 2, labels=('O', 'D'), flats=(0.0, 0.3))   # 4 round (18.1e) + 2 D-flat (18.1c/d)
        d = os.path.join(HERE, f'src-fitcoupon{_SFX}'); os.makedirs(d, exist_ok=True)
        objs = []
        for i, (n, w) in enumerate((('face-strip', face), ('frame-strip', frame), ('sprue', spr))):
            pth = os.path.join(d, f'{n}.stl')
            cq.exporters.export(w, pth, tolerance=0.005, angularTolerance=0.05)
            objs.append(dict(path=pth, count=1, filaments=[2], assemble_index=[i + 1]))
        face_pp = dict(CROWN_FINISH)
        if p.face_shell:
            face_pp.update(FACE_SHELLS[p.face_shell])
        plates = [{'plate_name': 'Fit coupon: face ribs ' + ' / '.join(str(t) for t in fi['tips']) + ', frame stops',
                   'need_arrange': True, 'plate_params': {'print_sequence': 'by layer'}, 'objects': objs,
                   'assembled_params': [dict(assemble_index=1, print_params=face_pp)]}]
        final = os.path.join(HERE, 'walltest', f'a1m-fitcoupon{_SFX}.3mf')
        open(os.path.join(HERE, f'run-fitcoupon{_SFX}.sh'), 'w').write('#!/bin/bash\n' + write_job(
            f'a1m-fitcoupon{_SFX}', 'a1m', plates, final, process_extra={'enable_prime_tower': '0'}))
        print(f'bambu/run-fitcoupon{_SFX}.sh ->', final, fi)
    elif cmd == 'pusher':        # the dowel pusher alone (KC_VERSION's pusher_cups), blue, 0.16, palm end down
        p = K.version_p(os.environ.get('KC_VERSION', 'v1.3'), closure='back')
        _, push = K.dowel_sprue(p, 1, 1)
        d = os.path.join(HERE, 'src-pusher'); os.makedirs(d, exist_ok=True)
        pth = os.path.join(d, 'pusher.stl')
        cq.exporters.export(push, pth, tolerance=0.005, angularTolerance=0.05)
        bw, bh = bed_size('a1m')
        objs = [dict(path=pth, count=1, filaments=[2], assemble_index=[1], pos_x=[bw / 2], pos_y=[bh / 2], pos_z=[0.0])]
        plates = [{'plate_name': 'Dowel pusher, cups ' + ' / '.join(str(c) for c in p.pusher_cups), 'need_arrange': False,
                   'plate_params': {'print_sequence': 'by layer'}, 'objects': objs, 'assembled_params': []}]
        final = os.path.join(HERE, 'walltest', f'a1m-pusher{_SFX}.3mf')
        open(os.path.join(HERE, f'run-pusher{_SFX}.sh'), 'w').write('#!/bin/bash\n' + write_job(
            f'a1m-pusher{_SFX}', 'a1m', plates, final, process_extra={'enable_prime_tower': '0'}))
        print(f'bambu/run-pusher{_SFX}.sh ->', final)
    elif cmd in ('cover2', 'frame'):   # 10.3a face + 10.3b frame + 4 dowels (2 spares); 'frame': 10.3b + dowels only
        p = K.version_p(VERSION, closure='back', proto=PROTO)
        d = src_dir('back')
        meta = export_parts('back', p, d, only=('cover', 'frame'))
        _, peg, _ = K.peg_coupon(clearances=(p.peg_clr,), d=p.peg_d, peg_len=2 * p.peg_depth)
        ps = os.path.join(d, 'dowel.stl')
        cq.exporters.export(peg, ps, tolerance=0.005, angularTolerance=0.05)
        objs = []
        halves = (('cover', meta['cover']['regions']),) + ((('frame', meta['frame']['regions']),) if 'frame' in meta else ())
        if cmd == 'frame':
            halves = halves[1:]
        for i, (g, regions) in enumerate(halves):
            for r in regions:
                o = dict(path=os.path.join(d, f'{g}-{r}.stl'), count=1, filaments=[2], assemble_index=[i + 1])
                if r in ('led_blocker', 'support_enforcer'):
                    o['subtype'] = 'support_blocker' if r == 'led_blocker' else 'support_enforcer'
                objs.append(o)
        n0 = len(halves) + 1
        nd = 0 if p.cover_onepiece else (2 if p.centre_dowels else 0) + len(p.extra_dowels) + 2    # every dowel + 2 spares (#18: 6 + 2); #19: none
        if p.face_screws and not p.extra_dowels and not p.short_dowels and not p.centre_dowels:
            nd = 0                   # #22: screws, no dowels
        if p.sml_sprues:             # 18.2-B: one rack, a labelled 10-dowel row per size (S / M / L)
            pth = os.path.join(d, 'sprue-SML.stl')
            cq.exporters.export(K.sml_rack(p), pth, tolerance=0.005, angularTolerance=0.05)
            objs.append(dict(path=pth, count=1, filaments=[2], assemble_index=[n0]))
            nd, ns_override = 0, 0
        elif p.dowel_sprue:          # #18.1c: twice the count on one sprue + the pusher, nothing loose
            n_set = (2 if p.centre_dowels else 0) + len(p.extra_dowels) + len(p.short_dowels)
            n_all = 2 * n_set if p.sprue_spares is None else n_set + p.sprue_spares + (1 if p.test_joint else 0)
            if p.short_len == 2 * p.peg_depth:      # one dowel type: two equal rows
                spr, push = K.dowel_sprue(p, n_all - n_all // 2, n_all // 2, labels=('L', 'L'))
            else:
                spr, push = K.dowel_sprue(p, 2 * ((2 if p.centre_dowels else 0) + len(p.extra_dowels)), 2 * len(p.short_dowels))
            for nm_, w_ in (('sprue', spr),) + ((('pusher', push),) if p.sprue_pusher else ()):
                pth = os.path.join(d, f'{nm_}.stl')
                cq.exporters.export(w_, pth, tolerance=0.005, angularTolerance=0.05)
                objs.append(dict(path=pth, count=1, filaments=[2], assemble_index=[n0 + (0 if nm_ == 'sprue' else 1)]))
            nd, ns_override = 0, 0
        tj_idx = None
        if p.test_joint and cmd == 'cover2':   # 18.2-G: one labelled 'ТЕСТ' joint (frame block + face tab), exact numbers
            tb, tt = K.test_joint(p)
            base_ = max(i_ for o_ in objs for i_ in o_['assemble_index']) + 1
            for k_, (nm_, w_) in enumerate((('test-frame', tb), ('test-face', tt))):
                pth = os.path.join(d, f'{nm_}.stl')
                cq.exporters.export(w_, pth, tolerance=0.005, angularTolerance=0.05)
                objs.append(dict(path=pth, count=1, filaments=[2], assemble_index=[base_ + k_]))
            tj_idx = base_ + 1
        if nd:
            objs.append(dict(path=ps, count=nd, filaments=[2], assemble_index=list(range(n0, n0 + nd))))
        ns = len(p.short_dowels) + 2 if (p.short_dowels and not p.dowel_sprue) else 0   # #18.1b: the short dowels + 2 spares
        if ns:
            _, peg_s, _ = K.peg_coupon(clearances=(p.peg_clr,), d=p.peg_d, peg_len=p.short_len)
            ps_s = os.path.join(d, 'dowel-short.stl')
            cq.exporters.export(peg_s, ps_s, tolerance=0.005, angularTolerance=0.05)
            objs.append(dict(path=ps_s, count=ns, filaments=[2], assemble_index=list(range(n0 + nd, n0 + nd + ns))))
        # the face gets the #14C finish (owner): 0.08 layers + the CROWN_FINISH top set; frame and dowels stay 0.16
        face_pp = dict(CROWN_FINISH)
        if p.face_shell:             # #16A + G1 (owner, 2026-09-29); #18: T08
            face_pp.update(FACE_SHELLS[p.face_shell])
        if p.cover_onepiece or p.face_bosses:   # #19 / #20: the 17.2 grid support (2 interface layers, 0.2 gap)
            face_pp.update(SHELF_SUPPORT)
        if p.face_bosses:            # #20: the back floats on the bosses; the plate-wide bridge setting left it unsupported
            face_pp.update({'bridge_no_support': '0', 'max_bridge_length': '5'})
        face_params = [dict(assemble_index=1, print_params=face_pp)] if p.crease_r > 0 and cmd == 'cover2' else []
        if face_params and tj_idx:   # the test face tab prints as the face does (0.08 layers)
            face_params.append(dict(assemble_index=tj_idx, print_params=face_pp))
        num = f'{PROTO}.3' if PROTO is not None else '10.3'
        name_ = (f'{num}b frame + {nd} dowels' if cmd == 'frame' else f'{num} one-piece cover + fins' if p.cover_onepiece
                 else f'{num}a face + {num}b frame + dowel sprue' + (' + pusher' if p.sprue_pusher else '') if p.dowel_sprue
                 else f'{num}a face + {num}b frame + {nd} dowels')
        plates = [{'plate_name': name_, 'need_arrange': True,
                   'plate_params': {'print_sequence': 'by layer'}, 'objects': objs, 'assembled_params': face_params}]
        final = os.path.join(ROOT, 'walltest', f'a1m-{cmd}.3mf')
        # one colour: no prime tower (desktop Studio refuses a tower with per-object layer heights)
        open(os.path.join(HERE, f'run-{cmd}{_SFX}.sh'), 'w').write('#!/bin/bash\n' + write_job(
            f'a1m-{cmd}{_SFX}', 'a1m', plates, final, process_extra=dict({'enable_prime_tower': '0'}, **(
                {'bridge_no_support': '0', 'max_bridge_length': '5'} if p.face_bosses else {}))))
        print(f'bambu/run-{cmd}{_SFX}.sh ->', final, meta)
    elif cmd == 'layerprofile':  # layerprofile <3mf> <z_switch> <height> <objects...>: 0.16 up to z_switch, 0.08 above
        # objects: 1-based order in the project; the last z must be the object's height or the slicer drops the profile
        path, zs, h = sys.argv[2], float(sys.argv[3]), float(sys.argv[4])
        prof = [0.0, 0.16, zs, 0.16, zs + 0.08, 0.08, h, 0.08]
        print(set_layer_profiles(path, {int(n): prof for n in sys.argv[5:]}))
    elif cmd == 'shelftest':     # a shelf-only test print (A1 mini), white, one plate
        plates = [plate(f'{VERSION} shelf', [('back', 'v1', 'shelf', 1)])]
        final = os.path.join(HERE, 'walltest', f'a1m-shelftest{_SFX}.3mf')
        open(os.path.join(HERE, 'run-shelftest.sh'), 'w').write('#!/bin/bash\n' + write_job(f'a1m-shelftest{_SFX}', 'a1m', plates, final))
        print('bambu/run-shelftest.sh ->', final)
    elif cmd == 'fit':           # bay-hold fit print (A1 mini): case A tray + shelf (+ the 10.4 seat) only, white, one plate
        plates = case_plates('a1m', 'v1', 'back', 'A (screws)')[:1]
        if 'seat' in json.load(open(os.path.join(src_dir('back'), 'parts.json'))):
            plates = [plate(f'{VERSION} tray + shelf + seat', [('back', 'v1', 'tray', 1), ('back', 'v1', 'shelf', 1), ('back', 'v1', 'seat', 1)])]
        final = os.path.join(HERE, 'walltest', 'a1m-fit.3mf')
        open(os.path.join(HERE, 'run-fit.sh'), 'w').write('#!/bin/bash\n' + write_job('a1m-fit', 'a1m', plates, final, process_extra={'enable_prime_tower': '0'}))
        print('bambu/run-fit.sh ->', final)
    elif cmd == 'traya':         # Checkpoint A: the 'fit' plate (tray 17.1 as printed in P17a) WITHOUT its superseded shelf,
        # the same objects, per-object settings and process; the tray only
        pl_ = case_plates('a1m', 'v1', 'back', 'A (screws)')[0]
        keep = [o for o in pl_['objects'] if o['assemble_index'] == [1]]
        plates = [dict(pl_, plate_name='Checkpoint A: tray 17.1', objects=keep,
                       assembled_params=[a for a in pl_.get('assembled_params', []) if a['assemble_index'] == 1])]
        final = os.path.join(HERE, 'walltest', 'a1m-traya.3mf')
        open(os.path.join(HERE, 'run-traya.sh'), 'w').write('#!/bin/bash\n' + write_job('a1m-traya', 'a1m', plates, final, process_extra={'enable_prime_tower': '0'}))
        print('bambu/run-traya.sh ->', final)
    elif cmd == 'walltest':      # the wall test plate (A1 mini): parts, job and run-walltest.sh
        plates = [walltest('a1m')]
        final = os.path.join(HERE, 'walltest', 'a1m-walltest.3mf')
        open(os.path.join(HERE, 'run-walltest.sh'), 'w').write('#!/bin/bash\n' + write_job('a1m-walltest', 'a1m', plates, final))
        print('bambu/run-walltest.sh ->', final)
    elif cmd == 'patch':         # write PROJECT_ONLY keys into the given saved / sliced projects
        for path in sys.argv[2:]:
            print(path, patch_settings(path))
    elif cmd == 'single':        # single <slot> <3mf ...>: every object and part on that slot (sliced: asserts 1 filament)
        slot = int(sys.argv[2])
        for path in sys.argv[3:]:
            print(path, 'slot', force_single_filament(path, slot))
    elif cmd == 'parts':
        for cl in sys.argv[2:] or ('back', 'snap'):
            print(cl, export_parts(cl))
    elif cmd == 'prepare':
        keys = sys.argv[2:] or list(PRINTERS)
        lines = ['#!/bin/bash\n# generated by build_bambu.py prepare: slices every job, one at a time\n']
        for key in keys:
            for project, plates in projects(key).items():
                lines.append(write_job(f'{key}-{project}', key, plates, final_path(key, project)))
        run = f'run{_SFX}.sh'
        open(os.path.join(HERE, run), 'w').write(''.join(lines))
        print(f'{len(lines) - 1} jobs -> bambu/{run}')
    elif cmd == 'previews':      # sliced previews of every plate of the given printers (default: the X1C base set)
        sys.path.insert(0, HERE)
        import gcode_preview as G
        pv = os.path.join(ROOT, 'preview'); os.makedirs(pv, exist_ok=True)
        for key in sys.argv[2:] or ['x1c']:
            for project in PROJECTS:
                od = os.path.join(OUT, f'{key}-{project}')
                for g in sorted(glob.glob(os.path.join(od, 'plate_*.gcode'))):
                    n = os.path.basename(g)[6:-6]
                    tag = '' if key == 'x1c' else f'{key}-'
                    print(G.render(g, os.path.join(pv, f'{tag}{project}-plate{n}.png')))
    elif cmd == 'fix':           # after run.sh: complete the saved projects from their sliced twins
        for key in sys.argv[2:] or list(PRINTERS):
            for project in PROJECTS:
                sl = os.path.join(OUT, f'{key}-{project}', f'{key}-{project}-sliced.3mf')
                if os.path.exists(sl):
                    print(key, project, fix_project(final_path(key, project), sl))
    elif cmd == 'stats':
        sys.path.insert(0, HERE)
        import gcode_preview as G
        rows = []
        for key in PRINTERS:
            for project in PROJECTS:
                od = os.path.join(OUT, f'{key}-{project}')
                if not os.path.exists(os.path.join(od, 'result.json')):
                    continue
                res = json.load(open(os.path.join(od, 'result.json')))
                jp = json.load(open(os.path.join(JOBS, f'{key}-{project}.json')))
                used = slicer_used_g(os.path.join(od, f'{key}-{project}-sliced.3mf'))
                for pl in res.get('sliced_plates', []):
                    g = G.stats(os.path.join(od, f'plate_{pl["id"]}.gcode'))
                    # P2S / H2S flush in firmware (M620.10 ... L<length>): the G-code has no flush extrusion, so the
                    # purge is the slicer's filament total for the plate minus what the G-code extrudes
                    gp = os.path.join(od, f'plate_{pl["id"]}.gcode')
                    if pl['filament_change_times'] and 'M620.10 A1' in open(gp, errors='ignore').read():
                        if pl['id'] not in used:
                            sys.exit(f'{key}-{project} plate {pl["id"]}: the flush runs in firmware and needs the sliced '
                                     'project for its purge; rerun the run script')
                        for fid, ug in used[pl['id']].items():
                            f = g['grams'].setdefault(f'filament_{fid}', {})
                            rest = sum(v for k, v in f.items() if k != 'purge')
                            f['purge'] = round(max(f.get('purge', 0.0), ug - rest), 2)
                    rows.append(dict(printer=key, label=PRINTERS[key][1], project=project, plate=pl['id'],
                                     name=jp['plates'][pl['id'] - 1]['plate_name'],
                                     minutes=round(pl['total_predication'] / 60), changes=pl['filament_change_times'],
                                     warning=pl.get('warning_message', ''), return_code=res['return_code'], **g))
        os.makedirs(ROOT, exist_ok=True)
        json.dump(rows, open(os.path.join(ROOT, 'stats.json'), 'w'), indent=1)
        write_stats_md(rows)
        print(len(rows), 'plates')
    elif cmd == 'verify':
        bad = 0
        for key in sys.argv[2:] or list(PRINTERS):
            for project in PROJECTS:
                errs = verify_project(final_path(key, project), os.path.join(JOBS, f'{key}-{project}.json'))
                bad += len(errs)
                print(key, project, 'ok' if not errs else errs)
        sys.exit(1 if bad else 0)
    else:
        raise SystemExit(__doc__)


if __name__ == '__main__':
    main()

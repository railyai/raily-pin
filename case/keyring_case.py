#!/usr/bin/env python3
"""Raily Keyring case v1: one parametric CadQuery model.

This file is the single source of truth for the keyring case: print files
(STL/STEP), the assembly guide's line art and the Blender renders all come
from it. Every printed body is one filament region (tray, loop, cover zone 3,
front zones 1 and 2, LED spot, shelf) so a multi-colour slicer can assign them;
SCHEMES maps bodies to filaments.

Coordinates are the ones of Seeed's Expansion Board STEP (../guide/cad):
  x  long axis of the case. x min = bottom end (XIAO USB-C), x max = top end (loop)
  y  thickness. +y = front (component side, OLED, screws go in from here)
  z  width. +z = the long side with Button D1 and the Grove A0/D0 port

Every number in MEASURED comes from expansion.step / xiao.step (measure()),
every number in DATASHEET from the cited source. Clearances, walls and shape
choices are design parameters in P.

HARD RULE (owner, 2026-09-29): owner-approved geometry is frozen. Never alter an approved part (its outline, fence,
steps, posts, clearances, notches, keys) or add a feature to it without asking the lead first and getting the owner's
yes. A new version reuses the approved part's own source and parameters; any difference must be listed and approved
before it is sliced. (17.2 drifted from the approved 13.2B + 11.2 through v1.2 audit features nobody re-approved.)

Run:  python keyring_case.py measure      # the measured stack
      python keyring_case.py options      # packing options A/B/C
      python keyring_case.py variants     # corner variants a/b/c: sizes after board-corner growth
      python keyring_case.py tab          # flex-tab mechanics
      python keyring_case.py check [v]    # interference / clearance report
      python keyring_case.py stage2       # all bodies -> print/ (STL per filament + STEP), preview line art,
                                          # build/stage2 meshes for Blender, interference report
      python keyring_case.py stage2 v2    # case v2 (OLED window, thinner shell; VERSIONS) -> print/v2/, ...-v2
"""
import json
import math
import os
import sys
from dataclasses import dataclass, replace, asdict

import cadquery as cq
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
CAD = os.path.normpath(os.path.join(HERE, '..', 'guide', 'cad'))
PREVIEW = os.path.join(HERE, 'preview')
BUILD = os.path.join(HERE, 'build')

# ---------------------------------------------------------------- measured
# From the Seeed STEP files (CC BY-SA 4.0). measure() re-derives them.
MEASURED = dict(
    board_x=(119.50, 177.50), board_z=(-20.39, 22.11), board_r=3.00,   # PCB outline, corner radius
    pcb_y=(-3.20, -1.60),                                              # 1.6 mm PCB, top face at -1.60
    holes=[(123.49, 18.36), (173.49, 18.36), (123.49, -16.64), (173.49, -16.64)], hole_r=1.50,
    under_min_y=-6.38,        # lowest part under the board: CR1220 holder, x 142.93..155.07
    # every other part under the board (Grove connectors, microSD slot, through-hole tails) ends above this
    under_rest_y=-5.14,
    cr1220_holder=dict(x=(142.93, 155.07), z=(-8.76, 10.24)),
    xiao_top_y=9.85,          # XIAO USB-C top; XIAO on 2.54 mm header spacers on the 2.90 sockets
    usb_c=dict(x_mouth=117.96, x_back=125.26, y=(5.65, 9.85), z=(-3.58, 5.36)),  # overhangs the edge by 1.54
    # the receptacle shell at its mouth (a section of the shell solid at x 118.0..119.4): 8.94 x 3.21, its tongue
    # at y 7.90..8.60 (centre 8.25). The y 5.65 above is the mounting legs behind the XIAO PCB edge (x > 119.48).
    usb_c_shell=dict(y=(6.64, 9.85), z=(-3.58, 5.36)),
    # OLED module block: glass top y -0.60 over z -7.96..5.76; the strip z 5.76..8.54 is the lower FPC ledge (top y -1.02).
    # The STEP has no active-area face: the window is sized from the glass (DATASHEET oled_aa for the reading area).
    oled=dict(x=(141.92, 166.60), z=(-7.96, 8.54), y_top=-0.60, glass_z=(-7.96, 5.76)),
    jst=dict(x_mouth=176.89, y=(-1.59, 3.79), z=(6.89, 15.04)),        # battery socket, opening +x
    grove_a0=dict(x=(143.53, 153.53), z_mouth=21.36, y=(-1.60, 3.50)),  # A0/D0 shroud, opening +z
    switch=dict(x=(130.30, 139.12), z_min=-21.47),                      # power switch lever past the -z edge
    # Button D1: body x 166.40..171.10, y -1.99..0.31, front face z 22.01; actuator
    # x 167.78..169.73, y -1.06..-0.21, tip z 23.01 -> 1.00 mm of actuator out of the body
    button_d1=dict(x=(167.78, 169.73), y=(-1.06, -0.21), z_max=23.01, body_z=22.01, body_x=(166.40, 171.10)),
    header_pins_top_y=7.88,   # 2x4 male pins at x 170.8..174.0, z -4.0..4.2
    # power switch lever past the board edge (one position in the STEP): x 134.71..136.16, y -1.11..0.02;
    # travel assumed = lever width (1.45) toward -x, the usual for this size of slide switch
    switch_lever=dict(x=(134.71, 136.16), y=(-1.11, 0.02), z_min=-21.47, travel=1.45),
    # XIAO LEDs, posed on the board (xiao.step product names): 'USR' = the RGB user LED, 'LED' = charge LED
    xiao_rgb_led=dict(x=(121.77, 122.77), y=(6.64, 6.94), z=(-5.32, -4.32)),
    xiao_charge_led=dict(x=(119.80, 120.43), y=(6.64, 6.94), z=(-5.45, -4.18)),
)

# The real stack against the STEP pose, from the owner's fit test (2026-09-28). The Expansion Board sits on its
# standoffs at the model height (checked in the printed tray), but its XIAO sockets are about 5.5 mm tall, not the
# STEP's 4.5, and the header spacer stands about 0.5 off them: with the headers the reversed way (short ends down,
# spacer on the sockets, the XIAO soldered on the long ends, tails clipped to <= 2 mm) the XIAO sits about +1.9
# over the STEP pose (side photos, the spacer as the scale). Long ends down sits +5.7..6.0: the guide shows it as
# wrong. The design takes +2.0 (the cover clears it by clr); the window also takes +1.5 (XIAO_DY_TOL).
# Every XIAO-referenced number (USB-C receptacle, XIAO top, its LEDs) and the XIAO in board_parts() move by XIAO_DY.
XIAO_DY = float(os.environ.get('KC_XIAO_DY', '2.0'))
XIAO_DY_TOL = 0.5            # the window also takes a XIAO this much lower (P.usb_window_low)
XIAO_DY_UP = 0.0             # the cover clears a XIAO this much higher (plus clr)
for _k in ('usb_c', 'usb_c_shell', 'xiao_rgb_led', 'xiao_charge_led'):
    MEASURED[_k]['y'] = tuple(v + XIAO_DY for v in MEASURED[_k]['y'])
MEASURED['xiao_top_y'] += XIAO_DY
# The power switch's lever sits higher on its body than in the STEP: with the bare board flat on its standoffs in the
# printed tray the lever is entirely above the v1 slot (top at y 0.42), so at least +1.5; an earlier photo showed its
# bottom sliver (+1.3). The slot takes +1.0 to +2.4 (switch_slot_clr_y around +1.7).
SWITCH_DY = float(os.environ.get('KC_SWITCH_DY', '1.7'))
MEASURED['switch_lever']['y'] = tuple(v + SWITCH_DY for v in MEASURED['switch_lever']['y'])

# ---------------------------------------------------------------- datasheet
DATASHEET = dict(
    # 602030 LiPo with PCM. Largest listed envelope: EEMB LP602030, 6.7 x 20.5 x 32 mm
    # (docs/pins/eu-compliance.md, supplier table; Ufine UFX602030 is 6 x 20 x 32).
    lipo=(20.5, 32.0, 6.7),            # width, length, thickness
    # Seeed Grove Vibration Motor 105020003: 24 x 20 x 9.8 mm (Distrelec product data,
    # distrelec.ch 30069914). Split used for the model: 1.6 PCB, 5.1 Grove socket
    # (same socket as on the Expansion Board STEP), 3.1 through-hole tails.
    motor=(20.0, 24.0, 9.8),
    motor_split=(3.1, 1.6, 5.1),       # tails, pcb, socket
    grove_plug_len=7.0,                # HY2.0-4P plug body (guide/cad/parts.py, Raily Blueprints 03-06)
    grove_socket_depth=6.46,           # A0/D0 mouth 21.36 to pin wall 14.90 (STEP)
    usb_c_overmold=(12.35, 6.5),       # USB Type-C spec: max plug overmold (the v1 opening was sized for it)
    usb_c_plug_shell=(8.4, 2.6, 6.65), # USB Type-C plug metal shell, max width x height, and its length past the overmold
    # eSUN PLA-Matte Rainbow TDS (esun3d.com): flexural modulus XY 2249 MPa, Z 1783 MPa;
    # elongation at break XY 7.23 %, Z 2.26 %. Used for the flex tab (PLA family).
    pla_E_xy=1997.0, pla_elong_xy=7.23,
    # Polymaker Panchroma TDS V2.1 (polymaker.com), Matte column: density 1.37 g/cm3, Young's modulus
    # X-Y 1997 MPa (used for the flex tab since the chosen spools are Panchroma Matte). The elongation
    # at break is not in that TDS; 7.23 % is eSUN PLA-Matte's (a PLA-matte proxy).
    pm_matte_density=1.37,
    # 0.96 inch 128 x 64 SSD1306 OLED glass (Univision UG-2864HSWEG01 class): active area 21.744 x 10.864 mm,
    # viewing area 23.744 x 12.864 mm. Assumed centred on the glass block of the STEP; check on the board.
    oled_aa=(21.744, 10.864), oled_va=(23.744, 12.864),
)

# ---------------------------------------------------------------- colour
# Raily brand tokens (Brandbook 2026-09-06, 04-Color-Tokens/raily-color-tokens.json)
PALETTE = dict(deepBlue='#425FFF', railyBlue='#5B77FF', periwinkle='#8599FF', lilac='#9AABFF')
# Owner's candidate filaments (amazon.es, 1.75 mm). Hex = eSUN's published colour list
# (esun3d.com PLA+ / PLA+HS pages) unless noted. token = brand role it stands in for.
FILAMENTS = dict(
    esun_blue=dict(name='eSUN PLA+ Blue', asin='B07FQJ8LQD', hex='#01378D', token='deepBlue',
                   src='esun3d.com: BLUE #01378D, Pantone 2146 C'),
    esun_blue_purple=dict(name='eSUN PLA+ Blue Purple (Very Peri)', asin='B0BDKTH44X', hex='#6667AB', token='periwinkle',
                          src='eSUN lists it as Very Peri; render uses Pantone Very Peri 17-3938 #6667AB. '
                              'eSUN\'s page gives #22047E, far darker than the name: check the spool'),
    esun_haze_blue=dict(name='eSUN PLA+ Haze Blue', asin='B0DNMLR9PC', hex='#7AB9EF', token='lilac',
                        src='esun3d.com: HAZE BLUE #7AB9EF'),
    esun_ocean=dict(name='eSUN PLA-Matte Rainbow Ocean', asin='B0DGPSJ464',
                    stops=['#1F4F9A', '#2BA3B8', '#A8DDE0'],
                    src='no published hex; stops approximated from the product name until a sample is photographed'),
    raily_4th=dict(name='4th spool, railyBlue role (to source)', asin=None, hex='#5B77FF', token='railyBlue',
                   src='placeholder = brand token railyBlue; eSUN\'s PLA+ colour table lists a "Periwinkle blue" '
                       '(Pantone 2116 C) worth sampling'),
    # Polymaker Panchroma Matte PLA; hex and TD (transmission distance, mm) from Polymaker's
    # "HEX Codes and Transmission Distances" table (wiki.polymaker.com); Raspberry Blue from the colourway agent
    pm_cotton_white=dict(name='Polymaker Panchroma Matte Cotton White', sku='CA04016', hex='#F4EFEB', td=4.1),
    pm_charcoal_black=dict(name='Polymaker Panchroma Matte Charcoal Black', sku='CA04015', hex='#2F2E30', td=0.1),
    pm_ash_grey=dict(name='Polymaker Panchroma Matte Ash Grey', sku='CA04014', hex='#485155', td=0.1),
    pm_army_blue=dict(name='Polymaker Panchroma Matte Army Blue', sku='CA04008', hex='#2E4462', td=0.1, token='deepBlue'),
    pm_raspberry_blue=dict(name='Polymaker Panchroma Matte Raspberry Blue', sku='CA04092', hex='#5472D0', td=None,
                           token='railyBlue'),
    pm_pastel_periwinkle=dict(name='Polymaker Panchroma Matte Pastel Periwinkle', sku='CA04036', hex='#ADB4E6', td=1.5,
                              token='lilac'),
    overture_marble=dict(name='OVERTURE Marble PLA', asin='B0F8NM6788', hex='#B9B7B0', src='stone effect; not rendered'),
)


@dataclass
class P:
    layout: str = 'B'            # 'A' sandwich, 'B' end bay (owner-approved), 'C' side by side
    corner: str = 'g2'           # front silhouette: 'circle' (a), 'g2' (b), 'stadium' (c)
    corner_r: float = 9.6        # (a) circular corner radius
    g2_req: float = 15.0         # (b) circle-equivalent radius (same corner cut on the diagonal)
    g2_n: float = 3.0            # (b) superellipse exponent: n > 2 gives zero curvature at the joins (G2)
    stadium_flat: float = 2.0    # (c) straight piece left on each end
    clr: float = 0.3             # part to wall / part to part
    print_clr: float = 0.2       # printed fits (stage 2)
    wall: float = 1.6            # side walls (v1.1: the owner's pick after printing 1.0 / 1.25 / 1.6 test trays; v1 was 2.0)
    floor: float = 1.4           # rear (v1.1 = v1: the owner found 1.4-1.5 fine after the 1.0 / 1.2 test trays)
    end_wall: float = 1.25       # the USB-C end wall where it is flat (up to the top band), so the receptacle mouth can sit
                                 # usb_recess under the outer face with clr to the board edge; 0 = wall
    floor_boss: float = 1.4      # floor thickness in a ring around each countersunk screw hole (closure A)
    # thin walls under the fillets: the front fillet (3.0) reaches below the parting line and the back fillet (2.5)
    # past the floor, so a 1.25 wall would be 0.49 mm at the parting line and 0.55 across the floor corner. A band
    # inside the wall top (the cover's lip moves in by as much) and a 45 degree fillet along the floor corner keep both
    # near 1 mm. 0 = none (v1 and v2 walls do not need them)
    rim_band: float = 0.2        # extra wall thickness over the top rim_band_h of the tray wall, 45 degrees below
    rim_band_h: float = 1.5
    floor_chamfer: float = 0.8   # 45 degree fill in the floor-to-wall corner inside
    floor_boss_d: float = 8.0    # ring diameter
    g2_req_bottom: float = 4.25  # USB-C end: circle-equivalent corner radius (the loop end keeps g2_req); 0 = g2_req
    cover: float = 1.0           # front plate at the rim, over the cavity (kept as low as the lip allows)
    facet_angle: float = 7.0     # tilt of the four X-arm facets, degrees (the corner facets are steeper)
    crown: float = 0.0           # 0 = facets run all the way to the panel edge (crown follows from the angle);
                                 # > 0 = cap: the facets stop at this height and the outer field is flat
    facet_min: float = 0.15      # lowest facet height over the rim (panel corners)
    rim_w: float = 4.0           # rim band around the faceted panel
    fillet_front: float = 3.0    # pillowed rim
    fillet_back: float = 2.5     # back (build plate) edge round, used when back_chamfer is 0
    back_chamfer: float = 1.2    # v1.1: a 45 degree chamfer on the back edge instead of the round (a round on the plate
                                 # side prints as stair steps: its first layers overhang almost flat; owner photo 2026-09-28)
    jst_zone: float = 5.0        # mated JST-PH plug + lead bend past the socket mouth (verify)
    grove_zone: float = 3.5      # Grove cable bend past the plug back (verify)
    lipo_swell: float = 0.3      # headroom over the cell for swelling
    foam: float = 0.5            # foam pad (layout A, cell under the parts)
    shelf: float = 0.8           # printed shelf between cell and motor (layout B)
    lattice_fill_w: float = 0.97 # the 4 outer diamonds span this share of the faceted panel's width
    lattice_fill_h: float = 0.95 # ... and of its height (owner sketch: they reach the panel edge)
    top_hole: bool = False       # v2 centre button hole, off in v1
    top_hole_d: float = 12.0
    loop_w: float = 11.0         # keyring lug, top end
    loop_t: float = 4.0          # lug thickness (y)
    loop_hole: float = 4.0       # split ring 25 mm, wire about 1.2-1.5
    loop_gap: float = 0.8        # hole edge to the case end
    loop_web: float = 2.5        # material from hole to lug edge (>= 2 mm)
    loop_boss: float = 1.5       # the end wall behind the lug, thickened inward over the lug (above the shelf)
    low_floor: bool = True       # v1.1: the floor as high under the board as the depth allows (packing()); v1 False
    holder_pocket: float = 0.75  # floor left under the CR1220 holder's pocket (low_floor). The holder reaches 2.3 under the
                                 # board in the owner's photo (0.85 clear of the pocket), 3.18 in the STEP (0.02 clear)
    # side button: print-in-place flexure in the +z wall over Button D1
    tab_len: float = 16.0        # hinge to free end, along x (v1 14.0: the 1.25 tongue needs a longer arm for the same force)
    tab_h: float = 5.0           # tongue height (y), centred tab_dy off the D1 actuator (v1 6.0: the slit below it now
                                 # stays 1.5 mm above the back fillet, the roofed slit above it under the B latch slits)
    tab_dy: float = 0.0          # tongue centre off the actuator centre
    tab_t: float = 1.25          # tongue thickness: 3 lines of a 0.4 nozzle (v1 1.0 = 2.4 lines printed ragged)
    tab_slit: float = 0.7        # slit width (0.4 nozzle: >= 0.6 so it does not fuse)
    tab_roof: float = 45.0       # the slit above the tongue (print Z) is a 45 degree roof, not a 14 mm bridge; 0 = flat
    tab_end_chamfer: float = 2.0 # 45 degree chamfer on the tongue's free-end top corner (x-y), the slit follows; 0 = square
    tab_free: float = 2.5        # free end past the D1 centre
    nub_gap: float = 0.15        # nub to actuator tip at rest
    nub_w: float = 1.6           # nub across x (actuator is 1.95)
    nub_h: float = 0.0           # nub across y (v1: 1.4) (actuator is 0.85; taller to forgive board height tolerance); 0 = a rib
    nub_rib_margin: float = 0.3  # nub_h 0 (v1.1): the nub is a vertical rib over the whole tongue height, this far in
                                 # from its edges; it only ever meets the D1 actuator (owner check), +-1.8 mm of height
    pad: tuple = (5.5, 4.4, 0.0) # outside pad on the tongue: x, y, proud (0 = flush, body colour, no pad)
    # USB-C: the end wall is flat at the receptacle and its outer face sits usb_recess in front of the receptacle mouth;
    # the opening fits the receptacle shell (8.94 x 3.21) with clearance, so only the plug's metal shell goes in.
    # v1 had a 13.0 x 7.2 overmold-sized opening with the mouth 8.3 mm inside the outer face.
    usb_recess: float = 0.2      # receptacle mouth behind the outer face at the USB axis (target; see packing())
    usb_window: tuple = (9.8, 3.8)    # width (z) x height (y): shell + 0.43 / 0.30 per side
    usb_window_low: float = 0.5       # the window's lower edge this much lower (XIAO_DY_TOL: the lift is read from photos)
    # colour: which filament each part is printed in
    scheme: str = 'v1'           # colour scheme (SCHEMES): which filament each body gets
    front_zones: bool = False    # split the front into 3 colour zones (option; the owner chose solid fronts)
    zone_depth: float = 1.0      # front colour zones 1 and 2: skin depth under the facet surface
    # RGB LED spot in the cover, over the XIAO user LED: 'skin' (thin cover skin) or 'hole'
    led_mode: str = 'skin'
    led_d: float = 3.0           # skin spot diameter
    led_skin: float = 0.3        # skin left over the LED (owner, 2026-09-28: 0.3 shows the LED clearly better than 0.4)
    led_hole_d: float = 2.0      # through-hole diameter (led_mode 'hole')
    # closure (owner, 2026-09-28: keep the front clean). 'back' = A: M2 countersunk screws from the back into
    # nuts in side slots of the cover pillars; 'snap' = B: screwless, four cantilever latches in the long walls;
    # 'front' = the first version (screws from the front into nuts in the back), kept as an option.
    closure: str = 'back'
    # A: ISO 7046-1 M2 x 16 countersunk, cross recess H0 (dk 3.8, k 1.2; the length includes the head)
    back_screw_len: float = 16.0
    csk_d: float = 4.1           # 90 degree countersink at the back face (dk 3.8 + 0.3)
    nut_slot_clr: float = 0.1    # slot width = nut AF + this: a light push fit, so the nut stays while the cover is turned
    nut_boss_d: float = 7.4      # pillar diameter around the nut slot (from 1 mm under the slot up to the front plate)
    nut_tip_past: float = 0.5    # screw tip past the nut
    # B: cantilever latches (tongues cut in the long walls, like the side button), bumps on the cover
    latch_x: tuple = (140.0, 178.0)   # bump centres along x, on both long walls (straight part of the outline)
    latch_len: float = 14.0      # hinge to bump centre
    latch_y0: float = 5.8        # tongue bottom; the tongue runs up to the wall top (v1 5.0; v1.1 clears the button's roofed slit)
    latch_t: float = 2.0         # latch tongue thickness: where the wall is thinner, a pad inside the wall makes it up locally
    latch_e: float = 0.6         # engagement = tongue deflection while the bump passes
    latch_bump_w: float = 3.0    # bump length along x
    latch_catch_h: float = 3.6   # catch block under the front plate (carries the bump)
    latch_catch_t: float = 1.6   # catch block thickness (z)
    latch_ret_deg: float = 45.0  # retention face angle from the pull direction (45: both parts print without support)
    latch_lead_deg: float = 30.0 # lead-in face angle
    latch_mu: float = 0.35       # PLA on PLA friction (assumed, not in the TDS; see snap_mechanics for the range)
    pry_notch: tuple = (6.0, 1.0, 9.5)  # width (z), depth (y), start z: pry notch in the bottom end, beside USB-C
    pin_d: float = 2.6           # B: locating pins on the standoffs, into the board holes (3.0)
    # fastening 'front': M2 screws from the front, through the board holes, into nuts in the back
    screw_len: float = 16.0      # ISO 1207 M2 x 16 (cheese head dk 3.8, k 1.3)
    screw_tip: float = 0.1       # tip stops this far inside the back face
    screw_clear: float = 2.4
    cbore_d: float = 4.2         # head counterbore
    nut_af: float = 4.0          # ISO 4032 M2: s 4.0, m 1.6
    nut_m: float = 1.6
    standoff_d: float = 5.0      # tray standoffs under the PCB
    pillar_d_top: float = 5.8    # cover pillars above y pillar_step_y (clears JST by 0.4)
    pillar_d_low: float = 4.2    # below the step (clears Reset and D1 by 0.4)
    pillar_step_y: float = 0.7   # above the button tops (0.31) + 0.4
    pillar_gap: float = 0.1      # pillar to PCB top
    lip_w: float = 1.0           # cover lip into the tray
    lip_h: float = 1.5
    # bay
    rib_t: float = 1.2           # cell stop rib at the start of the bay
    lead_notch: tuple = (8.0, 4.0)   # notch for the cell leads: width (z), depth (y)
    ledge_w: float = 1.5
    ledge_h: float = 1.5
    motor_post: float = 3.0      # corner posts under the motor PCB (tails hang between them); bay_hold False only
    # ---- bay retention (v1.1 follow-up, the owner's fit tests 2026-09-28). The real Grove Vibration Motor module is a
    # 20 x 20 mm PCB (the 24 mm datasheet length is its envelope); its Grove socket's through-hole pins stick 2 mm under it.
    bay_hold: bool = True        # pads, slide-in shelf with the corner hook leaf, motor rails (False = the v1 posts)
    motor_l: float = 20.0        # motor PCB length along z (Grove socket on the +z edge)
    cell_len: tuple = (30.1, 31.6)   # real cell length with its protection board (EEMB LP502030, THOR 602030 class)
    cell_t: tuple = (5.0, 6.7)   # cell thickness range
    pad_h: float = 1.8           # cell end pads on the bay floor, printed with the tray (both rigid)
    pad_wall_clr: float = 0.3    # pad outline off the bay wall (they sit under the ledges)
    pad_x0: float = 183.3        # pads start here: the rib-side corner stays free for the cell leads
    shelf_slide: float = 1.5     # the shelf goes in this far toward the rib, then slides home under the far lip
    shelf_lip_w: float = 1.4     # far lip over the shelf edge (45 degree underside; the shelf edge has a 45 degree chamfer)
    shelf_lip_z: float = 11.0    # far lip spans z within +- this of the bay centre line (the straighter part of the end)
    detent_h: float = 0.4        # detent bump on the rib top: the shelf edge drops past it (click) at home
    # v1.2 (no support anywhere): the shelf prints standing on its rib-side edge, so every spring is a (y, z) profile
    # bending in the layer plane. One leaf in the shelf plane runs along z (root by the -z motor rail, free end over the
    # cell's -z end); a hook under its free end presses the cell's -z top corner on a 45 degree face: down onto the
    # floor and +z against the rigid +z pad. Length and thickness both lift the leaf.
    leaf_x: tuple = (None, 194.1)   # leaf across x: from the shelf's rib-side edge to here
    leaf_z: tuple = (-15.8, -1.6)   # leaf free end (the hook's -z face) and root
    leaf_slit: float = 0.6
    hook_x0: float = 183.8       # hook starts here (clear of the rib when the shelf drops in 1.5 short), 45 degree lead
    hook_pre: float = 0.3        # preload on the shortest, thinnest cell, normal to the hook face
    rail_lip: float = 0.6        # motor rails: top lip over the PCB edge (45 degree underside), clearance rail_clr
    rail_clr: float = 0.2
    mark_h: float = 0.5          # the orientation mark: raised on the shelf top
    under_fence: tuple = None    # #13: (mode 'full' | 'ends' | 'ends+mid', height, tab width) under the shelf edge
    fence_t: float = 1.25        # #13: fence / tab thickness across x (3 lines)
    fence_lead: float = 0.3      # #13: 45 degree lead-in on the fence's free edge, partition side
    seat_posts: bool = True      # v1.1 seat posts on the shelf top (#13 prints top down: no posts)
    top_label_deboss: float = 0.0   # > 0: the top label is cut this deep instead of raised (#13: the top is the bed face)
    under_depth: float = 0.41    # the part number's depth in the shelf underside
    top_chamfer: float = 0.0     # 45 degree chamfer round the shelf top's edge (#13: the bed face, elephant foot)
    # ---- #10 (v1.2 at the owner's width option 3; lead + independent audit, 2026-09-28). Defaults keep v1.
    len_extra: float = 0.0       # extra length at the loop end (the owner's 89.5)
    shelf_clr: float = None      # shelf outline to the tray (None = print_clr; the owner asked >= 0.3)
    motor_drop: float = 0.0      # bay_hold: the motor PCB this much lower (underside at the socket pins + 0.3)
    motor_dx: float = 0.0        # bay_hold: the motor this much toward the loop (room for the -x click bump)
    floor_top: float = None      # low_floor: the floor top under the board fixed here (None = as low as the stack needs)
    motor_pins: float = 2.0      # Grove socket pins under the motor PCB (#11 fit: 1.6)
    bay_len: float = None        # rigid bay between the cell pads along z (None = the longest cell; the leaf presses)
    bay_leaf: bool = True        # the v1.2 spring leaf with its hook (audit: it cannot work; #10 drops it)
    bay_detent: bool = True      # detent bump on the rib top for the slide-home shelf
    motor_retain: bool = False   # -x click bumps, +x corner stops clear of the ears, no lip over the rails
    socket_gap: float = 11.0     # +z rail gap in front of the Grove socket (the plug overlaps the fence)
    lip_center: bool = False     # far lip centred on the case axis (v1: on z = 0)
    shelf_key: tuple = None      # (x length, z depth): key on the +z ledge wall, the shelf's notch on that side only
    nail_dip: float = 0.0        # a dip this deep in the rib top by the shelf edge: a fingernail lifts the shelf there
    ledge_45: bool = False       # ledges with 45 degree undersides (the tray prints them without overhang)
    loop_gusset: bool = False    # 45 degree gusset under the loop lug
    floor_arrow: str = None      # cut into the bay floor (e.g. 'провода →'): the cell's lead end
    board_locate: bool = False   # -z pads (the board edge sits switch_wall_clr off the wall) + +z crush ribs
    crush: float = 0.1           # crush rib interference with the board edge
    led_land: float = 0.0        # a flat land of this diameter round the LED spot, so the skin is one flat piece
    pillar_roofs: bool = False   # 90 degree cone at the blind screw end, 45 degree roof over the nut pocket and slot
    flat_top: bool = False       # flat front, 45 degree front chamfer (front_chamfer): the cover prints face down
    front_chamfer: float = 1.2
    crown_style: str = 'facets'  # 'pillow' (#16C): a smooth dome over the same panel outline, the same peak, no creases
    crease_r: float = 0.0        # round the crown's creases (facet to facet, facet to diamond) with this radius (#14C)
    crown_shell: float = 0.0     # > 0: the crown is a shell this thick (measured vertically), not a solid wedge (weight)
    boss_slim: bool = False      # nut boss only round the nut (+ a 45 degree cone back to the pillar), not to the ceiling
    cover_fins: float = 0.0      # > 0: designed breakaway fins under the cover's ceiling at this pitch (crown-up print)
    tab_square: bool = False     # the tongue's top edge flat (no 45 degree lean, no end chamfer): no one-line island
    tab_ties: int = 0            # breakaway ties across the lower slit (0.4 x the inner 0.6), 4 mm apart from the hinge
    tab_end_ties: bool = False   # #9 B1 (owner's pick): one 0.45 tie across the end slit at mid height, inner 0.5
    tab_tie_w: float = 0.4       # lower-slit tie width along the tongue (the slicer must see it as a support column)
    led_normal: bool = False     # the LED pocket's ceiling follows the outer face at led_skin (normal, not vertical), the
    led_body: bool = True        # outside untouched; led_body False: no separate skin body (no perimeter ring on the face)
    led_skin_check: bool = True  # assert the skin range (False: report only, for option renders)
    led_grow: float = 1.45       # the 26-direction Minkowski undershoots between directions: grow r = led_skin x this
    motor_rot180: bool = False   # the module turned 180 deg on the seat (owner, P10 fit): J1 on the -x (board) side,
                                 # the coin under the hold-down bar; J1 / coin positions from the photos (to be measured)
    hold_feet: float = None      # 10.3b: feet from the hold-down bar down to the motor PCB top + this (either side of the coin)
    shelf_ribs: bool = False     # 10.2C (owner: one piece, the 13.2B fence + the 11.2 posts): prints top up; designed
                                 # breakaway ribs under the plate (one line, 0.2 gap), their own body, snapped off after
    hold_legs: bool = False      # 10.3b: legs from the hold-down bar's ends down to the shelf top (+0.2)
    motor_kind: str = 'grove'    # 'da7280': check() takes the DA7280 on shelf 17.3c and the battery socket / plug envelope
    hold_down: bool = True       # False (#18.1e, DA7280 on 17.3c): no hold-down bar, boss or legs at all (pinned board)
    # 10.3b generic hold-down boss under the bar centre (retune these for a new motor module; hold_feet is the old name)
    hold_boss: float = None      # gap over the target plane (None: no boss)
    hold_boss_w: float = 10.0    # boss width across z, centred on the module
    hold_boss_y: float = None    # target plane (None: the module PCB top); e.g. the seat-post tops for another module
    hold_boss_gaps: tuple = None # per-foot gap over the target plane, in z order after the keep-out split (None: hold_boss)
    hold_boss_keep: tuple = ()   # z bands off the module centre kept clear (a part under the bar, e.g. the coin)
    motor_dz: float = 0.0        # the module's as-built z offset from its packed place (17.2b holds it in the exact 11.2
                                 # posts, 0.69 toward -z of the v1.3 axis); moves the envelope in check and the frame's feet
    coin_at: tuple = None        # motor_rot180: the coin's centre off the module centre (dx, dz); None = (2.0, 0.0)
    extra_dowels: tuple = ()     # 18.3 (owner, 2026-09-29): more dowels (x, z, web_x): a boss r 3 under the line in the
                                 # frame, a web from it to x = web_x (None: none), the same holes and lead-ins as the posts
    usb_bar_x: float = None      # 18.3b: a crossbar lip to lip at this x, t_sp under the line (the USB-end dowels' bosses)
    face_bosses: bool = False    # #20 (owner, 2026-09-29): square bosses under 10.3a's back at the 4 screws carry the M2 nuts
                                 # (the frame pillars' nut bosses move into the face), so the screws clamp the face itself
    face_boss_w: float = 7.0     # the boss diameter: walls 1.13 at the nut's corners, 1.45 at its flats
    cover_onepiece: bool = False # #19 (owner, 2026-09-29): 10.3a + 10.3b as ONE part (no dowels), so the 4 screws clamp the
                                 # whole cover; printed crown up on the pillar tips over designed breakaway fins (cover_fins)
                                 # plus a one-line fin ring under the lip
    ear_out: float = 2.0         # the motor module's M2 ears past the PCB edge (the envelope in check)
    mark_outside: bool = False   # #17: the tray number on the outside bottom (read with the keyring flipped, loop up)
    loop_support: bool = False   # paint the slicer's grid support under the loop (as #7 printed) when it has no fins
    partition_lip: float = 0.0   # #17 option D: a lip this tall on the partition top stops the one-piece shelf's edge
                                 # (0.2 play); lowered to 0.2 under anything of the shelf that hangs over the partition
    hold_legs_z: tuple = None    # the frame legs' z spans; None = 3..7 off the motor module's sides
    shelf_support: bool = False  # 17.2: paint the slicer's grid support under the one-piece shelf's plate (10.2C's ribs failed)
    face_shell: str = ''         # 10.3a slicer shell: 'G1' = build_bambu SHELL_G1 (10% gyroid, 2 bottom, 6 top at 0.08, ironing)
    port_pocket: float = 0.0     # 10.2 Grove fit: a pocket this deep in the +z wall behind the A0/D0 plug (wall >= 0.8 left);
                                 # the UART plug has the D1 tongue behind it, so it gets width (grove_zone) only
    motor_posts: bool = False    # bay_hold: the #11 seat (4 corner posts, owner-proven) instead of the v1.2 rails
    rib_teeth: tuple = None      # (width z, height): teeth on the partition top at the cell ends; the shelf edge stops on them
    xiao_pocket: float = 0.0     # a recess this deep in the cover's ceiling over the XIAO (more margin over its +2.0 lift)
    ceil_margin: float = 0.0     # more room over the XIAO's +2.0 lift: the parting line this much higher (cover thinner)
    loop_boss_flat: bool = False # the #7 nub over the shelf's far edge: flat underside (owner: it printed great, holds the shelf)
    far_lip: bool = True         # bay_hold: the v1.2 far lip + the shelf's far-edge wedge (None of it on #7)
    seat_split: bool = False     # the #11 posts as their own part (10.4) on a ring, sunk seat_groove into the shelf top
    seat_groove: float = 0.4
    shelf_edge_clr: float = None # the shelf's partition-side edge off the partition (None = shelf_clr); 13.2B: 0.2
    fence_lead_b: float = 0.0    # 45 degree chamfer on the fence's battery-side bottom edge (pushes the cell clear)
    ledge_cell_trim: bool = False  # trim the side ledges where the thickest cell lies clear of the fence
    cover_split: bool = False    # 10.3a face plate (above the parting line) + 10.3b frame (lip, pillars, spine), 2 dowels
    peg_clr: float = 0.1         # dowel hole clearance per side: 0.10 (holes 3.20) validated by #15 (owner, 2026-09-29)
    peg_d: float = 3.0
    peg_depth: float = 2.5       # in each half
    peg_clr_face: float = None   # #18.1 (owner: #18's six dowels had to be hammered in): the face holes' clearance (None: peg_clr)
    peg_lead: float = 0.3        # 45 degree lead-in at each dowel hole's mouth (#18.1: 0.5)
    dowel_blocks: bool = False   # #18.1: the loop-end -z dowel boss (it broke on #18) joined to the bar by a solid block,
                                 # full depth where it clears the coin by >= 1.0, stepped over the coin's edge
    proto_label: str = None      # the part number on the cover halves, instead of '<proto>.3' (e.g. '18.1-3')
    frame_plate: float = None    # #18.1 (owner): the frame's lattice filled to a plate t_sp (0.8) under the line from this x
                                 # to the loop end (the USB end stays open over the XIAO); windows in frame_windows
    frame_windows: tuple = ()    # (x0, x1, z0, z1) cut out of that plate
    face_label_x: float = 140.0  # the face label's centre x
    centre_dowels: bool = True   # the two centre dowel posts (152 / 172 on the axis); #18.1b drops them
    # #18.1c: the motor module's connector, as named numbers (the Grove vibration module is temporary: the final motor
    # changes only these). Its Grove header stands motor_conn_h over the PCB (Seeed 110990030, 1125S-4P vertical DIP:
    # 8.6 body), the cable plug motor_plug_proud over it (estimate). The face gets a pocket over motor_conn_xz (x0, x1,
    # dz0, dz1 off the module centre) as deep as face_pocket_skin allows, in face_pocket_step terraces (flat bridges)
    motor_conn_h: float = 8.6
    motor_plug_proud: float = 1.0
    motor_conn_xz: tuple = None
    face_pocket_skin: float = 0.8
    face_pocket_step: float = 0.24
    dowel_locators: tuple = ()   # #18.1c: (round (x, z), slotted (x, z)): the face holes there keep peg_clr_face; the
    peg_clr_loose: float = None  # slot runs +-peg_slot along x; every other face hole gets peg_clr_loose per side
    peg_slot: float = 0.3
    face_screws: tuple = ()      # #22 (owner, 2026-09-30): the face held to the frame by hidden self-tapping screws from
                                 # under the frame plate, no dowels: (x, z) of each; face_screw = (length, head d, head k,
                                 # pilot d, clearance d, locator clearance d, counterbore d, boss d, max engagement)
    face_screw: tuple = (4.5, 4.2, 1.8, 1.7, 2.5, 2.25, 4.6, 6.6, 2.4)   # DIN 7981 C ST2.2 x 4.5 (A2), PLA pilot 1.7
    face_screw_locators: tuple = ()   # the (x, z) whose frame hole is the snug locator clearance
    dowel_sprue: bool = False    # #18.1c (owner): the dowels printed standing on a carrier (a model-kit sprue, twice the
                                 # count) with a small tab on each D-flat's base, plus a pusher; not loose on the plate
    peg_ribs: tuple = None       # #18.1c: the non-locator face holes as crush-rib holes (bore d, rib-tip d, rib width, count):
                                 # the bore clears the position drift, the ribs keep the grip (they crush where they touch)
    short_dowels: tuple = ()     # #18.1b: USB-end dowels where the face is thin: (x, z, rib_to_x, rib_to_z) - a post like the
                                 # others, a rib (1.2 wide, lip_h deep) to the screw pillar at (rib_to_x, rib_to_z)
    short_face_depth: float = 1.5   # their face hole depth (the face is ~2.4 there: 0.9 skin)
    short_len: float = 4.0       # their dowel length (the rest sits in the frame post)
    short_lead: float = 0.3      # their lead-in (more straight grip than 0.5)
    # #18.1d (owner, 2026-09-30: the end dowels sank, the face came off there): the dowel bottoms in a blind frame hole,
    # so the frame depth sets the protrusion P = dowel - frame depth; the face hole is P + 0.04 (one layer rounding; 18.1c
    # had 0 and seated flush). Real grip = P - 0.3 (dowel end chamfer) - rib_lead. None = the older rules
    peg_face_d: float = None     # face / frame hole depth of the extra (middle) dowels and the locators (None: peg_depth)
    peg_frame_d: float = None
    short_frame_d: float = None  # frame hole depth of the corner (short_dowels) positions (None: short_len - short_face_depth);
                                 # a tuple: one per short_dowels entry
    short_face_ds: tuple = ()    # the corner face hole depths, one per short_dowels entry (() : short_face_depth)
    sprue_pusher: bool = True    # dowel_sprue: the pusher goes on the same plate (#18.1d: the owner has one already)
    short_post_r: tuple = ()     # their radius, one per short_dowels entry (() : 3.0)
    short_post_d: tuple = ()     # the corner posts' depth under the line, one per short_dowels entry (() : frame + 0.6)
    rib_lead: float = None       # the ribbed face holes' mouth cone and rib start (None: the hole's lead)
    frame_lead: float = None     # every frame hole's mouth cone (None: the hole's lead)
    pusher_cups: tuple = (1.5,)  # the pusher's cup depth at each end (= the protrusion it leaves when its rim meets the frame)
    mid_post_d: float = 3.2      # the middle (extra_dowels) posts' depth under the line
    post_trims: tuple = ()       # 18.2-B: ((x, z, dy), ...) a dowel post this much shorter (its frame hole then runs through)
    sml_sprues: tuple = ()       # 18.2-B: ((letter, dowel d), ...) one labelled 10-dowel sprue per size instead of the one sprue
    sprue_spares: int = None     # dowels on the sprue = the cover's count + this (+1 for test_joint); None: twice the count
    test_joint: bool = False     # 18.2-G (lead, 2026-09-30): the plate also carries one labelled 'ТЕСТ' joint (a frame block
                                 # with one stop hole + a face tab with one ribbed hole, this version's exact numbers)
    dowel_flat: float = 0.3      # the sprue dowels' D-flat depth (#15: vents a blind hole). 0 = round (#18.1e: a flat facing
                                 # one of a face hole's 3 ribs takes that rib out, 0.41 of radial play: the fit is a lottery)
    frame_neck: float = None     # #18.1e: every frame hole ends in a through neck this wide: the dowel's end (Ø2.4 after its
                                 # 0.3 chamfer) stops on the ledge, carried by the post's wall, not by a thin floor
    usb_face_clean: bool = False # the USB window and its lead-in cut the cover only below the line (P10e: they notched the
                                 # face's edge above the window, which the face does not need: the window top is the line)
    face_back_chamfer: float = 0.0   # 45 degree chamfer round 10.3a's back (bed) perimeter; the frame lip's gets 0.3
    led_mouth: float = 0.0       # 45 degree chamfer at the LED pocket's mouth on the bed face (first-layer ring)
    led_rect: tuple = None       # (length x, width z): a rectangular LED spot instead of the led_d circle
    led_side_wall: float = 0.0   # > 0: the pocket also keeps this much horizontally from vertical outer faces (P10e: the
                                 # spot straddles the 1 mm facet step 4.0 from the USB edge; a 0.3 skin there printed as slits)
    # face_pockets: kept as code, off by default. Measured 2026-09-29 (face sliced alone, 14C): the pockets do not save
    # weight against 15% infill (9.19 g solid -> 9.07 g at a 1.2 skin, 9.53 g at 1.6; with thinner shells they weigh
    # more than without) and add ~20 min: the ribs, the pocket walls and a bottom skin on every ceiling replace sparse
    # infill. The shell settings (build_bambu.SHELL_G1: 6.36 g) are the lever.
    face_pockets: float = None   # 10.3a lightening (owner's H sketch): pockets in the back, the crown kept this thick over
                                 # them (vertical); None = solid. Terraced ceilings every pocket_step, ribs along the creases
    pocket_step: float = 0.4
    pocket_rim: float = 4.0      # solid band round the back: the tray wall top + the frame lip (3.6) + 0.4
    usb_end_w: float = 0.0       # 10.3b: the lip ring this wide (inward) for 7 mm each side of the USB notch (P10f)
    frame_tie: bool = False      # 10.3b: gussets at the USB-end pillars + a breakaway tie across the USB notch (P10e)
    part_suffix: str = ''        # after the part number on a variant (13.2A)
    top_label: str = None        # raised label on the shelf top, '|' splits lines (#11: ВЕРХ|МОТОР)
    seat_center: bool = False    # v1.1 seat: centre the module on the case's width axis (#11)
    seat_l: float = None         # v1.1 seat: PCB length between the posts along z (None = the datasheet 24; #11: 20)
    seat_drop: float = 0.0       # v1.1 seat: lower the PCB this much (#11: to the pins + 0.3)
    post_keepout: float = 0.0    # v1.1 seat: posts this far off the tray (#11: 0.3)
    proto: int = None            # prototype number (fit / test builds only): debossed on the tray floor and the shelf underside
    fence: float = 1.0           # locating fence outside the motor PCB
    fence_h: float = 1.2
    switch_slot_clr: float = 0.4    # slot clearance along the travel (x)
    switch_slot_off: float = 1.25   # v1.2: the slot runs this much further past the OFF end, so a nail catches the lever
    switch_wall_clr: float = None   # board edge to the switch-side inner wall (None = v1.1: the lever tip clr inside);
                                    # 0.3 needs tighter USB-end corners (outer_box: 0.85 short), pending the owner
    switch_scoop: float = 0.8       # v1.2: nail scoop around the slot in the outer face, this deep (45 degree sides)
    switch_slot_clr_y: float = 0.7  # slot clearance up and down (the lever height is read from photos, see SWITCH_DY)
    usb_chamfer: float = 0.25   # 45 degree lead-in at the outer face; small so the tray keeps 0.8 mm over the window
    usb_r: float = 1.6          # opening corner radius (the receptacle ends are round, r 1.6)
    # Grove cable (kit: 20 cm), coiled in the free space over the A0/D0 shroud row
    cable_len: float = 200.0
    cable_w: float = 4.4         # 4 x 26 AWG side by side
    cable_t: float = 1.1
    # OLED window (case v2; v1 has none): 'none', 'open' (a cut-out over the glass), 'lip' (the cut-out plus a recess
    # from inside for a glued clear sheet). The glass sits 12 mm under the front, so the cut-out is the floor of a
    # well that hangs from the front down to window_gap above the glass: no parallax, no view into the case.
    window: str = 'none'
    window_inset: float = 0.2    # aperture = OLED glass minus this per side (the glass border is black)
    window_r: float = 0.8        # aperture corner radius
    window_gap: float = 0.5      # bezel underside to the glass top
    window_bezel: float = 1.2    # well floor (bezel) thickness
    window_chamfer: float = 0.3  # 45 degree chamfer on the aperture's top edge
    window_ledge: float = 0.4    # flat floor ring between the aperture and the well walls
    window_wall: float = 1.0     # well wall thickness (horizontal)
    window_deg: tuple = (0.0, 12.0, 15.0, 15.0)   # wall draft from vertical: -x (USB end), +x (loop end), -z, +z
    window_kink_y: float = 7.6   # the -x wall stays vertical up to here (0.3 mm past the XIAO), then opens
    window_kink_deg: float = 30.0
    window_top_chamfer: float = 0.8   # 45 degree chamfer where the well meets the front
    window_field: float = 1.5    # faceted fronts: flat field this far around the well (the facets stop there); 0 = none
    window_field_h: float = 0.6  # field height over the rim
    window_lip: tuple = (1.0, 0.6)    # 'lip': recess width per side and depth, from inside (a 0.5 mm sheet sits flush)
    # test prints: a label raised on the floor's inner face under the board (not visible from outside); '' = none
    label: str = ''
    label_at: tuple = (131.0, 0.0)   # x, z of the label centre (clear of the parts under the board)
    label_size: float = 5.0
    label_h: float = 0.3


VARIANTS = dict(a=dict(corner='circle'), b=dict(corner='g2'), c=dict(corner='stadium'))

# Case versions. v1: the locked case (no OLED window). v2 (owner, 2026-09-28): the same board stack with a window over
# the OLED and a thinner shell; v2flat: v2 without the facet relief (flat front, the thinnest). version_p() builds P.
# Front fillet 2.5 (v1 3.0): with less than about 1.7 mm between the fillet radius and the front plate, Bambu Studio
# spreads the cover's supports 4.5 mm past its ends (measured: fillet 2.0 or plate 1.0 with 2.5), instead of 0.7.
V2 = dict(wall=1.6, floor=1.2, cover=0.8, fillet_front=2.5, latch_e=0.7, window='lip', rim_band=0.0, floor_chamfer=0.0)
# v1.1 has the same 1.6 / 1.2 walls since the owner's pick (2026-09-28); v2 keeps its thinner front plate and fillet, and
# needs neither the top band nor the floor-corner fill (0.99 / 1.0 mm measured)
# v1.0: the walls of the trays printed before v1.1 (2.0 / 1.4, no band or corner fill). The bay, ledges and loop end
# are those of the printed v1 trays (shelf_variants.py builds shelves for them); the USB-C end is v1.1's.
VERSIONS = dict(v1={}, v2=V2, v2flat=dict(V2, facet_angle=0.0, facet_min=0.0, window_field=0.0),
                **{'v1.0': dict(wall=2.0, floor=1.4, rim_band=0.0, floor_chamfer=0.0, end_wall=0.0, low_floor=False,
                                 grove_zone=3.5, switch_slot_off=0.0, switch_wall_clr=None, switch_scoop=0.0),
                   # #11 throne test: the #7 (v1.1) shelf with the seat hugging the 20 x 20 PCB, 1.2 lower, posts 0.3 off the tray
                   'v1.1-throne': dict(bay_hold=False, seat_l=20.0, seat_center=True, seat_drop=1.2, post_keepout=0.3, top_label='ВЕРХ|МОТОР',
                                       switch_slot_off=0.0, switch_scoop=0.0),
                   # #13 fence variants: the #11 shelf printed top down (fence up, no support), so without the seat
                   # posts; the label cut into the top, the number 0.5 deep underneath
                   'v1.1-fence': dict(bay_hold=False, seat_l=20.0, seat_center=True, seat_drop=1.2, post_keepout=0.3,
                                      top_label='ВЕРХ|МОТОР', switch_slot_off=0.0, switch_scoop=0.0, seat_posts=False,
                                      top_label_deboss=0.5, under_depth=0.5, top_chamfer=0.3),
                   # #10 (owner's width option 3 + the independent audit, 2026-09-28): 47.8 x 89.5, the board 0.3 off
                   # the switch wall and located by its edge, a rigid bay (the longest cell + 0.3), the shelf 1.2 with
                   # the #11 motor seat, partition teeth and a key; the faceted cover in two halves (10.3a face plate,
                   # 10.3b frame) joined by two dowels on the parting line, both printed without support
                   'v1.2': dict(grove_zone=2.0, switch_wall_clr=0.3, g2_req_bottom=3.55, len_extra=0.481,
                                shelf=0.8, shelf_clr=0.2, shelf_edge_clr=0.2, motor_drop=1.2, motor_pins=1.6, bay_len=31.9,
                                bay_leaf=False, bay_detent=False, motor_retain=True, far_lip=False, loop_boss_flat=True,
                                shelf_key=(3.0, 1.5), nail_dip=1.2, ledge_45=True, loop_gusset=True,
                                floor_arrow='провода →', board_locate=True, led_land=0.0, pillar_roofs=True,
                                top_label='ВЕРХ|МОТОР', floor_top=-5.75, motor_posts=True, post_keepout=0.3,
                                seat_split=True, under_fence=('full', 2.0, 0.0), fence_lead_b=0.8, ledge_cell_trim=True,
                                top_label_deboss=0.5, under_depth=0.5, top_chamfer=0.3,
                                xiao_pocket=0.0,
                                cover_split=True, peg_clr=0.1, ear_out=1.2, crease_r=1.5, led_normal=True, led_body=False,
                                lip_w=1.6, frame_tie=True, usb_face_clean=True, face_back_chamfer=0.5, led_mouth=0.3,
                                led_side_wall=0.9, usb_end_w=2.4, motor_rot180=True, hold_boss=0.2, hold_boss_w=19.0, hold_boss_keep=((-5.6, 5.6),), hold_legs=True,
                                tab_t=1.6, tab_square=True, tab_ties=2, tab_end_ties=True, tab_tie_w=0.6)})
# v1.3 (owner, 2026-09-29): the Grove plug's wires were crushed flat against the +z wall (1.2 from the plug back to the
# wall, the wire is 1.3): +1.8 on +z only (47.8 -> 49.6) plus a 0.8 pocket behind A0/D0 give its wires an R 2.5 bend
# (UART R 1.7: the D1 tongue sits behind it, no pocket there). The D1 nub grows by itself (0.74 -> 2.54).
# len_extra: the wider corners need less loop-end growth; keep the owner's 89.5 (x1 207.258)
# the face: #16A (owner, 2026-09-29): facets with crease_r 20, printed with the G1 shell
# PENDING v1.3.1 tray (owner, 2026-09-29, from the printed 17.1): the A0 pocket shows through (0.8 wall) and breaks the
# rim top as a ragged slit. Next tray: pocket 0.4 deep (1.2 wall, 3 lines), its top at y 4.5 under a 45 degree roof
# (the wire's bend ends at y 0.95 + R 2.1 + 1.3 = 4.35), solid wall and a continuous rim above; A0 bend R 2.1.
# the loop (owner, 2026-09-29, 'gigantic'): back to the #7 lug exactly, no fins under it (loop_gusset off; the inner
# boss stays the #7 flat one), and the grid support #7 had painted under it; the tray number on the outside bottom
VERSIONS['v1.3'] = dict(VERSIONS['v1.2'], grove_zone=3.8, port_pocket=0.8, len_extra=0.481 + 0.47084,
                        crease_r=20.0, face_shell='G1',
                        loop_gusset=False, loop_support=True, mark_outside=True,
                        # 17.2 = the 10.2C shelf (owner: the construction is perfect, only its rib lattice failed): one
                        # piece, 13.2B fence + 11.2 posts, top up, the plate on the slicer's grid support (shelf_support);
                        # frame legs out to |z - 2.5| >= 15 (room for a 29.2-wide DA7280)
                        seat_split=False, shelf_ribs=False, shelf_support=True,
                        # the tray key stays (owner, 2026-09-29): the printed 17.1 carries it; 17.2b has its notch
                        hold_legs_z=((-16.5, -12.5), (17.5, 21.5)))
# #18 cover (v1.3-p18, owner, 2026-09-29): fits the printed 17.1 tray + 17.2b shelf; the tray, the loop and the shelf
# are v1.3's. Differences from #17 (17.3a/b), each asked for by the owner:
#  - face retention: 17.3a sat on 2 dowels only and its ends lifted. 4 more dowels where the face is >= 3.3 thick over the
#    lead-in disk (2.5 hole + 0.8 skin). The crossbar legs (z -14.5 / 19.5) have 2.05 there, so the loop-end pair moves
#    inward: +z on the hold-down bar over the +z foot, -z beside the bar on a web (off the coin and J1). The USB end has
#    >= 3.3 only over the XIAO (no room under the line), so its pair sits on a new crossbar just past the XIAO (x 144; +z stops short of the A0 cable zone).
#  - the hold-down feet (17.3b fit, owner photos 24fd6e5b / 59bf98e6): the coin sits under the -z foot (the A0 shroud is on
#    the photo's left = +z; the owner cut the foot he calls left, seen from the USB end = -z). The coin is modelled at
#    (dx 1.0, dz -4.5) off the module centre (edge 0.5 in from the -z PCB edge); the -z foot stops 1.0 over its top
#    (PCB top + 3.4 + 1.0), the +z foot keeps +0.2 on bare PCB. The module sits 0.69 toward -z (17.2b = the 11.2 posts).
#  - the face shell T08 (owner: the G1 gyroid showed through the 0.48 top): 10 x 0.08 top over 15% gyroid; crown unchanged.
VERSIONS['v1.3-p18'] = dict(VERSIONS['v1.3'], face_shell='T08', motor_dz=-0.69, coin_at=(1.0, -4.5),
                            hold_boss_gaps=(3.4 + 1.0, 0.2),
                            extra_dowels=((191.9, 7.25, None), (183.5, -7.25, 189.44), (144.0, -8.0, None), (144.0, 10.3, None)),
                            usb_bar_x=144.0)
# #19 (owner, 2026-09-29, fallback c): the #18 cover as one part, no dowels: the 4 M2 screws clamp face and frame
# together. The lip stays on the cover (the tray cannot take it: the face is 1.0 thick over the lip ring), so 19.3 fits
# the printed 17.1 unchanged. Crown up on the pillar tips over painted slicer grid support (the 17.2 settings the owner
# found easy to remove), blocked at the pillars / nut slots and the LED. Designed fins (cover_fins 4 + the lip ring)
# weighed 12.9 g of fins alone.
# #20 (owner, 2026-09-29): the #18 face and frame (2 centre dowels, no USB crossbar) with the nuts in square bosses
# under the face's back at the 4 screws (M2 x 16 unchanged), so the screws clamp the face. Fits the printed 17.1.
VERSIONS['v1.3-p20'] = dict(VERSIONS['v1.3-p18'], extra_dowels=(), usb_bar_x=None, face_bosses=True)
# #18.1 (owner, 2026-09-29): #18 closed the USB end perfectly, but its 6 dowels had to be hammered in, the loop-end -z
# dowel boss (on a 1.2 web) broke, and the face label read mirrored from the back. Face holes 0.125 per side (Ø3.25,
# thumb press), frame holes stay 0.10 (the dowel stays in the frame), 0.5 lead-ins, that boss on a solid block to the
# bar, the label read from the back. Everything else is #18.
# Owner additions: the frame lattice filled to a thin plate (0.8, as the webs) from past the XIAO to the loop end, with
# a window over the motor's J1 (its plug and cable); and 10 dowels in a pattern mirrored across both axes about
# (162, the case axis): USB x 144.5 and loop x 179.5 at z axis +- 7.8 and on the axis, the centre pair 152 / 172, and
# x 162 at +- 7.8. The face is >= 3.3 over every lead-in (3.69 .. 4.72); +- 7.8 stops the +z row clear of the A0 cable
# zone, x 144.5 clears the XIAO, x 179.5 clears J1. The #18 bar / web dowels and the USB crossbar go.
_zc = 2.505
VERSIONS['v1.3-p18.1'] = dict(VERSIONS['v1.3-p18'], peg_clr_face=0.125, peg_lead=0.5, proto_label='18.1-3', usb_bar_x=None,
                              extra_dowels=tuple((x_, _zc + dz_, None) for x_ in (144.5, 179.5) for dz_ in (-7.8, 0.0, 7.8))
                              + ((162.0, _zc - 7.8, None), (162.0, _zc + 7.8, None)),
                              frame_plate=141.0, frame_windows=((181.9, 189.0, 1.81 - 6.0, 1.81 + 6.0),), face_label_x=139.0)
# #18.1b (owner, 2026-09-29: "the edge will peel off"; his own layout on the 18.1 pattern): the 4 dowels on the axis go;
# the 6 at x 144.5 / 162 / 179.5, z axis +- 7.8 stay; 4 corner dowels mirrored about the axis, nudged 1.25 inward from
# his (192.5 / 132.5, 18.4 / -12.4) to where the face is 2.31: the short type (1.5-deep face holes, 0.8 skin, 0.3
# lead-ins, 4 mm dowels). The frame plate now spans the USB end too, open only over the XIAO's parts and the plug.
VERSIONS['v1.3-p18.1b'] = dict(VERSIONS['v1.3-p18.1'], proto_label='18.1b-3', centre_dowels=False,
                               extra_dowels=tuple((x_, _zc + dz_, None) for x_ in (144.5, 162.0, 179.5) for dz_ in (-7.8, 7.8)),
                               short_dowels=((191.75, 17.4, None, None), (191.75, 2 * _zc - 17.4, None, None),
                                             (133.25, 17.4, None, None), (133.25, 2 * _zc - 17.4, None, None)),
                               frame_plate=116.0, frame_windows=((117.0, 137.6, -6.0, 8.0), (181.9, 189.0, 1.81 - 6.0, 1.81 + 6.0)))
# #18.1c (owner, 2026-09-30): the loop end of #18.1b stood 1-1.5 proud: the Grove header on the motor module is a
# vertical 8.6 part (the model had 5.1), its plug went through the plate slot into a flat face back. Changes, each asked
# for: a terraced pocket over the plug (0.6 skin, the plate slot's footprint + 0.3), the +z hold-down foot to touch the
# PCB (it stood 0.2 off), 2 dowel locators (round Ø3.25 at 144.6, slot 3.25 x 3.85 along x at 179.4, z 10.3); the other
# 8 face holes Ø3.45 with 3 crush ribs to Ø3.225 (0.6 wide: a 0.3 rib is under the 0.42 line and would not print), the
# x 179.5 / 144.5 dowels 0.1 toward the middle (0.5 off the taller header).
VERSIONS['v1.3-p18.1c'] = dict(VERSIONS['v1.3-p18.1b'], proto_label='18.1c-3',
                               motor_conn_xz=(0.5 - 1.3, 5.5 + 1.3, -5.0 - 1.3, 5.0 + 1.3), face_pocket_skin=0.6,
                               hold_boss_gaps=(3.4 + 1.0, 0.0),
                               extra_dowels=tuple((x_, _zc + dz_, None) for x_ in (144.6, 162.0, 179.4) for dz_ in (-7.8, 7.8)),
                               dowel_locators=((144.6, _zc + 7.8), (179.4, _zc + 7.8)), peg_ribs=(3.45, 3.225, 0.6, 3),
                               dowel_sprue=True)
# owner, same day: one dowel type (the 4 mm ones were too small to hold, fell out, barely seated): the corner dowels are
# the normal 5 mm dowel, 1.5 in the thin face and 3.5 in a deeper frame post (4.1 under the line). The USB corners move
# 3.55 toward the middle (x 136.8): a 4.1-deep post at 133.25 sat over the screw pillars' nut-insertion slots. The
# middle dowels moved outward only where free (face >= 3.69, clear of the A0 cable zone): (162, -5.3) -> (162, -10.2).
VERSIONS['v1.3-p18.1c']['short_len'] = 2 * P.peg_depth
VERSIONS['v1.3-p18.1c']['short_dowels'] = ((191.75, 17.4, None, None), (191.75, 2 * _zc - 17.4, None, None),
                                          (136.8, 17.4, None, None), (136.8, 2 * _zc - 17.4, None, None))
VERSIONS['v1.3-p18.1c']['extra_dowels'] = ((144.6, _zc - 7.8, None), (144.6, _zc + 7.8, None), (162.0, _zc - 12.7, None),
                                          (162.0, _zc + 7.8, None), (179.4, _zc - 7.8, None), (179.4, _zc + 7.8, None))
# #18.1d (owner, 2026-09-30, after assembling #18.1c: the dowels at both ends sank, the face came off there; the pusher
# and the ribs stay): every frame hole is blind and sets the protrusion (P = 5.0 - frame depth), the face hole is P + 0.04,
# all depths on the 0.2 + 0.16 k layer grid. Real face grip (P - 0.3 dowel chamfer - 0.2 rib start), 18.1c -> 18.1d:
# middle 1.7 -> 2.06 (face 2.6, frame 2.44), corners 0.9 -> 1.26 (face 1.8, frame 3.24): the face is 2.3-2.5 there and
# 2.0 would need 3.2, found only 6-7 mm inward. The loop corners 1.0 inward (their skin 0.66 over a 1.8 hole); the USB
# corner posts end at y 8.43, over the pillar nuts' path (8.35). A two-ended pusher: cup 2.56 (middle) / 1.76 (corners).
VERSIONS['v1.3-p18.1d'] = dict(VERSIONS['v1.3-p18.1c'], proto_label='18.1d-3',
                               peg_face_d=2.6, peg_frame_d=2.44, short_face_depth=1.8, short_frame_d=3.24,
                               short_dowels=((191.27, 16.52, None, None), (191.27, 2 * _zc - 16.52, None, None),
                                             (136.8, 17.4, None, None), (136.8, 2 * _zc - 17.4, None, None)),
                               short_post_d=(4.1, 4.1, 3.72, 3.72), rib_lead=0.2, frame_lead=0.2,
                               pusher_cups=(2.56, 1.76))
# #18.1e candidate (not sliced; 18.1d went to print as is, with its pusher): no pusher on the plate (the owner keeps the
# 18.1c one, cup 1.5: shallower than every protrusion, so the hole floor is the stop everywhere). The USB corner floors
# 0.47 -> 0.63 (their posts cannot go lower: the pillar nuts' path is at 8.35): frame 3.08, face 1.96 (skin 0.57),
# protrusion 1.92, grip 1.26 -> 1.42 there.
# #18.1e (owner, 2026-09-30, option 3 for the DA7280's Qwiic cable): + the +z hold-down legs 2.8 out (17.5-21.5 ->
# 20.3-24.3) so shelf 17.3b can shift the board 2.9 toward +z: its J1 cable then runs clear of the expansion board's
# 2 x 4 pin column (wires >= 1.0 off in z)
VERSIONS['v1.3-p18.1e'] = dict(VERSIONS['v1.3-p18.1d'], proto_label='18.1e-3', sprue_pusher=False,
                               short_frame_d=(3.24, 3.24, 3.08, 3.08), short_face_ds=(1.8, 1.8, 1.96, 1.96),
                               hold_legs_z=((-16.5, -12.5), (20.3, 24.3)))
# 18.1d failed (owner, 2026-09-30: the dowels fall out of the face; some punched through their frame floors). From the
# sliced G-code (fit_gate.py), 18.1c and 18.1d have the SAME face holes (rib crest Ø3.227, bore Ø3.478 at every layer),
# the same frame holes (Ø3.20, stops 0.48 / 0.80) and the same dowels (Ø2.995 with a 0.3 D-flat): (1) the face grip
# never had a designed interference (crest 0.23 over the dowel), only the print's shrink; (2) the D-flat, turned onto one
# of the 3 ribs (~60% of orientations), takes that rib out: 0.41 of radial play, a lottery per dowel; (3) a bridged
# floor under 1 mm is no stop, and 18.1d's protrusions (2.56 / 1.76) were driven with the 18.1c pusher (cup 1.5) until
# its rim landed: 0.26-1.06 past the floors. 18.1e:
#  - round dowels (dowel_flat 0; the face holes vent between their ribs, the frame holes through their necks);
#  - rib crests Ø3.125 (+0.10 over 18.1c) until the fit coupon (3.225 / 3.175 / 3.125) picks the step;
#  - every frame hole ends in a Ø1.6 through neck; the ring the dowel lands on is 1.2 thick (1.28 on the layer grid) at
#    the 6 middle, the 2 loop-corner and the loop-centre posts (3.64 / 4.44 / 3.64 deep); a stop you feel, any pusher;
#  - the 2 USB corners keep 0.48 rings (their posts end over the pillar nuts' path, 8.35): owner to decide (the USB end
#    is clamped by its screws);
#  - a loop-end centre dowel on the axis at x 192.0 (owner's mark: in line with the corners, 1.03 of wall to the
#    connector pocket): face 2.6 (face 3.38 there, skin 0.78), frame 2.44 (2.56 proud, the middle type), post 3.64 to
#    y 8.51: 0.81 over the DA7280's LRA (17.3b), 2.9 to its J2. It overlaps the 17.2b Grove module's coin: 18.1e is for
#    the DA7280 only (lead, 2026-09-30). v1.3-p18.1e-g: the same with that post at 2.78 (0.42 over the coin, a 0.34
#    ring) for a closure test with the old motor only.
VERSIONS['v1.3-p18.1e'].update(
    peg_ribs=(3.45, 3.125, 0.6, 3), frame_neck=1.6, mid_post_d=3.64, dowel_flat=0.0,
    short_dowels=VERSIONS['v1.3-p18.1d']['short_dowels'] + ((192.0, _zc, None, None),),
    short_frame_d=(3.24, 3.24, 3.24, 3.24, 2.44), short_face_ds=(1.8, 1.8, 1.8, 1.8, 2.6),
    short_post_d=(4.44, 4.44, 3.72, 3.72, 3.64))
# v1.3-p18.1e-g: that 18.1e with the centre post at 2.78 (0.42 over the Grove coin, a 0.34 ring), for a closure test
# with the old Grove motor on 17.2b only (lead, 2026-09-30; not sliced until asked)
VERSIONS['v1.3-p18.1e-g'] = dict(VERSIONS['v1.3-p18.1e'], proto_label='18.1eg-3', short_post_d=(4.44, 4.44, 3.72, 3.72, 2.78))
# 18.1e for the DA7280 on shelf 17.3c (2026-09-30, after 17.3b sat on the battery socket): the board is raised 2.65
# (underside 4.70, top 6.30, LRA top 10.35, J1 / J2 tops 9.25), so the frame gives up the Grove hold-down (bar, boss,
# legs: the board is pinned, nothing presses it, nothing pushes the cover up) and the face its Grove connector pocket;
# the slot locator post moves (179.4, 10.3) -> (179.4, 14.1) off the raised J1 and its Qwiic plug; the loop-centre dowel
# moves (192.0, 2.5) -> (192.0, 5.45), off the LRA's footprint (0.8 in plan), its post 3.64 (a 1.2 ring); the loop
# corner posts r 3.0 -> 2.6 (0.8 in plan to the LRA). The board moves 0.2 toward the loop end (0.31 to the +z pillar).
VERSIONS['v1.3-p18.1e'].update(
    hold_down=False, hold_legs=False, motor_conn_xz=None, motor_kind='da7280',
    extra_dowels=tuple((x_, z_, None) for x_, z_ in ((144.6, _zc - 7.8), (144.6, _zc + 7.8), (162.0, _zc - 12.7),
                                                      (162.0, _zc + 7.8), (179.4, _zc - 7.8), (179.4, 14.1))),
    dowel_locators=((144.6, _zc + 7.8), (179.4, 14.1)),
    short_post_r=(2.6, 2.6, 3.0, 3.0, 3.0),     # the loop corners' posts r 2.6 (wall 1.0): 0.8 in plan to the raised LRA
    short_dowels=VERSIONS['v1.3-p18.1d']['short_dowels'] + ((192.0, 5.45, None, None),))
# 18.1e synced to 18.2-G's joint (owner, 2026-09-30: both versions share the strengthened face-dowel joint): round
# dowels, crest 3.075, middle / locator / centre frame 2.28 + face 2.76 (2.72 proud, grip 2.22), rings >= 1.28 round a
# through Ø1.6 neck; the loop corners to where the face is >= 3.2 and off the raised LRA: (191.27, 11.5) and
# (181.5, -10.0), frame 2.44 / face 2.6 (2.56 proud, grip 2.06); no USB corners (the screws clamp that end); the same
# sprue (+1 test, +3 spare) and ТЕСТ joint.
VERSIONS['v1.3-p18.1e'].update(
    peg_ribs=(3.45, 3.075, 0.6, 3), peg_face_d=2.76, peg_frame_d=2.28,
    short_dowels=((191.27, 11.5, None, None), (181.5, -10.0, None, None), (192.0, 5.45, None, None)),
    short_frame_d=(2.44, 2.44, 2.28), short_face_ds=(2.6, 2.6, 2.76), short_post_d=(3.72, 3.72, 3.64),
    short_post_r=(3.0, 3.0, 3.0), test_joint=True, sprue_spares=3)


# 18.2-G (owner, 2026-09-30: two versions; this is the production one for the current Grove motor on shelf 17.2b, to
# print now): 18.1e-g's retention fixes (round dowels, crests from the coupon, necked frame holes with 1.2 rings at the
# 6 middle and 2 loop-corner posts) on 18.1d's Grove frame (hold-down bar, boss and legs as 18.1d: legs at 18.1d's z,
# the Grove connector pocket in the face). The loop-centre dowel at (192.5, 5.6): off the coin (8.3 between centres,
# 0.3 in plan) and off the connector's pocket zone (x <= 189.2), so its post goes to 3.64 (a 1.2 ring) like the middle
# ones.
VERSIONS['v1.3-p18.2-G'] = dict(VERSIONS['v1.3-p18.1e-g'], proto_label='18.2G-3', hold_legs_z=None,
                                short_dowels=VERSIONS['v1.3-p18.1d']['short_dowels'][:2] + ((192.5, 5.6, None, None),),
                                short_frame_d=(3.24, 3.24, 2.44), short_face_ds=(1.8, 1.8, 2.6),
                                short_post_d=(4.44, 4.44, 3.64))
# owner, 2026-09-30 (coupon: the Ø3.125 crest, 3 dimples, holds perfectly; 3.175 comes off, 3.225 falls out): crest
# 3.125 as sliced; the 2 USB-corner dowels dropped (the screws clamp that end; their posts could not reach a 1.2 stop).
# owner, 2026-09-30 (coupon assembled: face hole 3 on the dowel standing 1.76 out of frame hole 3 holds poorly; a loose
# dowel fully in face hole 3 felt perfect; the 3.24 + neck + 1.28 ring stop sits firmly): the grip was too short. 18.2-G:
#  - every protrusion >= 2.4 into the face: middle 6 + locators + centre frame 2.28 / face 2.76 (2.72 proud, real grip
#    2.72 - 0.3 chamfer - 0.2 rib start = 2.22; skins >= 0.76); the loop corners move to where the face is >= 3.2:
#    (191.27, 11.5) and (186.5, -9.0) (the -z one clear of the coin), frame 2.44 / face 2.6 (2.56 proud, grip 2.06);
#  - crest 3.075 (one step past the coupon's 3.125), the 0.2 rib lead-in kept;
#  - every frame hole ends on a >= 1.28 ring round a through Ø1.6 neck (the stop the owner felt firm).
VERSIONS['v1.3-p18.2-G'].update(
    peg_ribs=(3.45, 3.075, 0.6, 3), peg_face_d=2.76, peg_frame_d=2.28,
    short_dowels=((191.27, 11.5, None, None), (186.5, -9.0, None, None), (192.5, 5.6, None, None)),
    short_frame_d=(2.44, 2.44, 2.28), short_face_ds=(2.6, 2.6, 2.76), short_post_d=(3.72, 3.72, 3.64), test_joint=True,
    sprue_spares=3)   # owner: 9 round dowels + 1 for the ТЕСТ joint + 3 spares; no pusher (he has one)
# 18.2-B (owner, 2026-10-02, after 18.1e-B failed its first assembly): edition B is 18.2-G exactly (face with the Grove
# pocket, frame, dowels, joint) minus the two points the owner found pressing the DA7280 board on shelf 17.3c, both on
# the +z side: the +z hold-down leg (z 14.8-18.8) and the +z hold-down boss piece (z 7.4-11.3, beside the (192.5, 5.6)
# post). The -z leg and the -z boss piece stay (owner: they press nothing).
VERSIONS['v1.3-p18.2-B'] = dict(VERSIONS['v1.3-p18.2-G'], proto_label=None,   # owner 2026-10-03: no part codes inside the models
                               
                                hold_legs_z=((-15.185, -11.185),), hold_boss_keep=((-5.6, 5.6), (5.6, 30.0)))
# 18.2-B first print (owner, 2026-10-03): the cover did not close; with the locator post (179.4, 10.305) cut 2 mm
# shorter it closed and the dowel still held (the model had that post 0.74 into the DA7280's J1 / Qwiic plug). And
# three labelled 10-dowel sprues, S 3.00 / M 3.05 / L 3.10, in place of the one sprue: a loose dowel takes the next size.
VERSIONS['v1.3-p18.2-B'].update(post_trims=((179.4, 10.305, 2.0), (179.4, -5.295, 1.0)),   # owner: the other post of that row 1 mm too
                                sml_sprues=(('S', 3.00), ('M', 3.05), ('L', 3.10)), test_joint=False)   # owner: no ТЕСТ joint any more
# #22 (owner, 2026-09-30): #18.1c with the face held to the frame by 8 hidden DIN 7981 C ST2.2 x 4.5 screws from under
# the frame plate, no dowels. The USB-end quadrant takes none: the pillar nut slots' insertion paths, the XIAO and the
# A0 cable zone leave no room for a boss there. Locators: the snug frame holes at (144.5, 9.7) and (187.3, -12).
VERSIONS['v1.3-p22'] = dict(VERSIONS['v1.3-p18.1c'], proto_label='22-3', extra_dowels=(), short_dowels=(), dowel_sprue=False,
                            dowel_locators=(), peg_ribs=None,
                            face_screws=((143.5, -12.0), (154.0, -13.0), (180.0, -12.0), (187.3, -12.0),
                                         (144.5, 9.7), (162.0, 9.7), (181.5, 17.0), (191.0, 14.0)),
                            face_screw_locators=((144.5, 9.7), (187.3, -12.0)))
VERSIONS['v1.3-p19'] = dict(VERSIONS['v1.3-p18'], extra_dowels=(), usb_bar_x=None, cover_onepiece=True)


def version_p(name='v1', **kw):
    if name not in VERSIONS:
        raise SystemExit(f'unknown case version {name!r}; known: {", ".join(VERSIONS)}')
    return P(**{**VERSIONS[name], **kw})


# ---------------------------------------------------------------- packing
def packing(p: P):
    """Cavity box (x0, x1, y0, y1, z0, z1) and where the cell and the motor go."""
    M, D = MEASURED, DATASHEET
    c = p.clr
    # USB-C end: the end wall's outer face usb_recess in front of the receptacle mouth, unless that leaves less than clr
    # between the (end_wall thick) flat end wall and the board edge; x0 is the box face one full wall inside
    ew = end_wall(p)
    x0 = min(M['usb_c']['x_mouth'] - p.usb_recess, M['board_x'][0] - c - 0.05 - ew) + p.wall
    # switch side: v1.1 kept the lever tip clr inside the wall (the lever sat 1.9 under the outer face, out of a nail's
    # reach); v1.2 puts the wall switch_wall_clr off the board edge, so the lever runs into the slot in the wall
    z0 = (M['switch']['z_min'] - c) if p.switch_wall_clr is None else (M['board_z'][0] - p.switch_wall_clr)
    z1 = M['grove_a0']['z_mouth'] + (D['grove_plug_len'] - D['grove_socket_depth']) + p.grove_zone
    x_jst = M['jst']['x_mouth'] + p.jst_zone
    ceil_board = M['xiao_top_y'] + XIAO_DY_UP + c + p.ceil_margin
    lw, ll, lt = D['lipo']
    mw, ml, mt = D['motor']
    ml = p.motor_l if p.bay_hold else ml
    under = M['under_min_y'] - c                           # floor top under the board, nothing extra
    if p.low_floor and p.layout == 'B':
        # v1.1: the floor rises under the board as far as it can without making the case deeper: the bay stack
        # (cell, swell, shelf, motor) then ends at the XIAO's ceiling, or the floor stops clr under the lowest part
        # other than the CR1220 holder, which gets its own pocket in the floor (holder_pocket)
        bay_stack = lt + p.lipo_swell + p.shelf + mt - (p.motor_drop if p.bay_hold else 0.0) + c
        under = min(M['under_rest_y'] - c, ceil_board - bay_stack)
        if p.floor_top is not None:     # #10: keep v1's floor (the CR1220 holder pocket needs it)
            under = p.floor_top
    parts = {}
    if p.layout == 'A':     # cell and motor side by side under the board
        motor_top = -4.74 - c
        cell_top = M['under_min_y'] - c - p.foam
        y0 = min(motor_top - mt, cell_top - lt)
        y1 = ceil_board
        x1 = x_jst
        parts['motor'] = (x0 + 0.4, x0 + 0.4 + ml, motor_top - mt, motor_top, z0 + 1.0, z0 + 1.0 + mw)
        parts['lipo'] = (143.0, 143.0 + ll, cell_top - lt, cell_top, -20.0, -20.0 + lw)
    elif p.layout == 'B':   # end bay past the JST: cell on the floor, shelf, motor on top
        y0 = under
        bay_top = y0 + lt + p.lipo_swell + p.shelf + mt - (p.motor_drop if p.bay_hold else 0.0) + c
        y1 = max(ceil_board, bay_top)
        bx0 = x_jst
        x1 = bx0 + lw + 2 * c
        lz0 = -ll / 2 if not p.bay_hold else (z0 + z1) / 2 - ll / 2     # v1.2: the bay centred on the case axis too
        parts['lipo'] = (bx0 + c, bx0 + c + lw, y0, y0 + lt, lz0, lz0 + ll)
        my0 = y0 + lt + p.lipo_swell + p.shelf - (p.motor_drop if p.bay_hold else 0.0)
        # socket toward +z for the Grove cable; 1.2 mm back so the module corner clears a corner arc
        mz1 = z1 - p.grove_zone - (D['grove_plug_len'] - D['grove_socket_depth']) - 1.2
        if p.bay_hold:          # v1.2 (owner): the module centred on the case's width axis
            mz1 = (z0 + z1) / 2 + ml / 2
        mdx = p.motor_dx if p.bay_hold else 0.0
        parts['motor'] = (bx0 + c + (lw - mw) / 2 + mdx, bx0 + c + (lw + mw) / 2 + mdx, my0, my0 + mt, mz1 - ml, mz1)
        parts['shelf'] = (bx0, x1, y0 + lt + p.lipo_swell, y0 + lt + p.lipo_swell + p.shelf, z0, z1)
    elif p.layout == 'C':   # cell and motor beside the board, along the +z long side
        y0 = under
        y1 = max(ceil_board, y0 + lt + p.lipo_swell + p.shelf + mt + c)
        x1 = x_jst
        cz0 = z1
        parts['lipo'] = (130.0, 130.0 + ll, y0, y0 + lt, cz0 + c, cz0 + c + lw)
        my0 = y0 + lt + p.lipo_swell + p.shelf
        parts['motor'] = (130.0 + (ll - ml) / 2, 130.0 + (ll + ml) / 2, my0, my0 + mt, cz0 + c, cz0 + c + mw)
        z1 = cz0 + lw + 2 * c
    else:
        raise ValueError(p.layout)
    return dict(cavity=(x0, x1, y0, y1, z0, z1), parts=parts)


def end_wall(p: P):
    return p.end_wall if 0 < p.end_wall < p.wall else p.wall


def base_outer_box(p: P):
    x0, x1, y0, y1, z0, z1 = packing(p)['cavity']
    return (x0 - p.wall, x1 + p.wall, y0 - p.floor, y1 + p.cover, z0 - p.wall, z1 + p.wall)


# ---------------------------------------------------------------- silhouette
def corner_extent(p: P, width, end='top'):
    """Distance from a box corner to where the corner curve meets the straight side. end 'bottom' = the USB-C end
    (x min), whose corners are g2_req_bottom (v1.1: the end wall is flat at the receptacle), 'top' = the loop end."""
    if p.corner == 'circle':
        return p.corner_r
    if p.corner == 'g2':
        # same diagonal cut as a circle of g2_req: circle cut = r(sqrt2 - 1);
        # superellipse cut = sqrt2 * c * (1 - 2^(-1/n))
        req = p.g2_req_bottom if end == 'bottom' and p.g2_req_bottom > 0 else p.g2_req
        return req * (math.sqrt(2) - 1) / (math.sqrt(2) * (1 - 2 ** (-1 / p.g2_n)))
    if p.corner == 'stadium':
        return width / 2 - p.stadium_flat / 2
    raise ValueError(p.corner)


def _corner_pts(p: P, cx, cz, sx, sz, c, n=24):
    """Points of one corner from the end-side tangent point to the long-side tangent point.
    (cx, cz) box corner, (sx, sz) inward signs."""
    ox, oz = cx + sx * c, cz + sz * c          # curve centre
    e = 2.0 if p.corner in ('circle', 'stadium') else p.g2_n
    pts = []
    for i in range(n + 1):
        t = (math.pi / 2) * i / n
        pts.append((ox - sx * c * math.cos(t) ** (2 / e), oz - sz * c * math.sin(t) ** (2 / e)))
    return pts


def outline_pts(p: P, box, n=24):
    """Closed outline in (x, z), counter-clockwise seen from +y... order: bottom end, +z side, top end, -z side."""
    X0, X1, _, _, Z0, Z1 = box
    c = corner_extent(p, Z1 - Z0)
    cb = corner_extent(p, Z1 - Z0, 'bottom')
    pts = []
    for (cx, cz, sx, sz, rev) in ((X0, Z1, 1, -1, False), (X1, Z1, -1, -1, True),
                                  (X1, Z0, -1, 1, False), (X0, Z0, 1, 1, True)):
        q = _corner_pts(p, cx, cz, sx, sz, cb if cx == X0 else c, n)
        pts += q[::-1] if rev else q
    return pts, c


def profile(p: P, box, y):
    """CadQuery closed profile of the outline on the plane y (normal +y)."""
    pl = cq.Plane(origin=(0, y, 0), xDir=(1, 0, 0), normal=(0, 1, 0))   # local (u, v) = (x, -z)
    X0, X1, _, _, Z0, Z1 = box
    c = corner_extent(p, Z1 - Z0)
    cb = corner_extent(p, Z1 - Z0, 'bottom')
    L = lambda q: (q[0], -q[1])
    wp = cq.Workplane(pl)
    corners = ((X0, Z1, 1, -1, False), (X1, Z1, -1, -1, True), (X1, Z0, -1, 1, False), (X0, Z0, 1, 1, True))
    first = True
    for (cx, cz, sx, sz, rev) in corners:
        q = _corner_pts(p, cx, cz, sx, sz, cb if cx == X0 else c, 16)
        q = q[::-1] if rev else q
        if first:
            wp = wp.moveTo(*L(q[0])); first = False
        else:
            wp = wp.lineTo(*L(q[0]))
        if p.corner in ('circle', 'stadium'):
            wp = wp.threePointArc(L(q[len(q) // 2]), L(q[-1]))
        else:
            # exact side directions at both joins, so the spline is tangent (and, n > 2, curvature-free) there
            t0, t1 = ((-sx, 0), (0, sz)) if rev else ((0, -sz), (sx, 0))
            wp = wp.spline([L(v) for v in q[1:]], tangents=[L(t0), L(t1)], includeCurrent=True)
    return wp.close()


# ---------------------------------------------------------------- growth
def footprints(p: P):
    """(points, clearance) sets in (x, z) that must stay inside the cavity (outer outline minus wall)."""
    M = MEASURED
    k = packing(p)
    x0, x1, _, _, z0, z1 = k['cavity']
    bx0, bx1 = M['board_x']; bz0, bz1 = M['board_z']; r = M['board_r']
    pcb = []
    for (cx, cz, a0) in ((bx1 - r, bz1 - r, 0), (bx0 + r, bz1 - r, 90), (bx0 + r, bz0 + r, 180), (bx1 - r, bz0 + r, 270)):
        for i in range(19):
            t = math.radians(a0 + 90 * i / 18)
            pcb.append((cx + r * math.cos(t), cz + r * math.sin(t)))

    def rect(xa, xb, za, zb, step=0.5):
        out = []
        for x in np.arange(xa, xb + 1e-9, step):
            out += [(x, za), (x, zb)]
        for z in np.arange(za, zb + 1e-9, step):
            out += [(xa, z), (xb, z)]
        return out
    # the USB-C receptacle is not a footprint: it passes through the end wall's opening (v1.1)
    sw = p.switch_wall_clr
    sets = [(pcb, p.clr if sw is None else min(p.clr, sw)),
            # v1.2: the lever runs into the slot in the wall, so only the switch body up to the board edge counts
            (rect(*M['switch']['x'], M['switch']['z_min'] if sw is None else bz0, -19.0), p.clr if sw is None else sw),
            (rect(*M['grove_a0']['x'], M['grove_a0']['z_mouth'], z1), 0.0),       # plug + cable bend zone
            (rect(M['jst']['x_mouth'], M['jst']['x_mouth'] + p.jst_zone, *M['jst']['z']), 0.0)]
    for name in ('lipo', 'motor'):
        if name in k['parts']:
            a = k['parts'][name]
            sets.append((rect(a[0], a[1], a[4], a[5]), p.clr))
    return sets


def _inside_margin(poly, pts):
    """Signed distance of each point to the closed convex polygon boundary (positive inside)."""
    P_ = np.array(poly); Q = np.array(pts)
    A = P_; B = np.roll(P_, -1, axis=0)
    AB = B - A
    AQ = Q[:, None, :] - A[None, :, :]
    t = np.clip((AQ * AB[None]).sum(-1) / (AB * AB).sum(-1)[None], 0, 1)
    d = np.linalg.norm(AQ - t[..., None] * AB[None], axis=-1).min(1)
    # inside test for a convex polygon: same side of every edge
    cr = AB[None, :, 0] * AQ[..., 1] - AB[None, :, 1] * AQ[..., 0]
    inside = (cr >= -1e-9).all(1) | (cr <= 1e-9).all(1)
    return np.where(inside, d, -d)


def fits(p: P, box):
    poly, _ = outline_pts(p, box, 90)
    worst = 1e9
    for pts, clr in footprints(p):
        m = _inside_margin(poly, pts) - p.wall - clr
        worst = min(worst, m.min())
    return worst


_GROW = {}


def outer_box(p: P):
    """Base box, grown along x (length) until every footprint clears the chosen silhouette.
    Thickness never changes; width does not grow (the owner prefers long and narrow)."""
    key = json.dumps(asdict(p), sort_keys=True, default=str)
    if key in _GROW:
        return _GROW[key]
    X0, X1, Y0, Y1, Z0, Z1 = base_outer_box(p)
    mid = (X0 + X1) / 2

    def ok(db, dt, side):
        box = (X0 - db, X1 + dt, Y0, Y1, Z0, Z1)
        poly, _ = outline_pts(p, box, 90)
        for pts, clr in footprints(p):
            sel = [q for q in pts if (q[0] < mid) == (side == 'bottom')]
            if not sel:
                continue
            # the flat USB-C end wall is end_wall thick (x under box[0] + wall + 0.3), the rest is wall
            w = np.array([end_wall(p) if (side == 'bottom' and q[0] < box[0] + p.wall + 0.3) else p.wall for q in sel])
            if (_inside_margin(poly, sel) - w - clr).min() < -1e-3:
                return False
        return True
    res = {}
    for side in ('bottom', 'top'):
        lo, hi = 0.0, 40.0
        if ok(0.0, 0.0, side):
            res[side] = 0.0
            continue
        for _ in range(40):
            m = (lo + hi) / 2
            if ok(m if side == 'bottom' else 40, m if side == 'top' else 40, side):
                hi = m
            else:
                lo = m
        res[side] = hi
    # the USB-C end must not grow: its outer face is placed on the receptacle mouth (usb_recess)
    assert res['bottom'] < 0.02, (f'the board does not fit the USB-C end corners (needs {res["bottom"]:.2f} mm more): '
                                  'lower g2_req_bottom')
    box = (X0 - res['bottom'], X1 + res['top'] + p.len_extra, Y0, Y1, Z0, Z1)
    _GROW[key] = box
    return box


# ---------------------------------------------------------------- shape
def _halfspace_above(origin, normal, size=600.0, h=200.0):
    n = cq.Vector(*normal).normalized()
    xd = cq.Vector(0, 0, 1).cross(n)
    if xd.Length < 1e-6:
        xd = cq.Vector(1, 0, 0)
    pl = cq.Plane(origin=cq.Vector(*origin), xDir=xd.normalized(), normal=n)
    return cq.Workplane(pl).rect(size, size).extrude(h)


def diamond_params(p: P, box):
    """The front lattice (owner sketch): a centre diamond (zone 2) and four diamonds of the same size
    sharing its edges (zone 1). Sized so the outer diamonds reach lattice_fill of the faceted panel."""
    X0, X1, Y0, Y1, Z0, Z1 = box
    b = p.lattice_fill_w * ((Z1 - Z0) / 2 - p.rim_w) / 2
    a = p.lattice_fill_h * ((X1 - X0) / 2 - p.rim_w) / 2
    return dict(xc=(X0 + X1) / 2, zc=(Z0 + Z1) / 2, a=a, b=b)


def zone_of(p: P, box, x, z):
    d = diamond_params(p, box)
    u, v = z - d['zc'], x - d['xc']
    s1, s2 = abs(u / d['b'] + v / d['a']), abs(u / d['b'] - v / d['a'])
    if s1 < 1 and s2 < 1:
        return 2
    if (s1 < 1 and s2 < 3) or (s2 < 1 and s1 < 3):
        return 1
    return 3


def _diamond(d, cu, cv, y0, y1):
    """Prism over a lattice diamond centred at (u, v) = (cu, cv), half-axes a (x) and b (z)."""
    xc, zc, a, b = d['xc'] + cv, d['zc'] + cu, d['a'], d['b']
    pl = cq.Plane(origin=(0, y0, 0), xDir=(1, 0, 0), normal=(0, 1, 0))   # local (x, -z)
    pts = [(xc + a, -zc), (xc, -(zc + b)), (xc - a, -zc), (xc, -(zc - b))]
    return cq.Workplane(pl).polyline(pts).close().extrude(y1 - y0)


def facet_panel(p: P, box):
    """Front panel with the reference's X facets: a flat centre diamond, planar facets
    falling toward the rim; the creases are the four lines through the diamond's sides."""
    X0, X1, Y0, Y1, Z0, Z1 = box
    d = diamond_params(p, box)
    xc, zc, a, b = d['xc'], d['zc'], d['a'], d['b']
    rim = Y1

    def T(u, v):
        s1, s2 = u / b + v / a, u / b - v / a
        return max(0.0, abs(s1) - 1) + max(0.0, abs(s2) - 1)
    poly, _ = outline_pts(p, box, 90)
    ctr = np.array([(X0 + X1) / 2, (Z0 + Z1) / 2])
    tmax = 0.0
    for q in np.array(poly):      # panel outline ~ outline pulled in by rim_w toward the centre
        dd = q - ctr
        qq = q - dd / np.linalg.norm(dd) * p.rim_w
        tmax = max(tmax, T(qq[1] - zc, qq[0] - xc))
    norm = math.sqrt(1 / a ** 2 + 1 / b ** 2)
    k = math.tan(math.radians(p.facet_angle)) / norm          # height per unit of the s coordinates
    crown_full = k * tmax + p.facet_min
    capped = 0 < p.crown < crown_full
    crown = p.crown if capped else crown_full
    top = rim + crown
    panel = profile(p, box, rim - 0.5).offset2D(-p.rim_w).extrude(0.5 + crown)
    for c1 in (0, 1, -1):
        for c2 in (0, 1, -1):
            if c1 == 0 and c2 == 0:
                continue
            du = k * (c1 / b + c2 / b)
            dv = k * (c1 / a - c2 / a)
            const = top + k * (abs(c1) + abs(c2))
            panel = panel.cut(_halfspace_above((xc, const, zc), (dv, 1.0, du)))
    if p.crown_style == 'pillow':  # a lofted dome: the panel outline scaled in step by step (same topology), rising as
        n_ = 10                     # a quarter sine: steep at the rim, flat on top
        base_w = profile(p, box, rim).offset2D(-p.rim_w).vals()[0]
        bb0 = base_w.BoundingBox()
        cxp, czp = (bb0.xmin + bb0.xmax) / 2, (bb0.zmin + bb0.zmax) / 2
        Lx, Lz = bb0.xlen, bb0.zlen
        dmax = Lz / 2 - 4.0                                 # the top keeps an 8 mm wide flat oval
        ws = []
        for i in range(n_ + 1):
            t = i / n_
            dd = dmax * (1 - math.cos(t * math.pi / 2)) if i < n_ else dmax
            hh = crown * math.sin(t * math.pi / 2)
            m = cq.Matrix([[(Lx - 2 * dd) / Lx, 0, 0, cxp * (1 - (Lx - 2 * dd) / Lx)],
                           [0, 1, 0, hh],
                           [0, 0, (Lz - 2 * dd) / Lz, czp * (1 - (Lz - 2 * dd) / Lz)]])
            ws.append(base_w.transformGeometry(m))
        dome = cq.Workplane().add(cq.Solid.makeLoft(ws, ruled=False))
        top_cap = cq.Workplane().add(cq.Solid.extrudeLinear(ws[-1], [], cq.Vector(0, 0.001, 0)))
        panel = profile(p, box, rim - 0.5).offset2D(-p.rim_w).extrude(0.5 + 0.001).union(dome)
    if p.crease_r > 0 and p.crown_style != 'pillow':
        panel = _round_creases(panel, p.crease_r)
    if capped:      # facets stop where they reach the rim + facet_min; the outer field stays flat there
        panel = panel.union(profile(p, box, rim - 0.5).offset2D(-p.rim_w).extrude(0.5 + p.facet_min))
    d.update(k=k, tmax=tmax, top=top, crown=crown, crown_full=crown_full, capped=capped,
             arm_deg=math.degrees(math.atan(k * norm)), corner_deg=math.degrees(math.atan(2 * k / b)),
             max_slope_deg=math.degrees(math.atan(2 * k / b)))
    return panel, d


def _round_creases(panel, r):
    """Fillet every edge between two upward faces of the faceted panel (the creases): each layer contour then
    turns smoothly instead of printing a sharp ring at every crease (#14C)."""
    from OCP.TopExp import TopExp
    from OCP.TopTools import TopTools_IndexedDataMapOfShapeListOfShape
    from OCP.TopAbs import TopAbs_EDGE, TopAbs_FACE
    from OCP.TopoDS import TopoDS
    solid = panel.val()
    m = TopTools_IndexedDataMapOfShapeListOfShape()
    TopExp.MapShapesAndAncestors_s(solid.wrapped, TopAbs_EDGE, TopAbs_FACE, m)
    edges = []
    for i in range(1, m.Extent() + 1):
        fs = [cq.Face(TopoDS.Face_s(f)) for f in m.FindFromIndex(i)]
        if len(fs) == 2 and all(f.normalAt().y > 0.5 for f in fs):
            n0, n1 = fs[0].normalAt(), fs[1].normalAt()
            if n0.dot(n1) < 0.99999:
                edges.append(cq.Edge(m.FindKey(i)))
    if not edges:
        return panel
    return cq.Workplane().add(solid.fillet(r, edges))


def crown_coupon(p: P, x=(117.76, 146.76), z_half=10.5, height=4.5, label=None):
    """#14: a patch of the cover's crown (the owner's cover top at the USB end, with the LED land, a diamond corner
    and two creases), solid under the crown down to a flat base: the slicer fills it with sparse infill under the top
    shells, as the real crown. label: cut 0.5 into the base (the bed face), read from below."""
    body, info = outer_body(p)
    X0, X1, Y0, Y1, Z0, Z1 = info['box']
    top = info['facets']['top']
    zc = (Z0 + Z1) / 2
    y0 = top - height
    c = body.intersect(_box(x[0], x[1], y0, top + 1, zc - z_half, zc + z_half))
    if p.led_land:
        Ld = MEASURED['xiao_rgb_led']
        c = c.cut(_cyl(sum(Ld['x']) / 2, sum(Ld['z']) / 2, p.led_land / 2, Y1, Y1 + 10))
    if label:
        t = (cq.Workplane(cq.Plane(origin=((x[0] + x[1]) / 2, y0 - 0.01, zc), xDir=(0, 0, -1), normal=(0, -1, 0)))
             .text(label, 7.0, -0.51, halign='center', valign='center', kind='bold'))
        c = c.cut(t)
    return c, dict(y0=round(y0, 2), rim=Y1, top=round(top, 2), crown=round(top - Y1, 2), base=round(Y1 - y0, 2))


def crown_section(p: P, half=5.0, label=None, depth=1.2):
    """#16: a full-width slice (x = the diamond centre +- half) of the crown down to depth under the rim, solid; the
    slicer fills it like the real crown. label: cut 0.5 into the base (the bed face)."""
    body, info = outer_body(p)
    X0, X1, Y0, Y1, Z0, Z1 = info['box']
    top = info['facets']['top']
    xc = info['facets']['xc']
    zc = (Z0 + Z1) / 2
    y0 = Y1 - depth
    c = body.intersect(_box(xc - half, xc + half, y0, top + 1, Z0 - 1, Z1 + 1))
    if label:
        t = (cq.Workplane(cq.Plane(origin=(xc, y0 - 0.01, zc), xDir=(0, 0, -1), normal=(0, -1, 0)))
             .text(label, 6.0, -0.51, halign='center', valign='center', kind='bold'))
        c = c.cut(t)
    return c, dict(top=round(top, 2), crown=round(top - Y1, 2), base=depth, width=round(Z1 - Z0, 2), length=2 * half)


def dowel_sprue(p: P, n_long, n_short, pitch=5.0, labels=('L', 'S'), flats=None):
    """#18.1c: the dowels standing in two rows on one carrier bar (print frame, z up, the bar flat on the bed): the long
    row on one side, the short row on the other, each dowel's D-flat facing the bar and joined to it at its base by a
    0.6 x 0.4 tab (twist off: the nub stays on the flat and the end chamfer, off the working diameter). 'L' / 'S' raised
    on the bar. Returns (sprue, pusher): the pusher is a Ø12 x 40 handle with a Ø3.4 x pusher_cups[0] cup on its top, palm end
    down (a second entry: a second cup in the palm end, marked by a V ring groove 4.6 mm up from it)."""
    d = p.peg_d
    fl_l, fl_s = flats if flats is not None else (p.dowel_flat, p.dowel_flat)
    _, peg_l, _ = peg_coupon(clearances=(p.peg_clr,), d=d, peg_len=2 * p.peg_depth, flat=fl_l)
    _, peg_s, _ = peg_coupon(clearances=(p.peg_clr,), d=d, peg_len=p.short_len, flat=fl_s)
    bw, bt, gap = 3.0, 1.0, 1.0            # bar width, thickness, bar edge to the dowel's surface
    n_max = max(n_long, n_short)
    L = (n_max - 1) * pitch + d + 8.0
    sprue = _box(-6.0, L - 6.0, -bw / 2, bw / 2, 0, bt).translate((0, 0, 0))      # (x, y, z) here: _box is x, y, z
    yc = bw / 2 + gap + d / 2
    for label, peg, n, side, fl in ((labels[0], peg_l, n_long, 1, fl_l), (labels[1], peg_s, n_short, -1, fl_s)):
        pg = peg.rotate((0, 0, 0), (0, 0, 1), -90 if side > 0 else 90)       # the flat (+x) toward the bar
        for i in range(n):
            x = i * pitch
            sprue = sprue.union(pg.translate((x, side * yc, 0)))
            if fl > 0:
                y_bar, y_peg = side * bw / 2, side * (yc - d / 2 + fl + 0.2)   # the tab reaches 0.2 into the dowel's flat
                sprue = sprue.union(_box(x - 0.3, x + 0.3, *sorted((y_bar - side * 0.2, y_peg)), 0, 0.4))
            else:     # round (#18.1e): a first-layer tab into the 0.3 end chamfer only: the nub stays inside the Ø d envelope
                y_bar, y_peg = side * bw / 2, side * (yc - d / 2 + 0.3 + 0.2)
                sprue = sprue.union(_box(x - 0.3, x + 0.3, *sorted((y_bar - side * 0.2, y_peg)), 0, 0.2))
        t = (cq.Workplane(cq.Plane(origin=(-4.5, side * 0.0, bt - 0.01), xDir=(1, 0, 0), normal=(0, 0, 1)))
             .text(label, 2.4, 0.6, halign='center', valign='center', kind='bold'))
        sprue = sprue.union(t.translate((0 if side > 0 else L - 7.5, 0, 0)))
    pusher = cq.Workplane().circle(6.0).extrude(40.0).faces('>Z or <Z').edges().chamfer(0.6)
    cups = p.pusher_cups
    pusher = pusher.cut(cq.Workplane().circle(3.4 / 2).extrude(cups[0]).translate((0, 0, 40.0 - cups[0])))
    if len(cups) > 1:             # #18.1d: a second cup at the other end (its own protrusion), marked by a ring groove
        pusher = pusher.cut(cq.Workplane().circle(3.4 / 2).extrude(cups[1]).translate((0, 0, -0.01)))
        ring = (cq.Workplane('XZ').polyline([(5.4, 4.6), (6.6, 3.4), (6.6, 5.8)]).close()
                .revolve(360, (0, 0, 0), (0, 1, 0)))       # a V groove, 45 degree flanks (no overhang)
        pusher = pusher.cut(ring)
    return sprue, pusher


def fit_coupon(p: P, tips=(3.225, 3.175, 3.125), frame_holes=((2.44, 'neck'), (2.44, 'floor'), (3.24, 'neck')),
               face_depth=2.6, stop=1.2):
    """#18.1e fit coupon (lead, 2026-09-30): the face-hole and frame-hole geometry of p on two small strips, printed in
    their cover orientation (hole mouths on the bed), so the fit is felt in minutes before a 2 h cover. Face strip:
    ribbed holes (bore p.peg_ribs[0], 3 ribs p.peg_ribs[2] wide) with rib crests `tips` (18.1c's 3.225, +0.05, +0.10),
    `face_depth` deep, the lead / rib start of p. Frame strip: one block per `frame_holes` entry (depth, 'neck' | 'floor'),
    each exactly depth + `stop` tall like a cover post: a Ø(peg_d + 2 peg_clr) hole that ends on a `stop` thick ring round
    a p.frame_neck through neck, or on a `stop` thick solid floor. The n-th hole of a strip has n dimples on top.
    Returns (face_strip, frame_strip, info), print frame (z up, on the bed at z 0)."""
    bore, _, rw, nr = p.peg_ribs
    lead = p.rib_lead if p.rib_lead is not None else p.peg_lead
    pitch, W = 8.0, 8.0
    def dimples(body, i, x, top):
        for k in range(i + 1):
            body = body.cut(cq.Workplane().add(cq.Solid.makeCylinder(0.5, 1.0, cq.Vector(x - (i * 1.4) / 2 + k * 1.4, 2.9, top - 0.4),
                                                                      cq.Vector(0, 0, 1))))
        return body
    L = pitch * len(tips) + 2.0
    H = face_depth + 0.8
    face = cq.Workplane().box(L, W, H, centered=(False, True, False)).edges('|Z').fillet(1.0)
    for i, tip in enumerate(tips):
        x = 1.0 + pitch * (i + 0.5)
        hole = cq.Workplane().add(cq.Solid.makeCylinder(bore / 2, face_depth + 0.01, cq.Vector(x, -1.0, -0.01), cq.Vector(0, 0, 1)))
        for j in range(nr):
            a = 2 * math.pi * j / nr + math.pi / 2
            rib = cq.Workplane().box(bore / 2 + 0.3 - tip / 2, rw, face_depth - lead + 0.02, centered=(False, True, False)) \
                .translate((tip / 2, 0, lead)).rotate((0, 0, 0), (0, 0, 1), math.degrees(a)).translate((x, -1.0, 0))
            hole = hole.cut(rib)
        hole = hole.union(cq.Workplane().add(cq.Solid.makeCone(bore / 2 + lead, bore / 2, lead, cq.Vector(x, -1.0, -0.01), cq.Vector(0, 0, 1))))
        face = dimples(face.cut(hole), i, x, H)
    rf = p.peg_d / 2 + p.peg_clr
    fl = p.frame_lead if p.frame_lead is not None else p.peg_lead
    neck = p.frame_neck or 1.6
    frame = None
    for i, (dpt, kind) in enumerate(frame_holes):
        x = 1.0 + pitch * (i + 0.5)
        Hf = dpt + stop
        blk = cq.Workplane().box(pitch + (1.0 if i in (0, len(frame_holes) - 1) else 0.02), W, Hf, centered=(False, True, False)) \
            .translate((x - pitch / 2 - (1.0 if i == 0 else 0.01), 0, 0))
        hole = cq.Workplane().add(cq.Solid.makeCylinder(rf, dpt + 0.01, cq.Vector(x, -1.0, -0.01), cq.Vector(0, 0, 1)))
        hole = hole.union(cq.Workplane().add(cq.Solid.makeCone(rf + fl, rf, fl, cq.Vector(x, -1.0, -0.01), cq.Vector(0, 0, 1))))
        if kind == 'neck':
            hole = hole.union(cq.Workplane().add(cq.Solid.makeCylinder(neck / 2, Hf + 1, cq.Vector(x, -1.0, dpt - 0.01), cq.Vector(0, 0, 1))))
        blk = dimples(blk.cut(hole), i, x, Hf)
        frame = blk if frame is None else frame.union(blk)
    frame = frame.edges('|Z').fillet(0.8)
    return face, frame, dict(tips=tips, face_depth=face_depth, frame_holes=frame_holes, neck=neck, stop=stop,
                             size=(L, W, H, max(d_ for d_, _ in frame_holes) + stop))


def test_joint(p: P, L=16.0, W=10.0, skin=0.8, label='ТЕСТ'):
    """One exact face / frame joint of p (lead, 2026-09-30: felt before the cover is assembled), each piece labelled on
    its top. Frame block: a Ø(peg_d + 2 peg_clr) hole p.peg_frame_d deep, its frame_lead cone, the Ø frame_neck through
    neck on a >= 1.28 ring. Face tab: the ribbed hole (p.peg_ribs, rib_lead) p.peg_face_d deep under `skin`. Print frame:
    hole mouths on the bed (z 0). Returns (frame_block, face_tab)."""
    bore, tip, rw, nr = p.peg_ribs
    lead = p.rib_lead if p.rib_lead is not None else p.peg_lead
    fl = p.frame_lead if p.frame_lead is not None else p.peg_lead
    hx, hy = 4.5, -1.6
    def label_on(body, top):
        t = (cq.Workplane(cq.Plane(origin=(L / 2 + 1.0, 2.7, top + 0.01), xDir=(1, 0, 0), normal=(0, 0, 1)))
             .text(label, 2.8, -0.45, halign='center', valign='center', kind='bold'))
        return body.cut(t)            # engraved: an embossed label left a one-cell island on a letter
    D = p.peg_frame_d
    Hf = round(D + 1.28, 2)
    rf = p.peg_d / 2 + p.peg_clr
    fb = cq.Workplane().box(L, W, Hf, centered=(False, True, False)).edges('|Z').fillet(1.0)
    hole = cq.Workplane().add(cq.Solid.makeCylinder(rf, D + 0.01, cq.Vector(hx, hy, -0.01), cq.Vector(0, 0, 1)))
    hole = hole.union(cq.Workplane().add(cq.Solid.makeCone(rf + fl, rf, fl, cq.Vector(hx, hy, -0.01), cq.Vector(0, 0, 1))))
    hole = hole.union(cq.Workplane().add(cq.Solid.makeCylinder((p.frame_neck or 1.6) / 2, Hf + 1, cq.Vector(hx, hy, D - 0.01),
                                                                cq.Vector(0, 0, 1))))
    fb = label_on(fb.cut(hole), Hf)
    Fd = p.peg_face_d
    Ht = round(Fd + skin, 2)
    ft = cq.Workplane().box(L, W, Ht, centered=(False, True, False)).edges('|Z').fillet(1.0)
    hole = cq.Workplane().add(cq.Solid.makeCylinder(bore / 2, Fd + 0.01, cq.Vector(hx, hy, -0.01), cq.Vector(0, 0, 1)))
    for j in range(nr):
        a = 2 * math.pi * j / nr + math.pi / 2
        rib = cq.Workplane().box(bore / 2 + 0.3 - tip / 2, rw, Fd - lead + 0.02, centered=(False, True, False)) \
            .translate((tip / 2, 0, lead)).rotate((0, 0, 0), (0, 0, 1), math.degrees(a)).translate((hx, hy, 0))
        hole = hole.cut(rib)
    hole = hole.union(cq.Workplane().add(cq.Solid.makeCone(bore / 2 + lead, bore / 2, lead, cq.Vector(hx, hy, -0.01), cq.Vector(0, 0, 1))))
    ft = label_on(ft.cut(hole), Ht)
    return fb, ft


def size_sprue(p: P, d, letter, n=10, pitch=5.0):
    """18.2-B (owner, 2026-10-03): n round dowels of diameter d standing in one row beside a carrier bar, each joined to
    it by a first-layer tab into the end chamfer (twist off, the nub stays off the working diameter), and a 7 x 7 tab at
    the bar's start with the size letter raised on it (S / M / L): a loose dowel is swapped for the next size up."""
    _, peg, _ = peg_coupon(clearances=(p.peg_clr,), d=d, peg_len=2 * p.peg_depth, flat=0.0)
    bw, bt, gap = 3.0, 1.0, 1.0
    L = (n - 1) * pitch + d / 2 + 1.0
    spr = _box(-1.0, L, -bw / 2, bw / 2, 0, bt).union(_box(-9.0, -1.0, -3.5, 3.5, 0, bt))
    yc = bw / 2 + gap + d / 2
    for i in range(n):
        x = i * pitch + d / 2
        spr = spr.union(peg.translate((x, yc, 0)))
        spr = spr.union(_box(x - 0.3, x + 0.3, bw / 2 - 0.2, yc - d / 2 + 0.5, 0, 0.2))
    t = (cq.Workplane(cq.Plane(origin=(-5.0, 0.0, bt - 0.01), xDir=(1, 0, 0), normal=(0, 0, 1)))
         .text(letter, 4.5, 0.6, halign='center', valign='center', kind='bold'))
    return spr.union(t)


def sml_rack(p: P, pitch_y=12.0):
    """18.2-B: the size sprues (p.sml_sprues) as one rack, one row per size, their label tabs joined by a spine."""
    if not p.sml_sprues:
        raise ValueError('sml_rack needs at least one (letter, diameter) in p.sml_sprues')
    rack = None
    n = len(p.sml_sprues)
    for i, (lt, dd) in enumerate(p.sml_sprues):
        row = size_sprue(p, dd, lt).translate((0, i * pitch_y, 0))
        rack = row if rack is None else rack.union(row)
    return rack.union(_box(-9.0, -7.0, -3.5, (n - 1) * pitch_y + 3.5, 0, 1.0))


def peg_coupon(number=15, clearances=(0.05, 0.10, 0.15), d=3.0, depth=3.0, peg_len=5.0, flat=0.3):
    """#15: the 10.3a/b dowel press fit. A block (print frame: z up) with a blind hole per clearance (diameter d + 2 c,
    0.3 lead-in at the mouth), labelled on top, and loose pegs d x peg_len printed standing (0.3 chamfers both ends, a
    0.3 D-flat along the length to vent the blind hole)."""
    n = len(clearances)
    pitch, W, H = 10.0, 12.0, depth + 1.2
    L = pitch * n + 6.0
    block = _box(0, L, -W / 2, W / 2, 0, H).edges('|Z').fillet(1.0)
    block = block.faces('<Z').edges().chamfer(0.3)
    txts = []
    for i, c in enumerate(clearances):
        x = 6.0 + pitch * i + 2.0
        r = d / 2 + c
        hole = cq.Workplane().add(cq.Solid.makeCylinder(r, depth + 0.01, cq.Vector(x, -1.5, H - depth), cq.Vector(0, 0, 1)))
        hole = hole.union(cq.Workplane().add(cq.Solid.makeCone(r, r + 0.3, 0.3, cq.Vector(x, -1.5, H - 0.3), cq.Vector(0, 0, 1))))
        block = block.cut(hole)
        txts.append(cq.Workplane(cq.Plane(origin=(x, 3.4, H - 0.5), xDir=(1, 0, 0), normal=(0, 0, 1)))
                    .text(f'.{round(c * 100):02d}', 3.2, 0.51, halign='center', valign='center', kind='bold'))
    txts.append(cq.Workplane(cq.Plane(origin=(3.0, 0, H - 0.5), xDir=(0, 1, 0), normal=(0, 0, 1)))
                .text(str(number), 3.5, 0.51, halign='center', valign='center', kind='bold'))
    for t in txts:
        block = block.cut(t)
    peg = (cq.Workplane().add(cq.Solid.makeCylinder(d / 2, peg_len, cq.Vector(0, 0, 0), cq.Vector(0, 0, 1)))
           .faces('>Z or <Z').edges().chamfer(0.3))
    if flat > 0:
        peg = peg.cut(_box(d / 2 - flat, d, -d, d, -1, peg_len + 1))    # a D-flat the whole length: the blind hole's air vents
    return block, peg, dict(holes=[round(d + 2 * c, 2) for c in clearances], depth=depth, peg=(d, peg_len), block=(L, W, H))


def loop_lug(p: P, box):
    X0, X1, Y0, Y1, Z0, Z1 = box
    zc, yc = (Z0 + Z1) / 2, (Y0 + Y1) / 2
    hr = p.loop_hole / 2
    hx = X1 + p.loop_gap + hr
    R = hr + p.loop_web
    w = max(p.loop_w, 2 * R)
    base = X1 - 6.0
    lug = (cq.Workplane().box(hx - base, p.loop_t, w).translate(((hx + base) / 2, yc, zc))
           .union(cq.Workplane().cylinder(p.loop_t, w / 2, direct=(0, 1, 0)).translate((hx, yc, zc))))
    lug = lug.cut(cq.Workplane().cylinder(p.loop_t * 3, hr, direct=(0, 1, 0)).translate((hx, yc, zc)))
    try:                # the bed-side edges stay square when the lug has fins (a fillet there prints as a 90 deg lip)
        lug = lug.faces('>Y' if p.loop_gusset else '>Y or <Y').edges('not %LINE').fillet(0.6)
    except Exception:
        pass
    return lug, dict(hole_x=hx, hole_r=hr, web=R - hr, end_x=hx + w / 2, t=p.loop_t,
                     section_mm2=p.loop_web * p.loop_t)


def outer_body(p: P):
    box = outer_box(p)
    X0, X1, Y0, Y1, Z0, Z1 = box
    body = profile(p, box, Y0).extrude(Y1 - Y0)
    body = (body.faces('>Y').edges().chamfer(p.front_chamfer) if p.flat_top
            else body.faces('>Y').edges().fillet(p.fillet_front))
    body = (body.faces('<Y').edges().chamfer(p.back_chamfer) if p.back_chamfer > 0
            else body.faces('<Y').edges().fillet(p.fillet_back))
    panel, fac = facet_panel(p, box)
    body = body.union(panel)
    return body, dict(box=box, facets=fac, corner_extent=corner_extent(p, Z1 - Z0),
                      corner_extent_bottom=corner_extent(p, Z1 - Z0, 'bottom'))


def _wall_loft(p: P, box, ya, da, yb, db):
    """Ruled loft between the outline offset inward by da at y = ya and by db at y = yb (a 45 degree skirt when
    |da - db| = |yb - ya|)."""
    wa = profile(p, box, ya).offset2D(-da).vals()[0]
    wb = profile(p, box, yb).offset2D(-db).vals()[0]
    return cq.Workplane().add(cq.Solid.makeLoft([wa, wb], ruled=True))


def tab_geometry(p: P):
    """Numbers of the D1 flex tab in the +z wall."""
    M = MEASURED['button_d1']
    x0, x1, y0, y1, z0, z1 = packing(p)['cavity']
    xc = sum(M['x']) / 2; yc = sum(M['y']) / 2
    hx1 = xc + p.tab_free
    hx0 = hx1 - p.tab_len
    zin = z1                          # inner face of the +z wall
    zout = z1 + p.wall
    ztongue = zout - min(p.tab_t, p.wall)   # tongue inner face (thinned from inside when the wall is thicker)
    ym = yc + p.tab_dy
    # the slit above the tongue: vertical gap sv (its width across the 45 degree roof is tab_slit), rising by the
    # tongue thickness from the outer face to the tongue's inner face (roof = tan(tab_roof) per mm inward)
    roof = math.tan(math.radians(p.tab_roof)) if p.tab_roof > 0 else 0.0
    sv = p.tab_slit / math.cos(math.radians(p.tab_roof)) if p.tab_roof > 0 else p.tab_slit
    return dict(xc=xc, yc=yc, hx0=hx0, hx1=hx1, ya=ym - p.tab_h / 2, yb=ym + p.tab_h / 2,
                zin=zin, zout=zout, ztongue=ztongue, nub_z0=M['z_max'] + p.nub_gap, roof=roof, sv=sv,
                top_slit_max=ym + p.tab_h / 2 + sv + roof * (zout - ztongue))


def tab_solids(p: P, g, z_in=None):
    """(tongue, grown): the tongue's envelope and the same grown by tab_slit (so grown - tongue = the slits).
    Slit above the tongue (print Z): a 45 degree roof, not a flat 14 mm bridge. At the outer face the gap is
    yb..yb + sv (sv = tab_slit / cos 45); it rises going inward, so the wall above grows over it at 45 degrees and the
    tongue's top edge leans back on its inner side. The free end's top corner is cut at 45 degrees (tab_end_chamfer)
    and the slit follows it, so the wall past the free end overhangs at 45 degrees too."""
    zo, yb, ya, sv, rf, s, ce = g['zout'], g['yb'], g['ya'], g['sv'], g['roof'], p.tab_slit, p.tab_end_chamfer
    zi = (g['ztongue'] - 0.3) if z_in is None else z_in
    ze = zo + 1.5

    def env(dy0, dy1, x1):
        return _prism_yz([(ya - dy0, ze), (yb + dy1, ze), (yb + dy1, zo), (yb + dy1 + rf * (zo - zi), zi),
                          (ya - dy0, zi)], g['hx0'], x1)
    tongue, grown = env(0.0, 0.0, g['hx1']), env(s, sv, g['hx1'] + s)
    if p.tab_square:              # #9 / audit: a flat top edge, so the tongue does not end in a one-line island
        tongue = _prism_yz([(ya, ze), (yb, ze), (yb, zi), (ya, zi)], g['hx0'], g['hx1'])
        ce = 0
    if ce > 0:
        a = g['hx1'] + yb - ce                   # chamfer plane x + y = a
        pl = cq.Plane(origin=(0, 0, zi - 1), xDir=(1, 0, 0), normal=(0, 0, 1))
        top = yb + sv + rf * (zo - zi) + 2

        def past(c):                             # everything with x + y > c near the free end
            return cq.Workplane(pl).polyline([(c - top, top), (g['hx1'] + 5, top), (g['hx1'] + 5, c - g['hx1'] - 5),
                                              ]).close().extrude(ze - zi + 2)
        tongue = tongue.cut(past(a))
        grown = grown.cut(past(a + s * math.sqrt(2)))
    return tongue, grown


def nub_span(p: P, g):
    """(y0, y1) of the nub: nub_h around the actuator centre, or (nub_h 0) a rib over the tongue height."""
    if p.nub_h > 0:
        return g['yc'] - p.nub_h / 2, g['yc'] + p.nub_h / 2
    return g['ya'] + p.nub_rib_margin, g['yb'] - p.nub_rib_margin


def tab_mechanics(p: P = None):
    """Cantilever flexure: hinge at hx0, load at the nub (D1 centre)."""
    p = p or P()
    g = tab_geometry(p)
    M = MEASURED['button_d1']
    L = g['xc'] - g['hx0']
    I = p.tab_h * p.tab_t ** 3 / 12
    E = DATASHEET['pla_E_xy']
    k = 3 * E * I / L ** 3                         # N/mm
    protrusion = M['z_max'] - M['body_z']          # 1.00 mm of actuator out of the switch body
    typ_travel = 0.25                              # typical side tact switch travel; D1 part number unknown
    stroke = p.nub_gap + typ_travel
    bottom = p.nub_gap + protrusion                # nub reaches the switch body: hard stop
    strain = lambda d: 3 * d * p.tab_t / (2 * L ** 2) * 100
    return dict(L=L, I=I, E=E, k=k, gap=p.nub_gap, protrusion=protrusion, typ_travel=typ_travel,
                stroke=stroke, flex_force=k * stroke, strain_stroke=strain(stroke),
                max_defl=bottom, strain_max=strain(bottom), elong_break=DATASHEET['pla_elong_xy'],
                nub_len=g['ztongue'] - g['nub_z0'], wall=p.wall, tab_t=p.tab_t, slit=p.tab_slit, tab_len=p.tab_len,
                roof_deg=p.tab_roof, end_chamfer=p.tab_end_chamfer)


def _cyl(x, z, r, y0, y1):
    return cq.Workplane().cylinder(y1 - y0, r, direct=(0, 1, 0)).translate((x, (y0 + y1) / 2, z))


def _hexprism(x, z, af, y0, y1):
    return (cq.Workplane(cq.Plane(origin=(x, y0, z), xDir=(1, 0, 0), normal=(0, 1, 0)))
            .polygon(6, af / math.cos(math.pi / 6)).extrude(y1 - y0))


def _box(x0, x1, y0, y1, z0, z1):
    return cq.Workplane().box(x1 - x0, y1 - y0, z1 - z0).translate(((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2))


def fastening(p: P, box):
    """Screw stack per board hole: head seat, tip, nut.
    'front': screws enter from the front, nuts in the back. 'back' (A): countersunk heads flush with the
    back face, nuts in the cover pillars. 'snap' (B): no screws."""
    Y0 = box[2]
    if p.closure == 'back':
        tip = Y0 + p.back_screw_len
        n1 = tip - p.nut_tip_past
        return dict(closure='back', seat=Y0, tip=tip, nut=(n1 - p.nut_m, n1), holes=MEASURED['holes'])
    if p.closure == 'snap':
        return dict(closure='snap', seat=None, tip=None, nut=None, holes=MEASURED['holes'])
    tip = Y0 + p.screw_tip
    seat = tip + p.screw_len
    return dict(closure='front', seat=seat, tip=tip, nut=(Y0, Y0 + p.nut_m), holes=MEASURED['holes'])


def _cone(x, z, d0, d1, y0, y1):
    """Cone along +y from diameter d0 at y0 to d1 at y1."""
    return cq.Workplane().add(cq.Solid.makeCone(d0 / 2, d1 / 2, y1 - y0, cq.Vector(x, y0, z), cq.Vector(0, 1, 0)))


def _prism_yz(pts, x0, x1):
    """Prism along x over a polygon given as (y, z) points."""
    pl = cq.Plane(origin=(x0, 0, 0), xDir=(0, 1, 0), normal=(1, 0, 0))     # local x = y, local y = z
    return cq.Workplane(pl).polyline(pts).close().extrude(x1 - x0)


def latches(p: P):
    """B: one dict per latch. s = +1 on the +z wall, -1 on the -z wall; the tongue's hinge points to the
    middle of the straight side, the free end (with the groove) to the end of the case."""
    x0, x1, y0, y1, z0, z1 = packing(p)['cavity']
    mid = sum(p.latch_x) / 2
    out = []
    tt = max(p.latch_t, p.wall)
    for s, zw in ((1, z1), (-1, z0)):
        for xb in p.latch_x:
            hd = 1 if xb < mid else -1
            zout = zw + s * p.wall
            out.append(dict(s=s, zin=zout - s * tt, zwall=zw, zout=zout, xb=xb, hinge=xb + hd * p.latch_len,
                            free=xb - hd * (p.latch_bump_w / 2 + 1.0), hd=hd))
    return out


def snap_features(p: P, tray, cover, split, box, usb):
    """B: per latch, an L-shaped slit frees a tongue in the long wall (hinge toward the middle, free end toward
    the case end, top edge = wall top under the cover rim); a groove in the tongue's inner face near its free
    end; on the cover, a catch block under the front plate with a bump that snaps into the groove.
    Both 45 degree retention faces and the 30 degree lead-in print without support. Plus a pry notch in the
    bottom end beside USB-C."""
    X0 = box[0]
    x0 = packing(p)['cavity'][0]
    sl, e, c = p.tab_slit, p.latch_e, p.print_clr
    tl, tr = math.tan(math.radians(p.latch_lead_deg)), math.tan(math.radians(p.latch_ret_deg))
    dz = c + e
    dl, dr, flat = dz / tl, dz / tr, 0.4
    yb0 = split - p.latch_catch_h + 0.3
    yr0 = yb0 + dl + flat
    for L in latches(p):
        s, zin, zout, xb, hd = L['s'], L['zin'], L['zout'], L['xb'], L['hd']
        if abs(zin - L['zwall']) > 1e-6:
            # a wall thinner than latch_t: a pad inside the wall over the tongue and its slits, and the cover's lip
            # (which runs just inside the wall) notched over it; the catch block below takes the lip's place there
            xa_, xb_ = sorted((L['free'] - hd * (sl + 1.5), L['hinge'] + hd * 1.5))
            pa, pb = sorted((L['zwall'] + s * 0.1, zin))
            tray = tray.union(_box(xa_, xb_, p.latch_y0 - sl - 0.6, split, pa, pb))
            ca_, cb_ = sorted((L['zwall'] + s * 0.1, zin - s * c))
            cover = cover.cut(_box(xa_ - 0.3, xb_ + 0.3, split - p.lip_h - 0.5, split + 0.01, ca_, cb_))
        za, zb = sorted((zin - s * 1.0, zout + s * 1.5))
        fa, fb = sorted((L['free'], L['free'] - hd * sl))
        tray = tray.cut(_box(fa, fb, p.latch_y0 - sl, split + 0.1, za, zb))                    # end slit
        ba, bb = sorted((L['free'] - hd * sl, L['hinge']))
        tray = tray.cut(_box(ba, bb, p.latch_y0 - sl, p.latch_y0, za, zb))                     # bottom slit
        hw = p.latch_bump_w / 2
        # lead-in chamfer on the tongue's inner top edge
        tray = tray.cut(_prism_yz([(split + 0.05, zin - s * 0.05), (split - 0.8, zin - s * 0.05),
                                   (split + 0.05, zin + s * 0.8)], xb - hw - 0.3, xb + hw + 0.3))
        # groove: the bump's shape, 0.15 lower on the lead side, 0.05 higher on the retention side, 0.1 deeper
        zf, zt = zin - s * c, zin + s * e
        zt2 = zt + s * 0.1
        tray = tray.cut(_prism_yz([(yb0 - 0.15, zf), (yb0 - 0.15 + (dz + 0.1) / tl, zt2),
                                   (yr0 + 0.05 - 0.1 / tr, zt2), (yr0 + 0.05 + dr, zf)], xb - hw - 0.2, xb + hw + 0.2))
        # catch block + bump on the cover
        ca, cb = sorted((zin - s * (c + p.latch_catch_t), zf))
        cover = cover.union(_box(xb - hw - 0.5, xb + hw + 0.5, split - p.latch_catch_h, split + 0.5, ca, cb))
        cover = cover.union(_prism_yz([(yb0, zf - s * 0.3), (yb0, zf), (yb0 + dl, zt), (yr0, zt), (yr0 + dr, zf),
                                       (yr0 + dr, zf - s * 0.3)], xb - hw, xb + hw))
    # pry notch in the bottom end, beside the USB-C opening
    w, d, pz0 = p.pry_notch
    uz, uw = usb
    assert pz0 > uz + uw / 2 + p.usb_chamfer + 0.5, 'pry notch runs into the USB-C lead-in'
    tray = tray.cut(_box(X0 - 1.0, x0 + 3.0, split - d, split + 0.1, pz0, pz0 + w))
    return tray, cover


def fit_check(p: P, info):
    """Board-referenced openings against the real parts (MEASURED), as margins in mm; negative = the part is covered.
    usb: receptacle shell at its mouth inside the window (4 sides); switch: the lever over its full travel inside the
    slot; d1: the nub covers the actuator in y, and the rest gap; led: the LED die inside the skin spot; tab_wall: solid
    wall between the button slits and the back fillet / the parting line."""
    M = MEASURED
    out = {}
    uw, uh, uy, uz = usb_window_rect(p)
    sh = M['usb_c_shell']
    out['usb'] = dict(y_low=round(sh['y'][0] - (uy - uh / 2), 3), y_high=round(uy + uh / 2 - sh['y'][1], 3),
                      y_low_at_min_lift=round(sh['y'][0] - XIAO_DY_TOL - (uy - uh / 2), 3),
                      cover_over_at_max_lift=round(packing(p)['cavity'][3] - (M['xiao_top_y'] + XIAO_DY_UP), 3),
                      z_low=round(sh['z'][0] - (uz - uw / 2), 3), z_high=round(uz + uw / 2 - sh['z'][1], 3))
    sl, ss = M['switch_lever'], info['switch_slot']
    out['switch'] = dict(x_on=round(sl['x'][0] - sl['travel'] - ss['x'][0], 3), x_off=round(ss['x'][1] - sl['x'][1], 3),
                         tip_under_outer=round(sl['z_min'] - info['box'][4] - (p.switch_scoop or 0.0), 3),
                         y_low=round(sl['y'][0] - ss['y'][0], 3), y_high=round(ss['y'][1] - sl['y'][1], 3))
    d1, g = M['button_d1'], info['tab']
    ny0, ny1 = nub_span(p, g)
    nx0, nx1 = g['xc'] - p.nub_w / 2, g['xc'] + p.nub_w / 2
    out['d1'] = dict(nub_covers_y_low=round(d1['y'][0] - ny0, 3), nub_covers_y_high=round(ny1 - d1['y'][1], 3),
                     nub_on_actuator_x=round(min(nx0 - d1['x'][0], d1['x'][1] - nx1), 3),
                     rest_gap=round(g['nub_z0'] - d1['z_max'], 3))
    L = M['xiao_rgb_led']
    lx, lz = info['led']['x'], info['led']['z']
    far = max(math.hypot(x - lx, z - lz) for x in L['x'] for z in L['z'])
    out['led'] = dict(die_inside_spot=round(p.led_d / 2 - far, 3))
    Y0, split = info['box'][2], info['split']
    out['tab_wall'] = dict(below=round(g['ya'] - p.tab_slit - (Y0 + (p.back_chamfer or p.fillet_back)), 3),
                           above=round(split - g['top_slit_max'], 3))
    worst = min(v for d in out.values() for v in d.values())
    out['ok'] = bool(min(min(out['usb'].values()), min(out['switch'].values()), out['d1']['nub_covers_y_low'],
                         out['d1']['nub_covers_y_high'], out['led']['die_inside_spot']) >= 0.2
                     and min(out['tab_wall'].values()) >= 1.5 and out['d1']['rest_gap'] > 0)
    out['worst'] = round(worst, 3)
    return out


def floor_deflection(p: P = None, force=20.0, nu=0.35):
    """The bay floor under a hand squeeze: a rectangular plate (bay length b along x at the case centre, cavity width a),
    force spread over it. Timoshenko plate tables for a/b = 2: w = alpha q b^4 / D, alpha 0.01013 simply supported
    (upper bound) and 0.00254 clamped (lower bound); stress sigma = beta q b^2 / t^2, beta 0.6102 (simply supported)."""
    p = p or P()
    box = outer_box(p)
    x0, x1, y0, y1, z0, z1 = packing(p)['cavity']
    b = (box[1] - p.wall) - packing(p)['parts']['shelf'][0]
    a = z1 - z0
    q = force / (a * b)
    D = DATASHEET['pla_E_xy'] * p.floor ** 3 / (12 * (1 - nu ** 2))
    return dict(floor=p.floor, force_N=force, bay_b=round(b, 1), width_a=round(a, 1), q_MPa=round(q, 4),
                w_simply_supported=round(0.01013 * q * b ** 4 / D, 3), w_clamped=round(0.00254 * q * b ** 4 / D, 3),
                stress_MPa=round(0.6102 * q * b ** 2 / p.floor ** 2, 1), swell_gap=p.lipo_swell)


def snap_mechanics(p: P = None):
    """B: tongue = cantilever of the wall (length L hinge to bump, height w, thickness t = wall), bent in the
    print plane (along the extrusion lines). Bayer snap-fit formulas: strain = 1.5 e t / L^2,
    deflection force P = w t^2 E strain / (6 L), axial force W = P (mu + tan a) / (1 - mu tan a)."""
    p = p or P()
    E = DATASHEET['pla_E_xy']
    L, t, e = p.latch_len, max(p.latch_t, p.wall), p.latch_e
    w = packing(p)['cavity'][3] - p.latch_y0
    eps = 1.5 * e * t / L ** 2
    Pd = w * t ** 2 * E * eps / (6 * L)

    def W(deg, mu):
        ta = math.tan(math.radians(deg))
        return Pd * (mu + ta) / (1 - mu * ta)
    n = 2 * len(p.latch_x)
    return dict(L=L, t=t, w=w, e=e, E=E, strain_pct=eps * 100, deflect_N=Pd,
                push_on_N_each=W(p.latch_lead_deg, p.latch_mu), pull_off_N_each=W(p.latch_ret_deg, p.latch_mu),
                push_on_N=n * W(p.latch_lead_deg, p.latch_mu), pull_off_N=n * W(p.latch_ret_deg, p.latch_mu),
                pull_off_N_mu_0_25=n * W(p.latch_ret_deg, 0.25), pull_off_N_mu_0_45=n * W(p.latch_ret_deg, 0.45),
                latches=n, elong_break_pct=DATASHEET['pla_elong_xy'])


def _rrect(x0, x1, z0, z1, y, r):
    """Rounded rectangle wire in the plane y: x0..x1, z0..z1, corner radius r."""
    r = max(0.05, min(r, (x1 - x0) / 2 - 0.05, (z1 - z0) / 2 - 0.05))
    w = cq.Wire.makePolygon([cq.Vector(x0 + r, y, z0 + r), cq.Vector(x1 - r, y, z0 + r),
                             cq.Vector(x1 - r, y, z1 - r), cq.Vector(x0 + r, y, z1 - r)], close=True)
    return w.offset2D(r, 'arc')[0]


def _loft(sections):
    """Ruled loft through [(y, (x0, x1, z0, z1), r)] sections, bottom to top."""
    return cq.Workplane().add(cq.Solid.makeLoft([_rrect(*rc, y, r) for y, rc, r in sections], ruled=True))


def _grow(rc, d):
    return (rc[0] - d, rc[1] + d, rc[2] - d, rc[3] + d)


def window_geometry(p: P, box, fac):
    """OLED window (case v2). The aperture is the OLED glass minus window_inset, cut through a bezel that hangs
    window_gap above the glass. A well rises from the bezel to the front: its floor is a window_ledge ring around
    the aperture, its walls open at window_deg; the -x wall stays vertical up to window_kink_y, past the XIAO.
    On a faceted front the well opens into a flat field (the facets stop at its 45 degree edge); on a flat front
    it opens straight into the front. Returns the numbers and the sections of the well (inner) and its wall (outer)."""
    g = MEASURED['oled']
    ap = (g['x'][0] + p.window_inset, g['x'][1] - p.window_inset,
          g['glass_z'][0] + p.window_inset, g['glass_z'][1] - p.window_inset)
    yg = g['y_top']
    yb = yg + p.window_gap
    ya = yb + p.window_bezel
    Y1 = box[3]
    field = p.window_field > 0 and fac['crown'] > 0.01
    yf = Y1 + p.window_field_h if field else fac['top']
    ch = p.window_top_chamfer
    fl = _grow(ap, p.window_ledge)
    tan = [math.tan(math.radians(v)) for v in p.window_deg]
    tk = math.tan(math.radians(p.window_kink_deg))

    def at(y, grow=0.0):
        h = max(0.0, y - ya)
        xm = h * tan[0] + max(0.0, y - p.window_kink_y) * tk
        return (fl[0] - xm - grow, fl[1] + h * tan[1] + grow, fl[2] - h * tan[2] - grow, fl[3] + h * tan[3] + grow)

    def r_at(y, grow=0.0):
        return p.window_r + p.window_ledge + max(0.0, y - ya) * min(tan[1:]) + grow
    assert p.window in ('open', 'lip'), f'window must be none, open or lip, not {p.window!r}'
    assert ya < yf - ch, 'the bezel top must sit below the chamfer at the front: lower window_bezel or window_top_chamfer'
    ys = [ya] + ([p.window_kink_y] if ya < p.window_kink_y < yf - ch else []) + [yf - ch]
    inner = [(y, at(y), r_at(y)) for y in ys]
    inner += [(yf, at(yf - ch, ch), r_at(yf - ch, ch)), (fac['top'] + 2.0, at(yf - ch, ch), r_at(yf - ch, ch))]
    y1 = packing(p)['cavity'][3]
    yo = y1 + min(0.3, p.cover / 2)                # the wall runs into the front plate
    w = p.window_wall
    outer = [(yb, at(ya, w), r_at(ya, w))] + [(y, at(y, w), r_at(y, w)) for y in ys if y < yo] + [(yo, at(yo, w), r_at(yo, w))]
    top = at(yf - ch, ch)
    # the whole viewing area stays in sight up to this tilt toward each side (-x, +x, -z, +z): the ray from the
    # VA edge must pass the aperture edge (at ya - chamfer) and every well section on that side
    va = DATASHEET['oled_va']
    vx, vz = (ap[0] + ap[1]) / 2, (ap[2] + ap[3]) / 2
    vr = (vx - va[0] / 2, vx + va[0] / 2, vz - va[1] / 2, vz + va[1] / 2)
    cone = []
    for i, sgn in enumerate((-1, 1, -1, 1)):
        lim = [(ya - p.window_chamfer, sgn * (ap[i] - vr[i]))] + [(y, sgn * (rc[i] - vr[i])) for y, rc, _ in inner[:-1]]
        cone.append(round(min(math.degrees(math.atan2(dx, y - yg)) for y, dx in lim), 1))
    return dict(aperture=ap, glass_top=yg, bezel=(yb, ya), floor=fl, front=yf, field=field, inner=inner, outer=outer,
                top=top, depth=yf - ya, cone_deg=dict(zip(('-x', '+x', '-z', '+z'), cone)),
                viewing_area=vr, active_area=(vx - DATASHEET['oled_aa'][0] / 2, vx + DATASHEET['oled_aa'][0] / 2,
                                              vz - DATASHEET['oled_aa'][1] / 2, vz + DATASHEET['oled_aa'][1] / 2))


def window_cut(p: P, cover, box, fac):
    """Adds the well to the cover and cuts the aperture (and, 'lip', the recess for a clear sheet)."""
    wg = window_geometry(p, box, fac)
    ap, (yb, ya) = wg['aperture'], wg['bezel']
    cover = cover.union(_loft(wg['outer']))
    if wg['field']:
        yf, top = wg['front'], fac['top'] + 2.0
        m = p.window_field
        fr = wg['inner'][-1][2] + m
        cover = cover.cut(_loft([(yf, _grow(wg['top'], m), fr), (top, _grow(wg['top'], m + top - yf), fr + top - yf)]))
    cover = cover.cut(_loft(wg['inner']))
    c = p.window_chamfer
    cover = cover.cut(_loft([(yb - 1.0, ap, p.window_r), (ya - c, ap, p.window_r), (ya + 0.01, _grow(ap, c + 0.01), p.window_r + c)]))
    insert = None
    if p.window == 'lip':
        lw, ld = p.window_lip
        cover = cover.cut(_loft([(yb - 1.0, _grow(ap, lw), p.window_r + lw), (yb + ld, _grow(ap, lw), p.window_r + lw)]))
        # a clear insert for the recess: 0.1 mm smaller per side, 0.1 mm thinner than the recess
        insert = _loft([(yb, _grow(ap, lw - 0.1), p.window_r + lw - 0.1), (yb + ld - 0.1, _grow(ap, lw - 0.1), p.window_r + lw - 0.1)])
    wg['insert_size'] = None if insert is None else (round(ap[1] - ap[0] + 2 * (p.window_lip[0] - 0.1), 2),
                                                     round(ap[3] - ap[2] + 2 * (p.window_lip[0] - 0.1), 2),
                                                     round(p.window_lip[1] - 0.1, 2), round(p.window_r + p.window_lip[0] - 0.1, 2))
    return cover, insert, wg


def switch_slot(p: P):
    s = MEASURED['switch_lever']
    x0 = s['x'][0] - s['travel'] - p.switch_slot_clr
    x1 = s['x'][1] + p.switch_slot_clr + (p.switch_slot_off or 0.0)
    return dict(x=(x0, x1), y=(s['y'][0] - p.switch_slot_clr_y, s['y'][1] + p.switch_slot_clr_y))


def parts(p: P = None):
    """Printed parts, split into one body per filament so a multi-colour slicer can assign them:
       tray (+ loop, pad), cover (zone 3) + zone1 + zone2 + led, shelf. Returns (bodies, info)."""
    p = p or P()
    M = MEASURED
    body, info = outer_body(p)
    box = info['box']
    X0, X1, Y0, Y1, Z0, Z1 = box
    x0, x1, y0, y1, z0, z1 = packing(p)['cavity']
    k = packing(p)
    d = info['facets']
    cavity = profile(p, box, y0).offset2D(-p.wall).extrude(y1 - y0)
    hollow = body.cut(cavity)
    split = y1
    above = _box(X0 - 20, X1 + 40, split, split + 60, -200, 200)
    cover = hollow.intersect(above)
    ceiling_top = split
    if p.crown_shell > 0:         # the crown as a shell: the space under it (crown_shell below its face) is hollow
        panel_, _ = facet_panel(p, box)
        void = panel_.translate((0, -p.crown_shell, 0)).intersect(_box(X0 - 5, X1 + 5, split, Y1 + 20, -200, 200))
        cover = cover.cut(void)
        ceiling_top = d['top'] - p.crown_shell
    tray = hollow.cut(above)
    if p.floor_chamfer > 0:       # 45 degree fill along the inside floor corner
        c_ = p.floor_chamfer
        tray = tray.union(cavity.intersect(_box(X0 - 5, X1 + 5, y0 - 0.5, y0 + c_, -200, 200))
                          .cut(_wall_loft(p, box, y0, p.wall + c_, y0 + c_, p.wall).union(
                              profile(p, box, y0 + c_ - 0.01).offset2D(-p.wall).extrude(1.0))))
    ew = end_wall(p)
    if ew < p.wall:               # the flat USB-C end wall thinner than the walls, from over the floor fill to under the band
        yt = split - p.rim_band_h - p.rim_band
        tray = tray.cut(profile(p, box, y0 + p.floor_chamfer).offset2D(-ew).extrude(yt - y0 - p.floor_chamfer)
                        .intersect(_box(X0 - 1, X0 + p.wall + 0.3, y0 - 1, yt + 1, -200, 200)))
    if p.rim_band > 0:            # a band inside the top of the tray wall, 45 degrees underneath
        b_, h_ = p.rim_band, p.rim_band_h
        tray = tray.union(cavity.intersect(_box(X0 - 5, X1 + 5, split - h_ - b_, split, -200, 200))
                          .cut(_wall_loft(p, box, split - h_ - b_, p.wall, split - h_, p.wall + b_).union(
                              profile(p, box, split - h_ - b_ - 1.0).offset2D(-p.wall).extrude(1.0 + 0.001)).union(
                              profile(p, box, split - h_ - 0.001).offset2D(-p.wall - b_).extrude(h_ + 1.0))))
    f = fastening(p, box)

    # ---- tray: standoffs under the PCB; screw holes with nut pockets (front) or countersinks (back),
    # or locating pins into the board holes (snap)
    pcb_bot = M['pcb_y'][0]
    for hx, hz in f['holes']:
        tray = tray.union(_cyl(hx, hz, p.standoff_d / 2, y0 - 0.5, pcb_bot))
        if p.closure == 'snap':
            tray = tray.union(_cyl(hx, hz, p.pin_d / 2, pcb_bot - 0.01, M['pcb_y'][1] - 0.1))
            continue
        if p.closure == 'back' and p.floor_boss > p.floor:     # the floor keeps floor_boss around the countersink
            tray = tray.union(_cyl(hx, hz, p.floor_boss_d / 2, y0 - 0.5, Y0 + p.floor_boss).intersect(cavity))
        tray = tray.cut(_cyl(hx, hz, p.screw_clear / 2, Y0 - 1, pcb_bot + 0.1))
        if p.closure == 'back':
            h = (p.csk_d - p.screw_clear) / 2
            tray = tray.cut(_cone(hx, hz, p.csk_d, p.screw_clear, Y0, Y0 + h)).cut(_cyl(hx, hz, p.csk_d / 2, Y0 - 1, Y0 + 0.01))
        else:
            tray = tray.cut(_hexprism(hx, hz, p.nut_af + p.print_clr, Y0 - 1, Y0 + p.nut_m + p.print_clr))
    if p.board_locate:
        tray, info['board_locate'] = _board_locate(p, tray, k, cavity)
    if p.label:
        pl = cq.Plane(origin=(p.label_at[0], y0 - 0.01, p.label_at[1]), xDir=(1, 0, 0), normal=(0, 1, 0))
        tray = tray.union(cq.Workplane(pl).text(p.label, p.label_size, p.label_h + 0.01, halign='center', valign='center',
                                                kind='bold'))
    if p.low_floor and p.layout == 'B':     # a pocket in the raised floor under the CR1220 holder
        hb = M['cr1220_holder']
        pb = Y0 + p.holder_pocket
        if pb < y0:
            tray = tray.cut(_box(hb['x'][0] - 0.5, hb['x'][1] + 0.5, pb, y0 + 0.01, hb['z'][0] - 0.5, hb['z'][1] + 0.5))
    # ---- tray: bay. Cell stop rib (with a notch for the leads) + ledges for the drop-in shelf
    sh = k['parts']['shelf']
    bx0 = sh[0]; shelf_y0, shelf_y1 = sh[2], sh[3]
    rib = _box(bx0 - p.rib_t, bx0, y0 - 0.5, shelf_y0, z0 - 1, z1 + 1).intersect(cavity)
    rib = rib.cut(_box(bx0 - p.rib_t - 1, bx0 + 1, shelf_y0 - p.lead_notch[1], shelf_y0 + 1,
                       M['jst']['z'][0], M['jst']['z'][0] + p.lead_notch[0]))
    tray = tray.union(rib)
    bay = cavity.intersect(_box(bx0, X1 + 5, shelf_y0 - p.ledge_h, shelf_y0, -200, 200))
    ledge = bay.cut(profile(p, box, shelf_y0 - p.ledge_h - 1).offset2D(-p.wall - p.ledge_w).extrude(p.ledge_h + 2))
    lp_ = k['parts']['lipo']            # keep the ledge clear of the cell's corners in the curved end
    ledge = ledge.cut(_box(lp_[0] - p.clr, lp_[1] + p.clr, lp_[2] - 1, lp_[3] + p.lipo_swell, lp_[4] - p.clr, lp_[5] + p.clr))
    if p.ledge_45:                # 45 degree underside: the ledge grows out of the wall, no overhang on the tray print
        ledge = ledge.cut(_wall_loft(p, box, shelf_y0 - p.ledge_w, p.wall - 0.001, shelf_y0 + 0.001, p.wall + p.ledge_w))
    tray = tray.union(ledge)
    if p.nail_dip:                # a fingernail goes down here past the shelf edge and lifts it (45 degree sides)
        zc_ = (z0 + z1) / 2 - 6.0
        dd, dw = p.nail_dip, 6.0
        tray = tray.cut(_prism_yz([(shelf_y0 + 0.01, zc_ - dw / 2), (shelf_y0 + 0.01, zc_ + dw / 2),
                                   (shelf_y0 - dd, zc_ + dw / 2 - dd), (shelf_y0 - dd, zc_ - dw / 2 + dd)],
                                  bx0 - p.rib_t - 1, bx0 + 0.5))
        info['nail_dip'] = dict(z=(round(zc_ - dw / 2, 2), round(zc_ + dw / 2, 2)), depth=dd)
    if p.floor_arrow:             # cut 0.5 into the bay floor: which end of the cell the leads come out of
        lpz = (lp_[4] + lp_[5]) / 2
        txt = (cq.Workplane(cq.Plane(origin=((lp_[0] + lp_[1]) / 2 - 4.0, y0 + 0.01, lpz), xDir=(0, 0, 1), normal=(0, 1, 0)))
               .text(p.floor_arrow, 4.5, -0.51, halign='center', valign='center', kind='bold'))
        tray = tray.cut(txt)
        info['floor_arrow'] = dict(text=p.floor_arrow, at=(round((lp_[0] + lp_[1]) / 2 - 4.0, 1), round(lpz, 1)), h=4.5, depth=0.5)
    # ---- tray: power-switch slot in the -z wall over the lever
    ss = switch_slot(p)
    tray = tray.cut(_box(ss['x'][0], ss['x'][1], ss['y'][0], ss['y'][1], Z0 - 2, z0 + 1).edges('|Z').fillet(0.4))
    if p.switch_scoop:            # nail scoop: a shallow pocket around the slot, 45 degree sides (prints on the wall)
        sd, m = p.switch_scoop, 1.5
        fa = cq.Wire.makePolygon([cq.Vector(ss['x'][0] - m - sd, ss['y'][0] - m - sd, Z0 - 0.01), cq.Vector(ss['x'][1] + m + sd, ss['y'][0] - m - sd, Z0 - 0.01),
                                  cq.Vector(ss['x'][1] + m + sd, ss['y'][1] + m + sd, Z0 - 0.01), cq.Vector(ss['x'][0] - m - sd, ss['y'][1] + m + sd, Z0 - 0.01)], close=True)
        fb = cq.Wire.makePolygon([cq.Vector(ss['x'][0] - m, ss['y'][0] - m, Z0 + sd), cq.Vector(ss['x'][1] + m, ss['y'][0] - m, Z0 + sd),
                                  cq.Vector(ss['x'][1] + m, ss['y'][1] + m, Z0 + sd), cq.Vector(ss['x'][0] - m, ss['y'][1] + m, Z0 + sd)], close=True)
        tray = tray.cut(cq.Workplane().add(cq.Solid.makeLoft([fa, fb], ruled=True)))

    # ---- USB-C: the end wall is flat and thin at the receptacle (outer face usb_recess before the mouth); the opening
    # fits the receptacle shell with clearance, so the plug's metal shell goes in and its overmold stops outside
    uw, uh, uy, uz = usb_window_rect(p)
    c = p.usb_chamfer
    xo = _outline_x_at(p, box, uz, side='bottom')
    usb = _xloft([(X0 - 2.0, (uw, uh), p.usb_r), (x0 + 1.0, (uw, uh), p.usb_r)], uy, uz)
    lead = _xloft([(xo - 1.0, (uw + 2 * c + 2.0, uh + 2 * c + 2.0), p.usb_r + c + 1.0),
                   (xo + c, (uw, uh), p.usb_r)], uy, uz)          # 45 degree lead-in at the outer face
    below = _box(X0 - 5, X1 + 5, Y0 - 5, split, Z0 - 5, Z1 + 5)
    for cut_ in (usb, lead):
        tray = tray.cut(cut_)
        cover = cover.cut(cut_.intersect(below) if p.usb_face_clean else cut_)
    # the cover's lip hangs down to split - lip_h just inside the end wall, over the receptacle: notch it
    usb_lip = _xloft([(X0 - 2.0, (uw, uh), p.usb_r), (x0 + p.rim_band + p.print_clr + p.lip_w + 0.5, (uw, uh), p.usb_r)], uy, uz)

    # ---- flex tab (side button over D1)
    g = tab_geometry(p)
    c_ext = info['corner_extent']
    assert X0 + info['corner_extent_bottom'] < g['hx0'] and g['hx1'] + p.tab_slit < X1 - c_ext, 'tab must sit on the straight side'
    zt, yb, sv = g['ztongue'], g['yb'], g['sv']
    tongue, grown = tab_solids(p, g)
    tray = tray.cut(grown.cut(tongue))                  # the slits: the tongue grown by tab_slit, minus the tongue
    if zt > g['zin'] + 1e-6:      # a wall thicker than the tongue: thin the tongue from inside
        tray = tray.cut(tongue.intersect(_box(g['hx0'], g['hx1'], g['ya'] - 1, yb + sv + 5, g['zin'] - 0.5, zt)))
    nl = g['ztongue'] - g['nub_z0']
    ny0, ny1 = nub_span(p, g)
    nub = _box(g['xc'] - p.nub_w / 2, g['xc'] + p.nub_w / 2, ny0, ny1, g['nub_z0'], g['ztongue'] + 0.2).edges('|Z').fillet(0.3)
    tray = tray.union(nub)
    button = tray.intersect(tab_solids(p, g, z_in=g['nub_z0'] - 0.1)[0])     # the tongue with its whole nub
    tray = tray.cut(button)                   # the tongue (with its nub) is its own colour region
    if p.tab_end_ties:            # #9 B1, owner: one tie across the end slit, centred on the tongue height, inner 0.5
        ym_ = (g['ya'] + g['yb']) / 2
        tray = tray.union(_box(g['hx1'], g['hx1'] + p.tab_slit + 0.01, ym_ - 0.225, ym_ + 0.225, g['zin'], g['zin'] + 0.5))
    if p.tab_ties:                # breakaway ties across the lower slit: the tongue's first layer bridges hinge -> tie -> tie
        # evenly over the free length, the last one past the nub and 0.7 short of the free end (spans <= 10)
        L_ = g['hx1'] - g['hx0'] - 0.7
        for i in range(p.tab_ties):
            xt = g['hx0'] + L_ * (i + 1) / p.tab_ties
            tray = tray.union(_box(xt - p.tab_tie_w / 2, xt + p.tab_tie_w / 2, g['ya'] - p.tab_slit - 0.01, g['ya'], g['zin'], g['zin'] + 0.8))
    px, py, pt = p.pad
    pad = None
    if pt > 0:
        pad = (cq.Workplane().box(px, py, pt + 0.4).edges('|Z').fillet(1.2).faces('>Z').edges().fillet(0.25)
               .translate((g['xc'] - 0.5, g['yc'], g['zout'] - 0.4 + (pt + 0.4) / 2)))
        tray = tray.cut(pad)
    lug, lp = loop_lug(p, box)
    if p.loop_gusset:
        lug = lug.union(_loop_fins(p, box, lp))
    loop = lug.cut(body)
    if p.loop_boss > 0:      # thin walls: back the lug's root from inside, between the shelf and the cover lip
        yc_, zc_ = (Y0 + Y1) / 2, (Z0 + Z1) / 2
        ya_ = max(yc_ - p.loop_t / 2 - 1.0, shelf_y1 + 0.3)
        yt_, lb_ = yc_ + p.loop_t / 2 + 1.0, p.loop_boss
        if p.loop_gusset and not p.loop_boss_flat:     # 45 degree underside (the tray prints floor down)
            boss = _xyp([(X1 - p.wall - lb_, ya_ + lb_), (X1 - p.wall - lb_, yt_), (X1 + 1, yt_), (X1 + 1, ya_), (X1 - p.wall, ya_)],
                        zc_ - p.loop_w / 2 - 2.0, zc_ + p.loop_w / 2 + 2.0)
        else:
            boss = _box(X1 - p.wall - lb_, X1 + 1, ya_, yt_, zc_ - p.loop_w / 2 - 2.0, zc_ + p.loop_w / 2 + 2.0)
        tray = tray.union(boss.intersect(cavity))

    # ---- cover: alignment lip into the tray, pillars with counterbores, RGB LED spot, colour zones
    lw_ = p.wall + p.rim_band        # the lip sits inside the tray wall's top band
    lip = (profile(p, box, split - p.lip_h).offset2D(-lw_ - p.print_clr).extrude(p.lip_h)
           .cut(profile(p, box, split - p.lip_h - 1).offset2D(-lw_ - p.print_clr - p.lip_w).extrude(p.lip_h + 2)))
    lip = lip.cut(usb_lip)
    cover = cover.union(lip)
    pcb_top = M['pcb_y'][1]
    xmid = (X0 + X1) / 2
    for hx, hz in f['holes']:
        # the step from the low to the top diameter is a 45 degree cone, so it prints without support
        rise = (p.pillar_d_top - p.pillar_d_low) / 2
        ptop = split + 0.5 if p.crown_shell <= 0 else ceiling_top + 0.5
        cover = cover.union(_cyl(hx, hz, p.pillar_d_top / 2, p.pillar_step_y + rise, ptop).intersect(body))
        cover = cover.union(_cone(hx, hz, p.pillar_d_low, p.pillar_d_top, p.pillar_step_y, p.pillar_step_y + rise + 0.01))
        cover = cover.union(_cyl(hx, hz, p.pillar_d_low / 2, pcb_top + p.pillar_gap, p.pillar_step_y + 0.01))
        if p.closure == 'front':
            cover = cover.cut(_cyl(hx, hz, p.screw_clear / 2, pcb_top, 30))
            cover = cover.cut(_cyl(hx, hz, p.cbore_d / 2, f['seat'], 30))
        elif p.closure == 'back':
            # nut boss: 45 degree cone up from the pillar, then the boss to the front plate; blind screw hole;
            # nut slot from the side that faces the middle of the case (along x)
            n0, n1 = f['nut']
            b0 = n0 - 1.0
            rise = (p.nut_boss_d - p.pillar_d_top) / 2
            cover = cover.union(_cone(hx, hz, p.pillar_d_top, p.nut_boss_d, b0 - rise, b0 + 0.01))
            if p.boss_slim:       # the boss only round the nut and its roof, then 45 degrees back to the pillar
                b1 = n1 + 0.1 + (p.nut_af + p.nut_slot_clr) / 2 + 0.8
                cover = cover.union(_cyl(hx, hz, p.nut_boss_d / 2, b0, b1))
                cover = cover.union(_cone(hx, hz, p.nut_boss_d, p.pillar_d_top, b1 - 0.01, b1 + rise))
            else:
                cover = cover.union(_cyl(hx, hz, p.nut_boss_d / 2, b0, split + 0.5))
            cover = cover.cut(_cyl(hx, hz, p.screw_clear / 2, pcb_top, f['tip'] + 0.6))
            af = p.nut_af + p.nut_slot_clr
            sy0, sy1 = n0 - 0.1, n1 + 0.1
            cover = cover.cut(_hexprism(hx, hz, af, sy0, sy1))
            xa, xb = (hx, hx + 10) if hx < xmid else (hx - 10, hx)
            cover = cover.cut(_box(xa, xb, sy0, sy1, hz - af / 2, hz + af / 2))
            if p.pillar_roofs:    # audit: v1.1 filled these with support. 90 degree cone at the blind end of the
                # screw hole, a 45 degree gable over the nut pocket and its slot (both print as ceilings, crown up)
                ht = f['tip'] + 0.6
                cover = cover.cut(_cone(hx, hz, p.screw_clear, 0.01, ht - 0.01, ht + p.screw_clear / 2))
                D = af / math.cos(math.pi / 6)
                ra, rb = min(hx - D / 2, xa) - 0.01, max(hx + D / 2, xb) + 0.01
                cover = cover.cut(_prism_yz([(sy1 - 0.01, hz - af / 2), (sy1 - 0.01, hz + af / 2), (sy1 + af / 2, hz)], ra, rb))
    if p.closure == 'snap':
        tray, cover = snap_features(p, tray, cover, split, box, usb=(uz, uw))
    window = insert = None
    if p.window != 'none':
        cover, insert, window = window_cut(p, cover, box, d)
    if p.xiao_pocket > 0:         # more room over the XIAO's +2.0 lift without a taller case: a recess in the ceiling
        bpx = board_parts()['xiao'].val().BoundingBox()
        inner_ = profile(p, box, split - 2).offset2D(-p.wall - p.rim_band - p.print_clr - p.lip_w - 0.3).extrude(4)
        cover = cover.cut(_box(bpx.xmin - 0.5, bpx.xmax + 0.5, split - 2, split + p.xiao_pocket, bpx.zmin - 0.5, bpx.zmax + 0.5)
                          .intersect(inner_))
        info['xiao_pocket'] = dict(depth=p.xiao_pocket, x=(round(bpx.xmin - 0.5, 1), round(bpx.xmax + 0.5, 1)),
                                   z=(round(bpx.zmin - 0.5, 1), round(bpx.zmax + 0.5, 1)))
    fins = None
    if p.cover_fins > 0:
        fins = _cover_fins(p, cover, box, split, pcb_top + p.pillar_gap, f, d)
    # RGB LED: a thin skin of the (lightest) cover filament over the XIAO user LED, or a through-hole
    top = d['top']
    Ld = M['xiao_rgb_led']
    lx, lz = sum(Ld['x']) / 2, sum(Ld['z']) / 2
    led = None
    if p.led_mode == 'hole':
        cover = cover.cut(_cyl(lx, lz, p.led_hole_d / 2, split - 1, Y1 + 5))
    else:
        cyl = _cyl(lx, lz, p.led_d / 2, split - 1, Y1 + 5)
        if p.led_rect:            # owner idea: a rectangular glow spot, long side along the case axis (x)
            rl, rw = p.led_rect
            cyl = _box(lx - rl / 2, lx + rl / 2, split - 1, Y1 + 5, lz - rw / 2, lz + rw / 2).edges('|Y').fillet(0.3)
        if p.led_land:            # audit: the spot sat on a facet step and the skin split in two; a flat land at rim
            cover = cover.cut(_cyl(lx, lz, p.led_land / 2, Y1, Y1 + 10))   # level makes it one flat 0.3 bridge
        if p.led_normal:          # owner, 2026-09-29: no land, nothing on the outside; the skin follows the facets
            pocket = cyl.intersect(cover).cut(_grown_air(body, lx, lz, split, top, p.led_skin * p.led_grow))
            if p.led_side_wall:
                pocket = pocket.cut(_grown_air(body, lx, lz, split, top, p.led_side_wall, flat=True))
        else:
            pocket = cyl.intersect(cover).intersect(cover.translate((0, -p.led_skin, 0)))
        cover = cover.cut(pocket)
        if p.led_mouth:           # a 45 degree lead-in on the mouth (the bed face): no squished ring on the first layer
            m_, n_ = p.led_mouth, 4
            for i_ in range(n_):
                g_ = m_ * (1 - (i_ + 0.5) / n_)
                sl_ = cyl.intersect(_box(X0 - 5, X1 + 5, split - 0.01 + m_ * i_ / n_, split + m_ * (i_ + 1) / n_, -200, 200))
                for dx, dz in ((g_, 0), (-g_, 0), (0, g_), (0, -g_), (g_ * .7071, g_ * .7071), (g_ * .7071, -g_ * .7071),
                               (-g_ * .7071, g_ * .7071), (-g_ * .7071, -g_ * .7071)):
                    cover = cover.cut(sl_.translate((dx, 0, dz)))
        # measured from the line up: below it the tray is not in 'cover', so its wall would read as air joining the outside
        led_min_skin = min_skin_over(cover, pocket, _cyl(lx, lz, max(p.led_d, max(p.led_rect or (0,))) / 2 + 2.0, split + 0.001, Y1 + 5))
        info['led_max_skin'] = round(_max_skin(cover, pocket, lx, lz, split, top, p.led_d if not p.led_rect else min(p.led_rect)), 3)
        if p.led_normal and p.led_skin_check:          # owner: the skin 0.30..0.45 everywhere over the spot
            smax = 0.45 if not p.led_side_wall else 0.55    # at the step strip the side wall lifts the ceiling a bit
            assert led_min_skin >= 0.3 - 1e-3 and info['led_max_skin'] <= smax, \
                f"LED skin {led_min_skin:.3f}..{info['led_max_skin']:.3f} outside 0.30..{smax}"
        if p.led_body:
            led = cover.intersect(cyl)                 # the skin over the LED: its own body, palest filament
            cover = cover.cut(led)
    # front colour zones (owner sketch): zone 2 = centre diamond, zone 1 = the four edge-sharing
    # diamonds, zone 3 = the rest (the cover body). Zones 1-2 are a zone_depth skin under the facets.
    zone1 = zone2 = None
    if p.front_zones:
        skin = cover.cut(cover.translate((0, -p.zone_depth, 0)))
        ya, yb = split - 1, top + 2
        zone2 = skin.intersect(_diamond(d, 0, 0, ya, yb))
        four = None
        for cu, cv in ((d['b'], d['a']), (-d['b'], d['a']), (d['b'], -d['a']), (-d['b'], -d['a'])):
            dm = _diamond(d, cu, cv, ya, yb)
            four = dm if four is None else four.union(dm)
        zone1 = skin.intersect(four).cut(zone2)
        cover = cover.cut(zone1).cut(zone2)

    # ---- shelf (separate drop-in print): plate on the ledges + motor locating posts
    sc = p.print_clr if p.shelf_clr is None else p.shelf_clr
    sce = sc if p.shelf_edge_clr is None else p.shelf_edge_clr
    shelf = (profile(p, box, shelf_y0).offset2D(-p.wall - sc).extrude(shelf_y1 - shelf_y0)
             .intersect(_box(bx0 + sce, X1 + 5, shelf_y0 - 1, shelf_y1 + 1, -200, 200)))
    if p.bay_hold:
        tray, shelf, hold = bay_hold(p, tray, shelf, box, k, cavity)
    else:
        mx0, mx1, my0, my1, mz0, mz1 = k['parts']['motor']
        tails = DATASHEET['motor_split'][0]
        # #11 throne test (owner, 2026-09-28): the seat hugs the real 20 x 20 PCB. The -z pair stays where #7 had it,
        # the connector-side pair moves 4 mm in, so the module sits toward the middle, clear of the rounded corner;
        # the seat drops seat_drop (pins + 0.3 under the PCB)
        if p.seat_l:
            mz1 = mz0 + p.seat_l
        if p.seat_center:        # owner: the module centred on the case's width axis (the loop's axis)
            zc_case = (box[4] + box[5]) / 2
            mz0, mz1 = zc_case - (mz1 - mz0) / 2, zc_case + (mz1 - mz0) / 2
        pcb_y = my0 + tails - p.seat_drop
        posts = None
        for (cx, sx) in ((mx0, 1), (mx1, -1)):
            for (cz, sz) in ((mz0, 1), (mz1, -1)):
                q, fe = p.motor_post, p.fence
                xa, xb = sorted((cx - sx * fe, cx + sx * q)); za, zb = sorted((cz - sz * fe, cz + sz * q))
                post = _box(xa, xb, shelf_y1 - 0.01, pcb_y + p.fence_h, za, zb)
                post = post.cut(_box(mx0 - p.print_clr, mx1 + p.print_clr, pcb_y, pcb_y + 20, mz0 - p.print_clr, mz1 + p.print_clr))
                posts = post if posts is None else posts.union(post)
        if p.post_keepout:       # keep every post post_keepout off the tray (the #7 +x/+z post sat in the corner radius)
            d = p.post_keepout
            for vx, vz in ((d, 0), (-d, 0), (0, d), (0, -d), (d * 0.7071, d * 0.7071), (d * 0.7071, -d * 0.7071),
                           (-d * 0.7071, d * 0.7071), (-d * 0.7071, -d * 0.7071)):
                posts = posts.cut(tray.translate((vx, 0, vz)))
        if p.seat_posts:
            shelf = shelf.union(posts)
        if p.top_chamfer:        # the bed face's edge (#13 prints top down)
            shelf = _chamfer_top(p, shelf, box, shelf_y0, shelf_y1, bx0)
        if p.top_label:          # #11: which side is up, raised on the top face beside the seat (-z of the posts)
            shelf, info['top_label'] = _top_label(p, shelf, box, shelf_y0, shelf_y1, bx0, (mz0 - p.fence - 1.0, mz1 + p.fence + 1.0))
        if p.under_fence:        # #13: a fence / tabs under the shelf's partition-side edge (stops it sliding back)
            shelf, info['under_fence'] = _under_fence(p, shelf, box, k, bx0, shelf_y0)
        if p.proto is not None:
            shelf = shelf.cut(_under_number(p, k, shelf_y0))
        hold = None
    if p.proto is not None and hold:
        tray, shelf, hold['proto'] = proto_marks(p, tray, shelf, hold, k)
    seat = hold.pop('seat_body', None) if hold else None
    shelf_ribs = None
    if p.shelf_ribs and hold:
        shelf, shelf_ribs, hold['ribs'] = _shelf_ribs(p, shelf, box, k)

    if p.proto is not None and p.flat_top:
        lpk = k['parts']['lipo']
        t3 = (cq.Workplane(cq.Plane(origin=((lpk[0] + lpk[1]) / 2, split + 0.01, (z0 + z1) / 2 - 13.5), xDir=(0, 0, -1), normal=(0, -1, 0)))
              .text(f'{p.proto}.3', 5.0, 0.51, halign='center', valign='center', kind='bold'))
        cover = cover.union(t3)
        info['cover_number'] = f'{p.proto}.3'
    info.update(split=split, tab=g, loop=lp, fastening=f, switch_slot=ss, lip=(p.lip_w, p.lip_h), hold=hold,
                led=dict(x=lx, z=lz, mode=p.led_mode, skin=p.led_skin, d=p.led_d, hole=p.led_hole_d,
                         min_skin=None if p.led_mode == 'hole' else round(led_min_skin, 3),
                         zone=zone_of(p, box, lx, lz), top_y=Ld['y'][1]),
                usb_depth=M['usb_c']['x_mouth'] - xo, window=window, window_insert=insert)
    if p.partition_lip > 0:  # #17 option D: the fence's job moves to the tray (10.2C's rib lattice failed)
        lip = _box(bx0 - p.rib_t, bx0, shelf_y0 - 0.01, shelf_y0 + p.partition_lip, z0 - 1, z1 + 1).intersect(cavity)
        lip = lip.cut(_box(bx0 - p.rib_t - 1, bx0 + 1, shelf_y0 - 1, shelf_y0 + 5,
                           M['jst']['z'][0], M['jst']['z'][0] + p.lead_notch[0]))          # the leads' notch stays open
        over = shelf.val().intersect(_box(bx0 - p.rib_t - 1, bx0 + 0.25, shelf_y0 - 1, 40, -200, 200).val())
        low = []
        for so in over.Solids():                                                   # e.g. the 11.2 posts' feet
            ob = so.BoundingBox()
            if ob.ymin > shelf_y0 + 0.05:
                lip = lip.cut(_box(ob.xmin - 0.3, bx0 + 1, ob.ymin - 0.2, 40, ob.zmin - 0.3, ob.zmax + 0.3))
                low.append((round(ob.zmin, 2), round(ob.zmax, 2), round(ob.ymin - 0.2 - shelf_y0, 2)))
        tray = tray.union(lip)
        info['partition_lip'] = dict(h=p.partition_lip, x=(round(bx0 - p.rib_t, 2), round(bx0, 2)), lowered=low)
    if p.port_pocket > 0:    # a vertical groove open to the rim: no overhang with the tray printed floor down
        gx0, gx1 = M['grove_a0']['x']
        tray = tray.cut(_box(gx0 - 0.5, min(gx1 + 0.5, g['hx0'] - p.tab_slit - 0.3), M['pcb_y'][0], split + 0.01,
                             z1 - 0.01, z1 + p.port_pocket))
    frame = frame_tie = None
    if p.cover_split:
        cover, frame, frame_tie, info['cover_split'] = _split_cover(p, cover, box, split, k, f)
        if p.cover_onepiece:      # #19: the halves fused (the lip, pillars, bar, feet and legs hang from the ceiling)
            cover, frame, frame_tie = cover.union(frame), None, None
            if p.cover_fins > 0:
                fins = _cover_fins(p, cover, box, split, pcb_top + p.pillar_gap, f, d)
                ring = _lip_fin_ring(p, cover, box, split, pcb_top + p.pillar_gap)
                fins = ring if fins is None else (fins if ring is None else fins.union(ring))
    bodies = dict(tray=tray, button=button, loop=loop, cover=cover, shelf=shelf)
    if frame is not None:
        bodies['frame'] = frame
    if frame_tie is not None:
        bodies['frame_tie'] = frame_tie
    if seat is not None:
        bodies['seat'] = seat
    if shelf_ribs is not None:
        bodies['shelf_ribs'] = shelf_ribs
    if zone1 is not None:
        bodies.update(zone1=zone1, zone2=zone2)
    if pad is not None:
        bodies['pad'] = pad
    if fins is not None:
        bodies['support_fins'] = fins
    if led is not None:
        bodies['led'] = led
    return bodies, info


def _grown_air(body, lx, lz, split, top, r, flat=False):
    """The air outside the outer body near the LED, grown by about r in every direction (a Minkowski sum over 26
    directions); cutting it from the pocket leaves a skin that follows the outer face, facets and steps alike."""
    zone = _box(lx - 6, lx + 6, split, top + 3, lz - 6, lz + 6)
    air = zone.cut(body)
    dirs = [(i, j, k) for i in (-1, 0, 1) for j in (-1, 0, 1) for k in (-1, 0, 1) if (i, j, k) != (0, 0, 0)]
    if flat:                      # horizontal growth only (walls), 16 directions
        dirs = [(math.cos(a_), 0, math.sin(a_)) for a_ in [i * math.pi / 8 for i in range(16)]]
    out = air
    for i, j, k in dirs:
        n = math.sqrt(i * i + j * j + k * k)
        out = out.union(air.translate((r * i / n, r * j / n, r * k / n)))
    return out


def _max_skin(part, void, lx, lz, split, top, d, n=7):
    """Largest skin over the LED spot: from points on the pocket's upward-facing ceiling (a grid over the spot) to the
    outside air, the least distance each; the largest of those."""
    from OCP.BRepExtrema import BRepExtrema_DistShapeShape
    from OCP.BRepBuilderAPI import BRepBuilderAPI_MakeVertex
    from OCP.gp import gp_Pnt
    air = _box(lx - 6, lx + 6, split, top + 3, lz - 6, lz + 6).cut(part).cut(void)
    solids = [s_ for v in air.vals() for s_ in v.Solids()]
    outside = max(solids, key=lambda s_: s_.BoundingBox().ymax)
    worst = 0.0
    for i in range(n):
        for j in range(n):
            x = lx + (i / (n - 1) - 0.5) * d * 0.8
            z = lz + (j / (n - 1) - 0.5) * d * 0.8
            if (x - lx) ** 2 + (z - lz) ** 2 > (d * 0.4) ** 2:
                continue
            # the ceiling point on this vertical line: the top of the void there
            line = _box(x - 0.01, x + 0.01, split - 2, top + 3, z - 0.01, z + 0.01).intersect(void)
            try:
                y = line.val().BoundingBox().ymax
            except Exception:
                continue
            v = BRepBuilderAPI_MakeVertex(gp_Pnt(x, y, z)).Vertex()
            dd = BRepExtrema_DistShapeShape(v, outside.wrapped); dd.Perform()
            worst = max(worst, dd.Value())
    return worst


def min_skin_over(part, void, around):
    """Least material (3D, so normal to a sloped or stepped outer face) between an inner cut `void` and the air
    outside `part`, looked for inside `around`: the air in `around` falls into the pieces outside and inside the part;
    the outside piece is the one reaching highest. 0.0 when the cut breaks through (the two pieces are one)."""
    from OCP.BRepExtrema import BRepExtrema_DistShapeShape
    air = around.cut(part).vals()
    solids = [s for v in air for s in v.Solids()]
    outside = max(solids, key=lambda s: s.BoundingBox().ymax)
    d = BRepExtrema_DistShapeShape(outside.wrapped, cq.Compound.makeCompound(void.vals()).wrapped)
    d.Perform()
    return d.Value()


def usb_window_rect(p: P):
    """(width, height, y centre, z centre) of the USB-C window: centred on the receptacle shell, except that a top edge
    within 0.8 mm of the parting line (on either side) moves onto it: the window is then an open notch in the tray rim
    that the cover closes, and neither part keeps a sliver thinner than 0.8 mm."""
    uw, uh = p.usb_window
    uy, uz = usb_axis()
    split = packing(p)['cavity'][3]
    top, bot = uy + uh / 2, uy - uh / 2 - p.usb_window_low
    if abs(split - top) < 0.8:
        top = split
    return uw, top - bot, (top + bot) / 2, uz


def usb_axis():
    """(y, z) centre of the USB-C receptacle shell at its mouth."""
    u = MEASURED['usb_c_shell']
    return sum(u['y']) / 2, sum(u['z']) / 2


def _xloft(sections, yc, zc):
    """Ruled loft along +x through [(x, (width z, height y), r)] rounded rectangles centred on (yc, zc)."""
    ws = []
    for x, (w, h), r in sections:
        r = max(0.05, min(r, w / 2 - 0.05, h / 2 - 0.05))
        pts = [cq.Vector(x, yc - h / 2 + r, zc - w / 2 + r), cq.Vector(x, yc - h / 2 + r, zc + w / 2 - r),
               cq.Vector(x, yc + h / 2 - r, zc + w / 2 - r), cq.Vector(x, yc + h / 2 - r, zc - w / 2 + r)]
        ws.append(cq.Wire.makePolygon(pts, close=True).offset2D(r, 'arc')[0])
    return cq.Workplane().add(cq.Solid.makeLoft(ws, ruled=True))


def usb_window_report(p: P, info):
    """Numbers of the USB-C opening: size, clearance to the receptacle shell, depth of the mouth under the outer face,
    how far a plug's metal shell goes in, and what is left of the tray above the opening at the split."""
    uw, uh, uy, uz = usb_window_rect(p)
    sh = MEASURED['usb_c_shell']; u = MEASURED['usb_c']
    xo = _outline_x_at(p, info['box'], uz, side='bottom')
    depth = u['x_mouth'] - xo
    shell_len = DATASHEET['usb_c_plug_shell'][2]
    top = uy + uh / 2
    return dict(window=(uw, uh), r=p.usb_r, chamfer=p.usb_chamfer, centre_yz=(round(uy, 3), round(uz, 3)),
                clearance_z=round((uw - (sh['z'][1] - sh['z'][0])) / 2, 3),
                clearance_y=round((uh - (sh['y'][1] - sh['y'][0])) / 2, 3),
                mouth_depth=round(depth, 3), outer_face_x=round(xo, 3),
                plug_shell_in_receptacle=round(min(shell_len, shell_len - depth), 3),
                receptacle_depth=round(u['x_back'] - u['x_mouth'], 2),
                tray_over_window=round(info['split'] - top, 3), tray_over_window_at_face=round(info['split'] - top - p.usb_chamfer, 3),
                end_wall=round(end_wall(p), 3))


def _xyz_prism(pts, y0, y1):
    """Prism along y over a polygon given as (x, z) points."""
    pl = cq.Plane(origin=(0, y0, 0), xDir=(1, 0, 0), normal=(0, 1, 0))     # local (u, v) = (x, -z)
    return cq.Workplane(pl).polyline([(x, -z) for x, z in pts]).close().extrude(y1 - y0)


def _xyp(pts, z0, z1):
    """Prism along z over a polygon in (x, y)."""
    pl = cq.Plane(origin=(0, 0, z0), xDir=(1, 0, 0), normal=(0, 0, 1))
    return cq.Workplane(pl).polyline(pts).close().extrude(z1 - z0)


def _cover_fins(p: P, cover, box, split, ybed, f, d):
    """Designed breakaway support for the crown-up cover (audit: the slicer's grid put 11.45 g under the ceiling).
    Fins along x every cover_fins across z, from the bed (the pillar tips) up to the ceiling: one line (0.45) for the
    top 1.0 mm, so they snap off the ceiling, two lines below. They stay clear of the lip, the pillars and the LED spot
    and are their own body (printed with the cover, never part of the case checks)."""
    X0, X1, Y0, Y1, Z0, Z1 = box
    inset = p.wall + p.rim_band + p.print_clr + p.lip_w + 0.8
    top = d['top'] + 1
    inner = profile(p, box, ybed).offset2D(-inset).extrude(top - ybed)
    air = inner.cut(cover)
    zc = (Z0 + Z1) / 2
    zs = [zc + i * p.cover_fins for i in range(-10, 11)]
    out = None
    for z in zs:
        parts_ = [w for w in (air.intersect(_box(X0, X1, ybed - 1, top, z - 0.225, z + 0.225)),
                              air.translate((0, -1.0, 0)).intersect(air).intersect(_box(X0, X1, ybed - 1, top, z - 0.45, z + 0.45)))
                  if any(v.Volume() > 1e-3 for v in w.vals())]
        for w in parts_:
            out = w if out is None else out.union(w)
    if out is None:
        return None
    keep = None
    for hx, hz in f['holes']:
        k_ = _cyl(hx, hz, p.nut_boss_d / 2 + 1.0, ybed - 1, top)
        keep = k_ if keep is None else keep.union(k_)
    Ld = MEASURED['xiao_rgb_led']
    keep = keep.union(_cyl(sum(Ld['x']) / 2, sum(Ld['z']) / 2, max(p.led_land, p.led_d) / 2 + 1.0, ybed - 1, top))
    out = out.cut(keep)
    # only pieces that stand on the bed (a fin cut short by a keep-out would start in the air)
    solids = [s_ for v in out.vals() for s_ in v.Solids() if s_.BoundingBox().ymin < ybed + 0.05 and s_.Volume() > 1.0]
    return cq.Workplane().add(cq.Compound.makeCompound(solids)) if solids else None


def _lip_fin_ring(p: P, cover, box, split, ybed):
    """#19: a one-line (0.45) fin ring from the bed (the pillar tips) up under the lip ring's bottom face, wherever the
    lip is (it is notched at USB-C). The crown-up one-piece cover's lip starts 1.5 under the ceiling, in the air."""
    X0, X1, Y0, Y1, Z0, Z1 = box
    lo = p.wall + p.rim_band + p.print_clr
    yl = split - p.lip_h
    sl = cover.intersect(_box(X0 - 5, X1 + 5, yl - 0.01, yl + 0.15, -200, 200))
    feet = []
    for v in sl.vals():
        for fc in v.Faces():
            if fc.normalAt().y < -0.9 and abs(fc.Center().y - yl) < 0.05:
                feet.append(cq.Solid.extrudeLinear(fc.outerWire(), fc.innerWires(), cq.Vector(0, ybed - yl, 0)))
    if not feet:
        return None
    shadow = cq.Workplane().add(cq.Compound.makeCompound(feet))
    mid = lo + p.lip_w / 2                                  # the ring's centre line, 0.45 wide
    band = (profile(p, box, ybed).offset2D(-mid + 0.225).extrude(yl - ybed)
            .cut(profile(p, box, ybed - 1).offset2D(-mid - 0.225).extrude(yl - ybed + 2)))
    ring = band.intersect(shadow)
    solids = [s_ for v in ring.vals() for s_ in v.Solids() if s_.Volume() > 0.5]
    return cq.Workplane().add(cq.Compound.makeCompound(solids)) if solids else None


def _face_screws(p: P, face, frame, box, split):
    """#22: each screw goes up from under the frame plate: a boss (face_screw boss d) hangs from the plate with a
    counterbore for the pan head (flush with the boss end), a clearance hole to the line, and a pilot in the face. The
    engagement e = min(max, face - 0.8 skin - 0.3 past the tip), measured as the thinnest face over the pilot's disk;
    the boss is as deep as the screw needs: length - e + head. Frame: parting face down, the boss grows up from the plate
    and the counterbore opens at its top (no overhang). Face: crown up, the pilot is a blind hole from the bed."""
    L, hd, hk, pd, cd, ld, cbd, bd, emax = p.face_screw
    fit = profile(p, box, split - 12).offset2D(-p.wall - p.rim_band - p.print_clr).extrude(14)
    loc = [(round(x_, 3), round(z_, 3)) for x_, z_ in p.face_screw_locators]
    out = []
    for sx, sz in p.face_screws:
        tops = []
        for dx, dz in ((0, 0), (1.2, 0), (-1.2, 0), (0, 1.2), (0, -1.2), (0.85, 0.85), (-0.85, 0.85), (0.85, -0.85), (-0.85, -0.85)):
            col = face.intersect(_box(sx + dx - 0.03, sx + dx + 0.03, split - 1, split + 20, sz + dz - 0.03, sz + dz + 0.03))
            tops.append(col.val().BoundingBox().ymax if col.vals() else split)
        t = min(tops) - split
        e = min(emax, t - 0.8 - 0.3)
        yb = split + e - L                       # the head's bearing face
        y0 = yb - hk                             # the boss end = the head's top, flush
        frame = frame.union(_cyl(sx, sz, bd / 2, y0, split).intersect(fit))
        frame = frame.cut(_cyl(sx, sz, cbd / 2, y0 - 1, yb))
        cl = ld if (round(sx, 3), round(sz, 3)) in loc else cd
        frame = frame.cut(_cyl(sx, sz, cl / 2, yb - 0.01, split + 0.01))
        face = face.cut(_cyl(sx, sz, pd / 2, split - 0.01, split + e + 0.3))
        face = face.cut(_cone(sx, sz, pd + 0.6, pd, split - 0.01, split + 0.3))     # a 0.3 lead-in at the pilot's mouth
        out.append(dict(at=(sx, sz), face=round(t, 2), engage=round(e, 2), boss_depth=round(split - y0, 2),
                        boss_end_y=round(y0, 2), skin_over_tip=round(t - e - 0.3, 2), locator=cl == ld))
    return face, frame, out


def _plug_pocket(p: P, face, split, k):
    """#18.1c: a pocket in the face's back over the motor connector's plug: as deep as face_pocket_skin allows (the skin
    measured vertically under the outer surface), in flat terraces face_pocket_step apart, so every ceiling is a bridge
    between the terrace steps (the face prints crown up). Returns (face, info: ceiling range, plug top, margin)."""
    mx0, mx1, my0, my1, mz0, mz1 = k['parts']['motor']
    zc_ = (mz0 + mz1) / 2 + p.motor_dz
    tails, pcb, _ = DATASHEET['motor_split']
    pcb_top = my0 + tails + pcb
    x0, x1, dz0, dz1 = p.motor_conn_xz
    fp = (mx0 + x0, mx0 + x1, zc_ + dz0, zc_ + dz1)
    shifted = face.translate((0, -p.face_pocket_skin, 0))
    pocket, L, levels = None, split + p.face_pocket_step, []
    while L < split + 12:
        sl = shifted.intersect(_box(fp[0], fp[1], L - 0.005, L + 0.005, fp[2], fp[3]))
        cols = []
        for v in sl.vals():
            for fc in v.Faces():
                if fc.normalAt().y < -0.9:
                    f2 = fc.translate(cq.Vector(0, (split - 0.01) - fc.Center().y, 0))
                    cols.append(cq.Solid.extrudeLinear(f2.outerWire(), f2.innerWires(), cq.Vector(0, L - split + 0.01, 0)))
        if not cols:
            break
        col = cq.Workplane().add(cq.Compound.makeCompound(cols))
        pocket = col if pocket is None else pocket.union(col)
        levels.append(round(L, 2))
        L += p.face_pocket_step
    plug_top = pcb_top + p.motor_conn_h + p.motor_plug_proud
    info = dict(footprint=tuple(round(v, 2) for v in fp), ceiling=(levels[0], levels[-1]) if levels else None,
                plug_top=round(plug_top, 2), pcb_top=round(pcb_top, 2))
    if pocket is not None:
        face = face.cut(pocket)
    return face, info


def _face_bosses(p: P, face, frame, box, split, f):
    """#20: the frame pillars' nut bosses (from 1.0 under the nut up to the line) move into the face as round bosses
    hanging from its back; the nut sits in them over a ledge, so M2 x back_screw_len clamps tray, pillar and face. The
    face prints crown up standing on the bosses (support only under its back). The frame keeps each pillar up to the
    boss bottom and a 0.8 collar round the boss (0.2 off it) that joins the pillar to the lip, the webs and the spine:
    three pillars sit ~4 from the wall, so the lip there becomes the collar's outer part."""
    X0, X1, Y0, Y1, Z0, Z1 = box
    n0, n1 = f['nut']
    yb = n0 - 1.0                                           # the boss bottom = the old frame boss start (b0)
    fit = profile(p, box, yb - 1).offset2D(-p.wall - p.rim_band - p.print_clr).extrude(split - yb + 2)   # the lip's outer edge
    af = p.nut_af + p.nut_slot_clr
    sy0, sy1 = n0 - 0.1, n1 + 0.1
    xmid = (X0 + X1) / 2
    rb = p.face_boss_w / 2
    out = []
    for hx, hz in f['holes']:
        boss = _cyl(hx, hz, rb, yb, split + 0.01).intersect(fit)
        face = face.union(boss)
        frame = frame.cut(_cyl(hx, hz, rb + 0.2, yb, split + 0.01))
        frame = frame.union(_cyl(hx, hz, rb + 1.0, yb, split).cut(_cyl(hx, hz, rb + 0.2, yb - 1, split + 1)).intersect(fit))
        face = face.cut(_cyl(hx, hz, p.screw_clear / 2, yb - 1, f['tip'] + 0.6))
        face = face.cut(_hexprism(hx, hz, af, sy0, sy1))
        xa, xb = (hx, hx + 10) if hx < xmid else (hx - 10, hx)      # the slot opens toward the case middle (the collar
        face = face.cut(_box(xa, xb, sy0, sy1, hz - af / 2, hz + af / 2))   # keeps the nut in once assembled)
        ht = f['tip'] + 0.6                                  # 90 degree cone over the screw's blind end, 45 degree gable
        face = face.cut(_cone(hx, hz, p.screw_clear, 0.01, ht - 0.01, ht + p.screw_clear / 2))
        D = af / math.cos(math.pi / 6)
        ra, rb_ = min(hx - D / 2, xa) - 0.01, max(hx + D / 2, xb) + 0.01
        face = face.cut(_prism_yz([(sy1 - 0.01, hz - af / 2), (sy1 - 0.01, hz + af / 2), (sy1 + af / 2, hz)], ra, rb_))
        bb = boss.val().BoundingBox()
        out.append(dict(at=(hx, hz), d=p.face_boss_w, x=(round(bb.xmin, 2), round(bb.xmax, 2)), z=(round(bb.zmin, 2), round(bb.zmax, 2)),
                        h=round(split - yb, 2), ledge=round(sy0 - yb, 2), nut=(round(sy0, 2), round(sy1, 2)),
                        wall_at_corners=round(rb - D / 2, 2), wall_at_flats=round(rb - af / 2, 2),
                        tip_cone_top=round(ht + p.screw_clear / 2, 2)))
    return face, frame, out


def _split_cover(p: P, cover, box, split, k, f):
    """10.3a / 10.3b (owner, 2026-09-28): the cover in two halves that print without support, joined on the tray/cover
    parting line (so the seam is the one the case has anyway). 10.3a, the face plate: everything above the line,
    printed crown up on its flat back. 10.3b, the frame: the lip, the pillars and bosses, webs from the pillars to the lip,
    a spine (0.8 under the line) to two dowel posts under the crown's thick centre, a label pad, and a bar over the motor
    module 0.3 above its top (the hold-down); printed with the parting face down. Two loose dowels (peg_d, D-flat)
    join them; the holes are peg_clr per side, 0.3 lead-in."""
    X0, X1, Y0, Y1, Z0, Z1 = box
    x0, x1, y0, y1, z0, z1 = k['cavity']
    zc = (Z0 + Z1) / 2
    face = cover.intersect(_box(X0 - 5, X1 + 5, split, Y1 + 20, -200, 200))
    frame = cover.intersect(_box(X0 - 5, X1 + 5, Y0 - 5, split, -200, 200))
    t_sp = 0.8                                            # spine / webs / bar thickness under the line
    lip_in = p.wall + p.rim_band + p.print_clr + p.lip_w   # the lip ring's inner edge off the outer outline
    extra = []
    for hx, hz in f['holes']:                              # webs: each pillar to the nearest long-side lip
        zl = (z1 - (lip_in - p.wall) + 0.5) if hz > zc else (z0 + (lip_in - p.wall) - 0.5)
        extra.append(_box(hx - 1.0, hx + 1.0, split - p.lip_h, split, *sorted((hz, zl))))
    posts = [(152.0, zc), (172.0, zc)] if p.centre_dowels else []
    for px, pz in posts:
        extra.append(_cyl(px, pz, 3.0, split - 3.2, split))
    extra.append(_box(172.0 - 1.0, 172.0 + 1.0, split - t_sp, split, f['holes'][2][1], f['holes'][0][1]))   # z spine
    extra.append(_box(152.0, 172.0, split - t_sp, split, zc - 1.0, zc + 1.0))                              # x spine
    extra.append(_box(153.5, 170.5, split - 1.2, split, zc - 3.5, zc + 3.5))                               # label pad
    mx0, mx1, my0, my1, mz0, mz1 = k['parts']['motor']
    mz0, mz1 = mz0 + p.motor_dz, mz1 + p.motor_dz
    tails, pcb, sock = DATASHEET['motor_split']
    mtop = my0 + tails + pcb + sock                        # the module's top (the socket)
    bar_x = (mx0 + mx1) / 2
    boss_gap = p.hold_boss if p.hold_boss is not None else p.hold_feet
    if not p.hold_down:
        pass
    elif boss_gap is not None or p.hold_legs:   # the bar only 1.2 under the line; the boss / legs hang from it (P10 fit)
        extra.append(_box(bar_x - 3.0, bar_x + 3.0, split - 1.2, split, z0 + (lip_in - p.wall) - 0.5, z1 - (lip_in - p.wall) + 0.5))
    else:
        extra.append(_box(bar_x - 3.0, bar_x + 3.0, mtop + 0.3, split, z0 + (lip_in - p.wall) - 0.5, z1 - (lip_in - p.wall) + 0.5))
    feet_legs = []
    if boss_gap is not None and p.hold_down:      # one boss hold_boss_w wide down to the target plane + gap; keep-out bands split it
        yt_ = (my0 + tails + pcb) if p.hold_boss_y is None else p.hold_boss_y
        zc_ = (mz0 + mz1) / 2
        bands = [(max(zc_ - p.hold_boss_w / 2, mz0 + 0.5), min(zc_ + p.hold_boss_w / 2, mz1 - 0.5))]
        for ka, kb in p.hold_boss_keep:
            bands = [piece for a_, b_ in bands for piece in ((a_, min(b_, zc_ + ka)), (max(a_, zc_ + kb), b_)) if piece[1] - piece[0] > 1.0]
        for i_, (za_, zb_) in enumerate(sorted(bands)):
            g_ = p.hold_boss_gaps[i_] if p.hold_boss_gaps else boss_gap
            feet_legs.append(_box(bar_x - 3.0, bar_x + 3.0, yt_ + g_, split, za_, zb_))
    if p.hold_legs and p.hold_down:               # owner: the bar sagged with no legs; legs down to the shelf top at both ends
        sh_top = k['parts']['shelf'][3]
        spans_ = p.hold_legs_z or ((mz0 - 1.0 - 2.0 - 4.0, mz0 - 1.0 - 2.0), (mz1 + 1.0 + 2.0, mz1 + 1.0 + 2.0 + 4.0))
        for za_, zb_ in spans_:
            feet_legs.append(_box(bar_x - 2.0, bar_x + 2.0, sh_top + 0.2, split, za_, zb_))
    extra += feet_legs
    if p.frame_plate is not None: # #18.1: a plate at the parting face (the frame's first layers), overlapping the lip ring
        plate = (profile(p, box, split - t_sp).offset2D(-lip_in + 0.4).extrude(t_sp)
                 .intersect(_box(p.frame_plate, X1 + 5, split - t_sp - 1, split + 1, -200, 200)))
        for wx0, wx1, wz0, wz1 in p.frame_windows:
            plate = plate.cut(_box(wx0, wx1, split - 5, split + 5, wz0, wz1))
        extra.append(plate)
    if p.usb_bar_x is not None:   # 18.3b: a crossbar lip to lip (the USB-end dowels hang from it), like the z spine
        extra.append(_box(p.usb_bar_x - 1.0, p.usb_bar_x + 1.0, split - t_sp, split, z0 - 5, z1 + 5))
    for sx_, sz_, rx_, rz_ in p.short_dowels:   # #18.1b: the post (+ a rib to a screw pillar when rx_ is given); its depth
        # follows the dowel: #18.1c uses the normal 5 mm dowel there too, 1.5 in the thin face, the rest in the post
        i_ = [q[:2] for q in p.short_dowels].index((sx_, sz_))
        sfd_ = p.short_frame_d if p.short_frame_d is not None else p.short_len - p.short_face_depth
        sfd_ = sfd_[i_] if isinstance(sfd_, tuple) else sfd_
        pd_ = p.short_post_d[i_] if p.short_post_d else max(3.2, sfd_ + 0.6)
        extra.append(_cyl(sx_, sz_, p.short_post_r[i_] if p.short_post_r else 3.0, split - pd_, split))
        if rx_ is None:
            continue
        L_ = math.hypot(rx_ - sx_, rz_ - sz_)
        ux_, uz_ = (rx_ - sx_) / L_, (rz_ - sz_) / L_
        wx2, wz2 = -uz_ * 0.6, ux_ * 0.6
        extra.append(_xyz_prism([(sx_ + wx2, sz_ + wz2), (rx_ + wx2, rz_ + wz2), (rx_ - wx2, rz_ - wz2), (sx_ - wx2, sz_ - wz2)],
                                split - p.lip_h, split))
    trim_ = {(round(tx_, 3), round(tz_, 3)): dy_ for tx_, tz_, dy_ in p.post_trims}
    for ex_, ez_, wx_ in p.extra_dowels:   # a boss like the posts' (r 3, 3.2 under the line) + an optional web (1.2 x 2.4)
        extra.append(_cyl(ex_, ez_, 3.0, split - p.mid_post_d + trim_.get((round(ex_, 3), round(ez_, 3)), 0.0), split))
        if wx_ is not None and p.dowel_blocks:   # #18.1: the boss's full width and depth to the bar, stepped to 1.0 over
            cx0_, cy_ = (mx0 + mx1) / 2 + (p.coin_at or (2.0, 0.0))[0] - 5.0, (my0 + tails + pcb) + 3.4   # the coin (x edge, top)
            xs_ = sorted((ex_, wx_))
            xa_ = min(max(xs_[0], cx0_ - 1.0), xs_[1])
            extra.append(_box(xs_[0], xa_, split - 3.2, split, ez_ - 3.0, ez_ + 3.0))
            extra.append(_box(xa_, xs_[1], max(cy_ + 1.0, split - 3.2), split, ez_ - 3.0, ez_ + 3.0))
        elif wx_ is not None:
            extra.append(_box(min(ex_, wx_), max(ex_, wx_), split - 1.2, split, ez_ - 1.2, ez_ + 1.2))
    inner = profile(p, box, Y0).offset2D(-p.wall - p.rim_band - p.print_clr).extrude(Y1 - Y0)
    for e in extra:
        frame = frame.union(e.intersect(inner))
    # dowel holes: into the face from the line up, into the posts from the line down; 0.3 lead-in at each mouth
    pfd_ = p.peg_depth if p.peg_face_d is None else p.peg_face_d
    prd_ = p.peg_depth if p.peg_frame_d is None else p.peg_frame_d
    sfd_ = p.short_frame_d if p.short_frame_d is not None else p.short_len - p.short_face_depth
    holes_ = [(px, pz, pfd_, prd_, p.peg_lead)
              for px, pz in ([] if p.cover_onepiece else posts + [(ex_, ez_) for ex_, ez_, _ in p.extra_dowels])]
    holes_ += [(sx_, sz_, p.short_face_ds[i_] if p.short_face_ds else p.short_face_depth,
                sfd_[i_] if isinstance(sfd_, tuple) else sfd_, p.short_lead) for i_, (sx_, sz_, _, _) in enumerate(p.short_dowels)]
    loc_ = [tuple(round(v, 3) for v in l_) for l_ in p.dowel_locators]
    for px, pz, dface_, dframe_, lead_ in holes_:
        for sgn, half in ((1, 'face'), (-1, 'frame')):
            key_ = (round(px, 3), round(pz, 3))
            cf_ = p.peg_clr_face if p.peg_clr_face is not None else p.peg_clr
            ribbed = sgn > 0 and p.peg_ribs is not None and key_ not in loc_
            if sgn > 0 and p.peg_clr_loose is not None and key_ not in loc_:
                cf_ = p.peg_clr_loose
            if ribbed:
                cf_ = p.peg_ribs[0] / 2 - p.peg_d / 2
            r = p.peg_d / 2 + (cf_ if sgn > 0 else p.peg_clr)
            y_end = split + sgn * (dface_ if sgn > 0 else dframe_)
            L_ = lead_
            if ribbed and p.rib_lead is not None:
                L_ = p.rib_lead
            if sgn < 0 and p.frame_lead is not None:
                L_ = p.frame_lead
            hole = _cyl(px, pz, r, min(split, y_end) - 0.01, max(split, y_end) + 0.01)
            if sgn < 0 and p.frame_neck:  # the stop: a ledge at y_end, the neck through the post below it
                hole = hole.union(_cyl(px, pz, p.frame_neck / 2, y_end - 6.0, y_end + 0.02))
            if ribbed:                  # ribs from the bore wall in to the rib-tip diameter, the full depth
                bore_d, tip_d, rw_, n_ = p.peg_ribs
                for i_ in range(n_):
                    a_ = 2 * math.pi * i_ / n_ + math.pi / 2
                    rib = _box(tip_d / 2, bore_d / 2 + 0.3, split + L_, y_end + 0.02, -rw_ / 2, rw_ / 2)
                    rib = rib.rotate((0, 0, 0), (0, 1, 0), math.degrees(a_)).translate((px, 0, pz))
                    hole = hole.cut(rib)
            if sgn > 0 and len(loc_) > 1 and key_ == loc_[1]:      # the slotted locator: the slot along x
                hole = hole.union(_cyl(px - p.peg_slot, pz, r, split - 0.01, y_end + 0.01)).union(
                    _cyl(px + p.peg_slot, pz, r, split - 0.01, y_end + 0.01)).union(
                    _box(px - p.peg_slot, px + p.peg_slot, split - 0.01, y_end + 0.01, pz - r, pz + r))
            hole = hole.union(_cone(px, pz, 2 * r + 2 * L_, 2 * r, split - 0.01, split + L_) if sgn > 0 else
                              _cone(px, pz, 2 * r, 2 * r + 2 * L_, split - L_, split + 0.01))
            if half == 'face':
                face = face.cut(hole)
            else:
                frame = frame.cut(hole)
    lab = p.proto_label or (f'{p.proto}.3' if p.proto is not None else None)
    if lab and p.proto_label:     # #18.1: the face label reads from the back (18.3a came out mirrored there)
        face = face.cut(cq.Workplane(cq.Plane(origin=(p.face_label_x, split + 0.5, zc), xDir=(0, 0, -1), normal=(0, -1, 0)))
                        .text(lab + 'a', 5.0, 0.51, halign='center', valign='center', kind='bold'))
    elif lab:                 # both on the parting face (the bed face of each half), read from below / from above
        face = face.cut(cq.Workplane(cq.Plane(origin=(140.0, split - 0.01, zc), xDir=(0, 0, -1), normal=(0, 1, 0)))
                        .text(lab + ('' if p.cover_onepiece else 'a'), 5.0, 0.51, halign='center', valign='center', kind='bold'))
    if lab and not p.cover_onepiece:
        frame = frame.cut(cq.Workplane(cq.Plane(origin=(162.0, split + 0.01, zc), xDir=(1, 0, 0), normal=(0, -1, 0)))
                          .text(lab + 'b', 4.5, 0.51, halign='center', valign='center', kind='bold'))
    # P10e (owner photo, 2026-09-29): the lip ring's two free ends at the USB notch curled up and tore off at the
    # end pillars. A 45 degree gusset ties each end to its pillar (in plan, lip height), and a breakaway tie (2 layers,
    # its own body, snapped off after printing: the USB receptacle sits 0.3 under the line) closes the notch while it
    # prints
    tie = None
    if p.frame_tie and not p.cover_onepiece:
        x_out = X0 + p.wall + p.rim_band + p.print_clr    # the lip's outer face at the flat USB end
        x_in = x_out + p.lip_w
        hs = sorted(f['holes'])[:2]                       # the two USB-end pillars
        for hx, hz in hs:
            sgn = 1 if hz < zc else -1                    # toward the notch
            za = hz + sgn * 2.4
            gus = _xyz_prism([(x_in - 0.01, za), (x_in + 3.0, za), (x_in - 0.01, za + sgn * 3.0)], split - p.lip_h, split)
            frame = frame.union(gus.intersect(inner))
        uw, uh, uy, uz = usb_window_rect(p)
        nz0, nz1 = uz - uw / 2, uz + uw / 2               # the notch over the USB receptacle
        wt = p.usb_end_w
        if wt > p.lip_w:          # P10f (owner): the ring beside the notch was thin and nearly tore when peeled: wider
            for za_, zb_ in ((nz0 - 7.0, nz0), (nz1, nz1 + 7.0)):      # locally (inward), where nothing inside is hit
                thick = _box(x_out, x_out + wt, split - p.lip_h, split, za_, zb_)
                xb_ = board_parts()['xiao']                  # (cut one by one: a union of the XIAO compounds fills in)
                thick = thick.cut(xb_).cut(xb_.translate((0.3, 0, 0)))
                frame = frame.union(thick.intersect(inner))
        # round the notch's corners (r 1, in plan): the ring's end faces meet its faces in a curve, not a corner
        rr = 1.0
        for zn, sg in ((nz0, -1), (nz1, 1)):
            for xc_, sx in ((x_out, 1), (x_out + wt, -1)):
                corner = _box(*sorted((xc_, xc_ + sx * rr)), split - p.lip_h - 1, split + 1, *sorted((zn, zn + sg * rr)))
                corner = corner.cut(_cyl(xc_ + sx * rr, zn + sg * rr, rr, split - p.lip_h - 2, split + 2))
                frame = frame.cut(corner)
        tie = _box(x_out + 0.2, x_out + min(wt, 1.6) - 0.2, split - 0.32, split, nz0 - 2.0, nz1 + 2.0)
        # V-notches: the tie necks to 0.4 at 0.3 inside each notch end, so it snaps there and not in the ring
        tw_ = min(wt, 1.6) - 0.4
        for zn, sg in ((nz0, 1), (nz1, -1)):
            zv = zn + sg * 0.3
            for xe, sx in ((x_out + 0.2, 1), (x_out + 0.2 + tw_, -1)):
                v = _xyz_prism([(xe - sx * 0.01, zv - 0.5), (xe + sx * (tw_ - 0.4) / 2, zv), (xe - sx * 0.01, zv + 0.5)],
                               split - 1, split + 1)
                tie = tie.cut(v)
    bb = frame.val().BoundingBox()
    out = dict(seam_y=round(split, 2), posts=posts, peg=(p.peg_d, 2 * p.peg_depth), peg_clr=p.peg_clr,
               dowels=[(round(x_, 2), round(z_, 2)) for x_, z_ in posts] + [(ex_, ez_) for ex_, ez_, _ in p.extra_dowels],
               short_dowels=[(sx_, sz_) for sx_, sz_, _, _ in p.short_dowels],
               hold_down=dict(x=round(bar_x, 2), gap=0.3, under=round(mtop + 0.3, 2)),
               frame_solids=sum(len(v.Solids()) for v in frame.vals()))
    if p.face_pockets is not None:
        face, out['pockets'] = _face_pockets(p, face, box, split, k, f, posts)
    if p.face_back_chamfer:       # P10e: a serrated bed edge (textured PEI + elephant foot); 45 degrees round the back
        cb = p.face_back_chamfer
        keep = _wall_loft(p, box, split - 0.01, cb + 0.01, split + cb, 0.0).union(
            profile(p, box, split + cb - 0.001).extrude(30))
        face = face.intersect(keep)
        # the frame's lip: 0.3 on its outer bed edge (it slides into the tray wall; the first layer grows)
        lo = p.wall + p.rim_band + p.print_clr
        cl = 0.3
        keep_f = _wall_loft(p, box, split - cl, lo, split + 0.01, lo + cl + 0.01).union(
            profile(p, box, Y0 - 5).offset2D(-lo).extrude(split - cl - (Y0 - 5) + 0.001))
        frame = frame.intersect(keep_f)
    if p.face_screws:             # #22: hidden self-tapping screws frame -> face
        face, frame, out['face_screws'] = _face_screws(p, face, frame, box, split)
    if p.motor_conn_xz is not None:   # #18.1c: the pocket over the motor's connector (the plug stood 2+ mm into a flat back)
        face, out['plug_pocket'] = _plug_pocket(p, face, split, k)
    if p.face_bosses:             # #20: after the back chamfer (it would trim anything under the line)
        face, frame, out['bosses'] = _face_bosses(p, face, frame, box, split, f)
    out['tie'] = None if tie is None else dict(thick=0.32, span=round(usb_window_rect(p)[0] + 4.0, 1))
    return face, frame, tie, out


def _face_pockets(p: P, face, box, split, k, f, posts):
    """Pockets in 10.3a's back (owner's H sketch, 2026-09-29): everything but a rim band, a central island round the
    label and the two dowels (the stiffening spine), the LED spot, the pillar tops and the frame's bar / spine contacts is
    hollowed as deep as the crown allows: the ceiling stays face_pockets under the outer surface (vertically), in
    pocket_step terraces. Ribs 1.2 run along the four facet creases and along the middle of each side strip, so every
    ceiling is a bridge of a few mm between ribs (it prints crown up, the back on the bed)."""
    X0, X1, Y0, Y1, Z0, Z1 = box
    zc = (Z0 + Z1) / 2
    skin, st = p.face_pockets, p.pocket_step
    top = split + 8
    allowed = profile(p, box, split - 1).offset2D(-p.pocket_rim).extrude(top - split + 1)
    keep = None

    def add(w):
        nonlocal keep
        keep = w if keep is None else keep.union(w)
    r_d = p.peg_d / 2 + p.peg_clr + 2.0 + 0.3          # >= 2.0 wall round each dowel hole (+ its 0.3 lead-in)
    for px, pz in posts:
        add(_cyl(px, pz, r_d, split - 1, top))
    xs = [px for px, _ in posts]
    add(_box(min(xs) - r_d, max(xs) + r_d, split - 1, top, zc - 1.5, zc + 1.5))       # the spine between the dowels
    add(_box(136.0, 144.5, split - 1, top, zc - 8.5, zc + 8.5))                          # the label island
    add(_box(136.0, min(xs), split - 1, top, zc - 1.5, zc + 1.5))                        # label to the spine
    Ld = MEASURED['xiao_rgb_led']
    add(_cyl(sum(Ld['x']) / 2, sum(Ld['z']) / 2, 3.2, split - 1, top))                  # the LED spot
    for hx, hz in f['holes']:                                                             # pillar tops
        add(_cyl(hx, hz, p.pillar_d_top / 2 + 1.0, split - 1, top))
    mx0, mx1 = k['parts']['motor'][:2]
    bx = (mx0 + mx1) / 2
    add(_box(bx - 3.0, bx + 3.0, split - 1, top, -200, 200))                             # the frame's hold-down bar
    add(_box(171.0, 173.0, split - 1, top, -200, 200))                                    # the frame's z spine
    d = diamond_params(p, box)
    xc, a, b = d['xc'], d['a'], d['b']
    L = 200.0
    for c1, c2 in ((1, 1), (1, -1), (-1, 1), (-1, -1)):      # the crease lines: z/b +- x/a = +-1 (u = z - zc, v = x - xc)
        # a point on the line and its direction: u/b + s v/a = c
        s_, c_ = c2, c1
        p0 = (xc, zc + c_ * b)                              # v = 0 -> u = c b
        dv, du = 1.0, -s_ * b / a                           # direction (x, z)
        n_ = math.hypot(dv, du); dv, du = dv / n_, du / n_
        wx, wz = -du * 0.6, dv * 0.6
        pts = [(p0[0] - dv * L + wx, p0[1] - du * L + wz), (p0[0] + dv * L + wx, p0[1] + du * L + wz),
               (p0[0] + dv * L - wx, p0[1] + du * L - wz), (p0[0] - dv * L - wx, p0[1] - du * L - wz)]
        add(_xyz_prism(pts, split - 1, top))
    inner_z0, inner_z1 = Z0 + p.pocket_rim, Z1 - p.pocket_rim
    for zr in ((zc + 8.5 + inner_z1) / 2, (zc - 8.5 + inner_z0) / 2):                   # mid-strip ribs
        add(_box(X0, X1, split - 1, top, zr - 0.6, zr + 0.6))
    region = allowed.cut(keep)
    cav = None
    Lv = st
    while True:
        probe = split + Lv + skin
        sec_ = face.intersect(_box(X0 - 5, X1 + 5, probe - 0.01, probe + 0.01, -100, 100))
        sols = [x for v in sec_.vals() for x in v.Solids()]
        if not sols:
            break
        col = None
        for so in sols:
            for fc in [fc for fc in so.Faces() if fc.normalAt().y < -0.9]:
                f2 = fc.translate(cq.Vector(0, (split - 0.5) - fc.Center().y, 0))
                c_w = cq.Workplane().add(cq.Solid.extrudeLinear(f2.outerWire(), f2.innerWires(), cq.Vector(0, Lv + 0.5, 0)))
                col = c_w if col is None else col.union(c_w)
        if col is None:
            break
        cav = col if cav is None else cav.union(col)
        Lv += st
    if cav is None:
        return face, dict(skin=skin, volume=0.0)
    cav = cav.intersect(region)
    v0 = sum(v.Volume() for v in face.vals())
    face = face.cut(cav)
    v1 = sum(v.Volume() for v in face.vals())
    return face, dict(skin=skin, step=st, max_depth=round(Lv - st, 2), removed_cm3=round((v0 - v1) / 1000, 2),
                      face_cm3=round(v1 / 1000, 2))


def _shelf_ribs(p: P, shelf, box, k, pitch=7.0, t=0.45, gap=0.2):
    """10.2C prints top up (the 11.2 posts up) with the 13.2B fence on the bed: the plate hangs 2.0 over the bed. Ribs
    along x every `pitch` across z, one line thick, from the bed up to `gap` under the plate (the plate's first layer
    bridges from rib to rib and lifts off them); inside the plate outline by 0.8 and clear of the fence by 0.6. Their own
    body: snapped off after printing. The shelf's part number moves to its top (the underside prints on the ribs)."""
    sh = k['parts']['shelf']
    sy0, sy1 = sh[2], sh[3]
    bb = shelf.val().BoundingBox()
    ybed = bb.ymin                                        # the fence bottom
    fence_x1 = sh[0] + (p.shelf_edge_clr if p.shelf_edge_clr is not None else p.print_clr) + p.fence_t
    foot = profile(p, box, ybed).offset2D(-p.wall - (p.shelf_clr or p.print_clr) - 0.8).extrude(sy0 - gap - ybed)
    foot = foot.intersect(_box(fence_x1 + 0.6, 400, ybed - 1, sy0, -200, 200))
    zc = (box[4] + box[5]) / 2
    ribs = None
    for i in range(-8, 9):
        z = zc + i * pitch
        r = foot.intersect(_box(0, 400, ybed - 1, sy0, z - t / 2, z + t / 2))
        if any(v.Volume() > 0.5 for v in r.vals()):
            ribs = r if ribs is None else ribs.union(r)
    # a rib round the outline (0.8 inside it): no plate edge hangs more than 0.8 past a rib
    inset = p.wall + (p.shelf_clr or p.print_clr) + 0.8
    ring = foot.cut(profile(p, box, ybed - 1).offset2D(-inset - t).extrude(sy0 - ybed + 2))
    ribs = ribs.union(ring)
    # a cross rib along z near the fence and one near the far end tie the ribs so they stand
    for xr in (fence_x1 + 0.6 + t / 2, (fence_x1 + bb.xmax) / 2):
        r = foot.intersect(_box(xr - t / 2, xr + t / 2, ybed - 1, sy0, -200, 200))
        ribs = r if ribs is None else ribs.union(r)
    # the number on top (debossed 0.5), beside the label, not on the bridged underside
    if p.proto is not None:
        mx0, mx1, my0, my1, mz0, mz1 = k['parts']['motor']
        t_ = (cq.Workplane(cq.Plane(origin=((mx0 + mx1) / 2, sy1 + 0.01, mz1 + 6.5), xDir=(0, 0, 1), normal=(0, 1, 0)))
              .text(f'{p.proto}.2{p.part_suffix}', 4.5, -0.51, halign='center', valign='center', kind='bold'))
        shelf = shelf.cut(t_)
    return shelf, ribs, dict(pitch=pitch, t=t, gap=gap, height=round(sy0 - gap - ybed, 2),
                             count=sum(len(v.Solids()) for v in ribs.vals()))


def _board_locate(p: P, tray, k, cavity):
    """The board located by its edge (audit: M2 in 3.0 holes floats +-0.5, so D1 could be pinned or out of reach).
    -z: two pads switch_wall_clr thick on the switch wall, clear of the switch; the board edge rests on them.
    +z: two crush ribs, crush into the board edge, clear of RESET, the Grove sockets and D1; a 45 degree lead-in on
    top. Both run from the floor to 0.5 over the PCB, so they print as wall ribs."""
    M = MEASURED
    x0, x1, y0, y1, z0, z1 = k['cavity']
    bz0, bz1 = M['board_z']
    pb, pt = M['pcb_y']
    top = pt + 0.5
    out = dict(pads=[], ribs=[])
    t = bz0 - z0                                   # the pad thickness: the board edge on the pad face
    for xc in (125.0, 172.0):
        pad = _box(xc - 1.0, xc + 1.0, y0 - 0.5, top, z0 - 0.5, bz0)
        pad = pad.cut(_prism_yz([(top - t - 0.01, bz0 + 0.01), (top + 0.01, bz0 + 0.01), (top + 0.01, bz0 - t - 0.01)],
                                xc - 2, xc + 2))
        tray = tray.union(pad)
        out['pads'].append(dict(x=xc, t=round(t, 2)))
    zf = bz1 - p.crush
    for xc in (137.0, 176.5):             # clear of RESET, the Grove sockets, D1 and the +z pillar (cable)
        d = z1 - zf
        rib = _box(xc - 0.75, xc + 0.75, y0 - 0.5, top, zf, z1 + 0.5)
        rib = rib.cut(_prism_yz([(top - d - 0.01, zf - 0.01), (top + 0.01, zf - 0.01), (top + 0.01, zf + d + 0.01)],
                                xc - 2, xc + 2))
        tray = tray.union(rib)
        out['ribs'].append(dict(x=xc, depth=round(d, 2), crush=p.crush))
    return tray, out


def _loop_fins(p: P, box, lp):
    """Two 45 degree fins under the loop's side arms, clear of the ring hole: the tray prints floor down and the lug
    then grows out of the end wall. The span between the fin tips at the ring's end is a short bridge."""
    X0, X1, Y0, Y1, Z0, Z1 = box
    zc, yc = (Z0 + Z1) / 2, (Y0 + Y1) / 2
    yb = yc - p.loop_t / 2
    hr = lp['hole_r']
    wa = max(p.loop_w, 2 * (hr + p.loop_web)) / 2          # the arms' outer edge off the centre line
    L = lp['end_x'] - (X1 - 1.0)
    fins = None
    for sz in (-1, 1):
        za_, zb_ = sorted((zc + sz * (hr + 0.3), zc + sz * wa))
        fin = _xyp([(X1 - 1.0, yb + 0.01), (lp['end_x'], yb + 0.01), (X1 - 1.0, yb - L)], za_, zb_)
        fins = fin if fins is None else fins.union(fin)
    # down to the bed: at the wall the fins stand on the plate; clipped to the ring's outline in plan
    ring = (_box(X1 - 6.0, lp['hole_x'], Y0 - 1, yb + 1, zc - wa, zc + wa)
            .union(_cyl(lp['hole_x'], zc, wa, Y0 - 1, yb + 1)))
    return fins.intersect(_box(X1 - 2, lp['end_x'] + 1, Y0, yb + 0.02, -200, 200)).intersect(ring)


def bay_hold(p: P, tray, shelf, box, k, cavity):
    """Cell and motor retention in the bay (v1.1 follow-up). Returns (tray, shelf, numbers).
    Tray: rigid cell end pads on the floor, a detent bump on the rib top, a far lip over the shelf's far edge.
    Shelf (prints standing on its rib-side edge, no support): slides home 'shelf_slide' toward the loop under the far
    lip and clicks past the detent; a leaf with a hook presses the cell's -z top corner down and toward +z; rails with
    top lips take the 20 x 20 motor module, which slides in from the rib side; a raised orientation mark."""
    X0, X1, Y0, Y1, Z0, Z1 = box
    x0, x1, y0, y1, z0, z1 = k['cavity']
    lp = k['parts']['lipo']
    sh = k['parts']['shelf']
    bx0, sy0, sy1 = sh[0], sh[2], sh[3]
    c = p.print_clr
    out = {}
    # ---- pads: both rigid. +z (protection-board end) takes the cell; -z stops the longest cell. The shelf's hook
    # pushes the cell's -z top corner down and toward +z, so the cell sits on the floor against the +z pad; lifting its
    # +z end would push that end into the pad, so it cannot.
    zp = lp[5] + p.clr                                   # +z pad face: the cell's protection-board end
    z_rigid = zp - (p.bay_len or p.cell_len[1])          # #10: rigid bay, the longest cell + 0.3
    reg = (profile(p, box, y0).offset2D(-p.wall - p.pad_wall_clr).extrude(p.pad_h)
           .intersect(_box(p.pad_x0, X1 + 5, y0 - 1, y0 + p.pad_h + 1, -200, 200)))
    pad_p = reg.intersect(_box(0, 300, -50, 50, zp, 200))
    pad_m = reg.intersect(_box(0, 300, -50, 50, -200, z_rigid))
    tray = tray.union(pad_p).union(pad_m)
    out['pads'] = dict(plus_z_face=round(zp, 2), minus_z_face=round(z_rigid, 2), height=p.pad_h, cell_len=p.cell_len,
                       bay=round(zp - z_rigid, 2), play=tuple(round(zp - z_rigid - L, 2) for L in p.cell_len))
    E = DATASHEET['pla_E_xy']
    # ---- detent bump on the rib top (clear of the lead notch), and the far lip over the shelf edge
    M = MEASURED
    nz0 = M['jst']['z'][0]
    dh = p.detent_h
    for za, zb in (((-12.0, -4.0),) if p.bay_detent else ()):
        assert zb < nz0
        tray = tray.union(_xyp([(bx0 - 0.9, sy0 - 0.01), (bx0 - 0.9 + dh, sy0 + dh), (bx0, sy0 + dh), (bx0, sy0 - 0.01)], za, zb))
    lipfree = None
    if p.far_lip:
        lw = p.shelf_lip_w
        ly0 = sy1 + 0.15
        ring = (profile(p, box, ly0 - lw).offset2D(-p.wall + 0.01).extrude(lw + 0.8)
                .cut(profile(p, box, ly0 - lw - 1).offset2D(-p.wall - c - lw).extrude(lw + 3)))
        # 45 degree underside: a loft from the wall line at ly0 - lw ... simplified: the lip is the ring above ly0 plus a
        # chamfer body under it (lw deep at the wall, 0 at the lip edge)
        lip = ring.intersect(_box(0, 300, ly0, ly0 + 0.8, -200, 200))
        cham = (profile(p, box, ly0 - lw).offset2D(-p.wall + 0.01).extrude(lw)
                .cut(cq.Workplane().add(cq.Solid.makeLoft([profile(p, box, ly0 - lw).offset2D(-p.wall).vals()[0],
                                                            profile(p, box, ly0).offset2D(-p.wall - c - lw).vals()[0]],
                                                           ruled=True))))
        zl = (z0 + z1) / 2 if p.lip_center else 0.0
        far = _box(lp[1] - 2.0, X1 + 5, 0, 30, zl - p.shelf_lip_z, zl + p.shelf_lip_z)
        tray = tray.union(lip.union(cham).intersect(far))
        out['far_lip'] = dict(width=lw, y=(round(ly0, 2), round(ly0 + 0.8, 2)), z=(round(zl - p.shelf_lip_z, 2), round(zl + p.shelf_lip_z, 2)))
        # ---- shelf: far edge wedge (it slides under the lip), spring tongue, rails, mark.
        # The lip underside runs from the wall line at ly0 - lw out to (lw + c) inside it at ly0; the shelf keeps
        # 'lip_gap' under that slope, so the lip holds the shelf down while the detent holds it home.
        gap = 0.2
        slope = lambda y: -p.wall - (c + lw) * (y - (ly0 - lw)) / lw - gap * 1.414
        ya, yb = sy0 - 0.01, sy1 + 0.01
        keep = cq.Workplane().add(cq.Solid.makeLoft([profile(p, box, ya).offset2D(min(slope(ya), -p.wall - c)).vals()[0],
                                                     profile(p, box, yb).offset2D(slope(yb)).vals()[0]], ruled=True))
        farg = _box(lp[1] - 2.0 - gap, X1 + 5, 0, 30, zl - p.shelf_lip_z - gap, zl + p.shelf_lip_z + gap)
        shelf = shelf.cut(profile(p, box, ya).offset2D(-p.wall + 0.5).extrude(yb - ya).cut(keep).intersect(farg))
        lipfree = (profile(p, box, sy1 - 1).offset2D(-p.wall - c).extrude(20)
                   .cut(profile(p, box, sy1 - 2).offset2D(-p.wall - c).extrude(25)
                        .cut(profile(p, box, sy1 - 2).offset2D(-p.wall - c - lw - gap).extrude(25))
                        .intersect(_box(0, 300, -50, 50, zl - p.shelf_lip_z - gap, zl + p.shelf_lip_z + gap))))
    la = bx0 + c
    lb = p.leaf_x[1]
    lz0, lz1 = p.leaf_z
    sl = p.leaf_slit
    if p.bay_leaf:
        # the leaf: the shelf plate itself over x leaf_x, z leaf_z, cut free by a side slit and a tip slit
        la = bx0 + c
        lb = p.leaf_x[1]
        lz0, lz1 = p.leaf_z
        sl = p.leaf_slit
        shelf = shelf.cut(_box(lb, lb + sl, sy0 - 1, sy1 + 1, lz0 - sl, lz1))
        shelf = shelf.cut(_box(la - 1, lb + sl, sy0 - 1, sy1 + 1, lz0 - sl, lz0))
        # the hook under the free end: its 45 degree face passes the shortest, thinnest cell's -z top corner hook_pre
        # (normal) inside it; the face rises toward +z, so it only touches the corner. A 45 degree lead toward the rib.
        ze, te = zp - p.cell_len[0], p.cell_t[0]
        off = te - ze - p.hook_pre * 2 ** 0.5                 # face: y - y0 = z + off
        zt = sy0 - y0 - off                                   # where the face meets the leaf underside
        yb = y0 + lz0 + off                                   # hook bottom at its -z face
        pl = cq.Plane(origin=(lb, 0, 0), xDir=(0, 0, 1), normal=(-1, 0, 0))     # local (u, v) = (z, y), extrudes toward -x
        hook = (cq.Workplane(pl).polyline([(lz0, yb), (lz0, sy0 + 0.01), (zt, sy0 + 0.01)]).close()
                .extrude(lb - p.hook_x0))
        hook = hook.cut(_xyp([(p.hook_x0 - 20, sy0 + 0.02), (p.hook_x0, sy0 + 0.02), (p.hook_x0 + 20, sy0 - 20),
                              (p.hook_x0 - 20, sy0 - 20)], lz0 - 1, zt + 1))
        shelf = shelf.union(hook)
        # leaf mechanics: cantilever from the root to the contact (about the cell's -z end), bending in y; PLA in plane
        Ll = lz1 - (ze + z_rigid) / 2
        Il = (lb - la) * p.shelf ** 3 / 12
        kl = 3 * E * Il / Ll ** 3
        d0 = p.hook_pre * 2 ** 0.5                            # lift at the shortest, thinnest cell
        d1 = (p.cell_t[1] - z_rigid) - (te - ze) + d0         # lift at the longest, thickest cell
        out['leaf'] = dict(L=round(Ll, 1), w=round(lb - la, 1), t=p.shelf, k_N_per_mm=round(kl, 2),
                           lift_mm=(round(d0, 2), round(d1, 2)), force_N=(round(kl * d0, 2), round(kl * d1, 2)),
                           strain_max_pct=round(1.5 * d1 * p.shelf / Ll ** 2 * 100, 2),
                           hook_bottom_over_floor=round(yb - y0, 2), hook_face_z=(round(lz0, 2), round(zt, 2)))
    # rails: under-edge support at the PCB underside, fence, top lip over the PCB top (45 degree underside);
    # +z side: the top lip only outside the Grove socket; +x: an end stop; -x: open (the module slides in from there)
    mx0, mx1, my0, my1, mz0, mz1 = k['parts']['motor']
    tails = DATASHEET['motor_split'][0]
    yp = my0 + tails                                       # PCB underside
    yt = yp + DATASHEET['motor_split'][1]                  # PCB top
    rc, rl = p.rail_clr, p.rail_lip
    rails = None
    sc = p.print_clr if p.shelf_clr is None else p.shelf_clr
    if p.motor_posts:             # #11 seat, exactly (owner 2026-09-28: "perfect"): posts hug the 20 x 20 PCB at print_clr
        posts = None
        for (cx, sx) in ((mx0, 1), (mx1, -1)):
            for (cz, sz) in ((mz0, 1), (mz1, -1)):
                q, fe = p.motor_post, p.fence
                xa, xb = sorted((cx - sx * fe, cx + sx * q)); za, zb = sorted((cz - sz * fe, cz + sz * q))
                post = _box(xa, xb, sy1 - 0.01, yp + p.fence_h, za, zb)
                post = post.cut(_box(mx0 - p.print_clr, mx1 + p.print_clr, yp, yp + 20, mz0 - p.print_clr, mz1 + p.print_clr))
                posts = post if posts is None else posts.union(post)
        if p.post_keepout:
            d_ = p.post_keepout
            for vx, vz in ((d_, 0), (-d_, 0), (0, d_), (0, -d_), (d_ * 0.7071, d_ * 0.7071), (d_ * 0.7071, -d_ * 0.7071),
                           (-d_ * 0.7071, d_ * 0.7071), (-d_ * 0.7071, -d_ * 0.7071)):
                posts = posts.cut(tray.translate((vx, 0, vz)))
        if p.seat_split:          # 10.4: the same posts on a ring, sunk seat_groove into the shelf top (option A)
            g_, fe, gc = p.seat_groove, p.fence, 0.15
            ox0, ox1, oz0, oz1 = mx0 - fe, mx1 + fe, mz0 - fe, mz1 + fe
            ring = _box(ox0, ox1, sy1 - g_, sy1 + 0.8, oz0, oz1).cut(_box(ox0 + 1.0, ox1 - 1.0, sy1 - 5, sy1 + 5, oz0 + 1.0, oz1 - 1.0))
            tab = _box(mx0 + 6.0, mx0 + 14.0, sy1 - g_, sy1 + 0.8, oz1 - 0.5, oz1 + 5.0)
            seat = posts.union(ring).union(tab)
            # the posts' feet go down into the groove too: their bottom g_ copied g_ lower
            seat = seat.union(posts.intersect(_box(0, 400, sy1 - 0.01, sy1 + g_, -200, 200)).translate((0, -g_ + 0.005, 0)))
            seat = seat.union(cq.Workplane(cq.Plane(origin=(mx0 + 10.0, sy1 + 0.79, oz1 + 2.25), xDir=(1, 0, 0), normal=(0, 1, 0)))
                              .text(f'{p.proto}.4' if p.proto is not None else 'ВЕРХ', 3.6, 0.51, halign='center', valign='center', kind='bold'))
            # the groove: the seat's footprint at the shelf top, gc all round
            foot = seat.intersect(_box(0, 400, sy1 - g_ - 0.01, sy1 + 0.02, -200, 200))
            gcut = None
            for dx, dz in ((gc, 0), (-gc, 0), (0, gc), (0, -gc), (gc * 0.7, gc * 0.7), (-gc * 0.7, gc * 0.7),
                           (gc * 0.7, -gc * 0.7), (-gc * 0.7, -gc * 0.7)):
                t_ = foot.translate((dx, 0, dz))
                gcut = t_ if gcut is None else gcut.union(t_)
            shelf = shelf.cut(gcut)
            out['seat_body'] = seat
            out['seat'] = dict(groove=g_, groove_clr=gc, ring=(1.0, round(g_ + 0.8, 2)), label=f'{p.proto}.4' if p.proto is not None else None)
        else:
            shelf = shelf.union(posts)
    if p.ledge_cell_trim:         # where the thickest cell lies, clear of the fence: the side ledges and the floor-corner
        # fill in the curved end are trimmed (+ clr across, exact along the cell, which sits against the +z pad)
        xf = bx0 + (p.shelf_edge_clr if p.shelf_edge_clr is not None else sc) + p.fence_t + 0.1
        L1, T1 = p.cell_len[1], p.cell_t[1]
        zp_ = lp[5] + p.clr
        cellg = _box(xf - p.clr, xf + 20.0 + p.clr, y0 - 0.001, y0 + T1 + p.clr, zp_ - L1, zp_ - 0.001)
        cut_ = tray.intersect(cellg).intersect(cavity)
        trimmed = sum(v.Volume() for v in cut_.vals())
        out['ledge_trim'] = dict(volume=round(trimmed, 2))
        if trimmed > 1e-4:
            for so in [x_ for v in cut_.vals() for x_ in v.Solids()]:
                bbt = so.BoundingBox()
                out['ledge_trim'].setdefault('pieces', []).append(dict(v=round(so.Volume(), 2), x=(round(bbt.xmin, 2), round(bbt.xmax, 2)),
                                                                        y=(round(bbt.ymin, 2), round(bbt.ymax, 2)), z=(round(bbt.zmin, 2), round(bbt.zmax, 2))))
            tray = tray.cut(cellg.intersect(cavity))
    if not p.motor_posts:
        xcm = (mx0 + mx1) / 2
        for side, zin in ((-1, mz0 - rc), (+1, mz1 + rc)):
            zo = zin + side * 1.2
            za, zb = sorted((zin, zo))
            xa = bx0 + sc if p.motor_retain else max(mx0 - 0.5, bx0 + c)    # never past the shelf edge: it prints on it
            xb = mx1 + rc
            fence = _box(xa, xb, sy1 - 0.01, yt + rc + 0.8, za, zb)
            sup = _box(xa, xb, sy1 - 0.01, yp, *sorted((zin, zin - side * 0.8)))
            # the support's lower face runs down to the shelf: it is a wall 0.8 thick under the PCB edge, clear of the pins
            lip = None
            spans = [(xa, xb)] if side < 0 else [(xa, mx0 + 4.5), (mx1 - 4.5, xb)]
            for sa, sb in spans:
                pl = cq.Plane(origin=(sa, 0, 0), xDir=(0, 1, 0), normal=(1, 0, 0))
                tri = cq.Workplane(pl).polyline([(yt + rc, zin), (yt + rc + 0.8, zin), (yt + rc + 0.8, zin - side * rl)]).close() \
                    .extrude(sb - sa)
                lip = tri if lip is None else lip.union(tri)
            r = fence.union(sup).union(lip)
            if p.motor_retain:
                if side > 0:          # audit: the plug overlaps the fence in front of the Grove socket: open it there
                    xg0, xg1, H = xcm - p.socket_gap / 2, xcm + p.socket_gap / 2, yt + rc + 2 - yp
                    # the +x end of the gap rises at 45 degrees: the shelf prints on its -x edge
                    r = r.cut(_xyp([(xg0, yp), (xg1, yp), (xg1 + H, yp + H), (xg0, yp + H)], *sorted((zin - side * 1.0, zo + side * 1.0))))
                # -x: a click bump on the fence's inner face just past the PCB's -x edge (45 degree faces both ways)
                hb = rc + 0.1                                  # 0.1 into the PCB outline: the fence flexes that much
                bx = max(mx0 - 0.1, bx0 + sc + 2 * hb)         # never past the shelf edge (it prints standing on it)
                r = r.union(_xyz_prism([(bx, zin), (bx - hb, zin - side * hb), (bx - 2 * hb, zin), (bx - 2 * hb, zin + side * 0.5),
                                        (bx, zin + side * 0.5)], yp, yt + rc))
                # +x: a real stop at each PCB corner, 1.2 thick, 0.6 over the PCB edge (prints as a 0.8 ledge on the
                # fence); the middle stays open for the module's ear
                r = r.union(_box(mx1 + rc, mx1 + rc + 1.2, sy1 - 0.01, yt + rc + 0.8, *sorted((zo, zin - side * 0.8))))
            rails = r if rails is None else rails.union(r)
        if not p.motor_retain:
            stop = _box(mx1 + rc, mx1 + rc + 1.2, sy1 - 0.01, yt + rc + 0.8, mz0 - rc - 1.2, mz1 + rc + 1.2)
            rails = rails.union(stop)
        shelf = shelf.union(rails.intersect(lipfree))
    E_ = DATASHEET['pla_E_xy']
    hf = yt + rc + 0.8 - sy1
    out['motor'] = dict(seat='#11 posts' if p.motor_posts else 'rails', pcb=(round(mx1 - mx0, 1), round(mz1 - mz0, 1)), x=(round(mx0, 2), round(mx1, 2)), z=(round(mz0, 2), round(mz1, 2)),
                        pcb_underside_over_shelf=round(yp - sy1, 2), pins_over_shelf=round(yp - p.motor_pins - sy1, 2),
                        lip=rl, clr=rc, socket_gap=p.socket_gap if p.motor_retain else None,
                        click=dict(interference=0.1, fence_h=round(hf, 2), strain_pct=round(1.5 * 1.2 * 0.1 / hf ** 2 * 100, 2))
                        if p.motor_retain and not p.motor_posts else None)
    if p.top_label:               # #10: ВЕРХ|МОТОР + arrow, raised on the plate beside the rails
        shelf, out['mark'] = _top_label(p, shelf, box, sy0, sy1, bx0, (mz0 - rc - 2.2, mz1 + rc + 2.2))
    else:
        # orientation mark, raised on the shelf top: an arrow toward the loop on the leaf's hook end (stiff there
        # anyway) and TOP on the plate beside the leaf, reading left to right with the loop up
        zt = zt if p.bay_leaf else lz0 + 5.0
        zc = (lz0 + zt) / 2
        hgt = min(zt - lz0 - 0.6, 4.4)
        pl = cq.Plane(origin=(0, sy1 - 0.01, 0), xDir=(1, 0, 0), normal=(0, 1, 0))
        xa0 = la + 1.0
        arrow = cq.Workplane(pl).polyline([(xa0, -(zc - hgt * 0.18)), (xa0 + 5.5, -(zc - hgt * 0.18)), (xa0 + 5.5, -(zc - hgt * 0.5)),
                                           (xa0 + 9.0, -zc), (xa0 + 5.5, -(zc + hgt * 0.5)), (xa0 + 5.5, -(zc + hgt * 0.18)),
                                           (xa0, -(zc + hgt * 0.18))]).close().extrude(p.mark_h + 0.01)
        tz_c = (lz0 + lz1) / 2
        txt = (cq.Workplane(cq.Plane(origin=(lb + sl + 3.3, sy1 - 0.01, tz_c), xDir=(0, 0, 1), normal=(0, 1, 0)))
               .text('TOP', 4.0, p.mark_h + 0.01, halign='center', valign='center', kind='bold'))
        shelf = shelf.union(arrow).union(txt)
        out['mark'] = dict(arrow=(round(xa0, 1), round(xa0 + 9.0, 1), round(zc, 1)), text=('TOP', round(lb + sl + 3.3, 1), round(tz_c, 1)))
    if p.under_fence:             # #13 result: tabs under the rib-side edge stop the shelf sliding back
        shelf, out['under_fence'] = _under_fence(p, shelf, box, k, bx0, sy0)
    if p.rib_teeth:               # the #13 fence's job in the tray: teeth on the partition top at the cell ends; the shelf
        tw_, th_ = p.rib_teeth    # (flat, #11 seat on top) stops against them, 0.3 play (shelf_clr)
        za_, zb_ = z0 + p.ledge_w + 0.2, z1 - p.ledge_w - 0.2
        cz0_, cz1_ = lp[4] - 0.3, lp[5] + 0.3
        spans_ = [(max(za_, cz0_ - tw_), cz0_), (cz1_, min(zb_, cz1_ + tw_))]
        for a_, b_ in spans_:
            tray = tray.union(_box(bx0 - p.rib_t, bx0, sy0 - 0.01, sy0 + th_, a_, b_).intersect(cavity))
        out['rib_teeth'] = dict(spans=[(round(a_, 2), round(b_, 2)) for a_, b_ in spans_], height=th_, play=sc)
    if p.shelf_key:               # audit: a key on the +z ledge wall; the shelf's notch is on that side only, so an
        kl, kd = p.shelf_key      # upside-down shelf (notch on -z) stands on the key
        ka = bx0 + sc + 4.0
        tray = tray.union(_box(ka, ka + kl, sy0 - 0.01, sy1, z1 - kd, z1 + 0.5).intersect(cavity))
        # the notch: key + sc all round, and a 45 degree +x side (the shelf prints on its -x edge)
        nd = kd + sc
        shelf = shelf.cut(_xyz_prism([(ka - sc, z1 + 1), (ka - sc, z1 - nd), (ka + kl + sc, z1 - nd),
                                      (ka + kl + sc + nd + 1, z1 + 1)], sy0 - 1, sy1 + 1))
        out['key'] = dict(x=(round(ka, 2), round(ka + kl, 2)), depth=kd, notch=(round(ka - sc, 2), round(ka + kl + sc, 2), round(nd, 2)))
    if p.nail_dip:                # a 45 degree chamfer under the shelf edge over the tray's nail dip
        zc_ = (z0 + z1) / 2 - 6.0
        e = sc + 0.8
        shelf = shelf.cut(_xyp([(bx0, sy0 - 0.01), (bx0 + e, sy0 - 0.01), (bx0, sy0 + e)], zc_ - 3.0, zc_ + 3.0))
    return tray, shelf, out


def _under_fence(p: P, shelf, box, k, bx0, y_under):
    """A fence or tabs hanging from the shelf underside along its partition-side edge (the #13 variants).
    under_fence = (mode, height, tab_w): 'full' = the whole edge with a gap over the lead notch, 'ends' = one tab at
    each end past the cell, 'ends+mid' = those plus one over the cell's middle. Its -x face is the shelf edge, 0.2 off
    the partition's battery-side face."""
    mode, h, tw = p.under_fence
    x0, x1, y0, y1, z0, z1 = k['cavity']
    lp = k['parts']['lipo']
    M = MEASURED
    xa = bx0 + (p.shelf_edge_clr if p.shelf_edge_clr is not None else (p.print_clr if p.shelf_clr is None else p.shelf_clr))
    xb = xa + p.fence_t
    za, zb = z0 + p.ledge_w + 0.2, z1 - p.ledge_w - 0.2        # clear of the side ledges
    cz0, cz1 = lp[4] - 0.3, lp[5] + 0.3                                # the cell envelope's ends + 0.3
    nz0, nz1 = M['jst']['z'][0] - 0.5, M['jst']['z'][0] + p.lead_notch[0] + 0.5
    spans = []
    if mode == 'full':
        spans = [(za, nz0), (nz1, zb)]
    else:
        spans = [(max(za, cz0 - tw), cz0), (cz1, min(zb, cz1 + tw))]
        if mode == 'ends+mid':
            zm = (lp[4] + lp[5]) / 2
            spans.append((zm - tw / 2, zm + tw / 2))
    fence = None
    yb, c = y_under - h, p.fence_lead
    for a, b in spans:
        if b - a < 0.5:
            continue
        f = _box(xa, xb, yb, y_under + 0.01, a, b)
        if c:                    # lead-in: the free edge on the partition side, 45 degrees
            f = f.cut(cq.Workplane('XY').polyline([(xa - 0.01, yb - 0.01), (xa + c, yb - 0.01), (xa - 0.01, yb + c)])
                      .close().extrude(b - a + 0.2).translate((0, 0, a - 0.1)))
        if p.fence_lead_b:       # the battery-side bottom edge, 45 degrees: a cell partly under the fence is pushed clear
            cb = p.fence_lead_b
            f = f.cut(cq.Workplane('XY').polyline([(xb + 0.01, yb - 0.01), (xb - cb, yb - 0.01), (xb + 0.01, yb + cb + 0.01)])
                      .close().extrude(b - a + 0.2).translate((0, 0, a - 0.1)))
        fence = f if fence is None else fence.union(f)
    return shelf.union(fence), dict(mode=mode, h=h, tab_w=tw, t=p.fence_t, lead=c, x=(round(xa, 2), round(xb, 2)),
                                    bottom_over_floor=round(y_under - h - y0, 2), spans=[(round(a, 2), round(b, 2)) for a, b in spans])


def _chamfer_top(p: P, shelf, box, y0, y1, bx0, n=4):
    """A 45 degree chamfer (p.top_chamfer) round the plate's top face, in n steps: the shelf printed top down has its
    top on the bed, and the chamfer takes the first layer's elephant foot."""
    c = p.top_chamfer
    keep = _box(-500, 500, y0 - 50, y1 - c + 0.001, -500, 500)
    for i in range(n):
        d = (i + 1) * c / n
        ya = y1 - c + i * c / n
        sl = (profile(p, box, ya).offset2D(-p.wall - p.print_clr - d).extrude(c / n + 0.001)
              .intersect(_box(bx0 + p.print_clr + d, 500, ya - 1, ya + 1, -500, 500)))
        keep = keep.union(sl)
    return shelf.intersect(keep)


def _top_label(p: P, shelf, box, y0, y1, bx0, seat_z):
    """Raised label on the shelf top: lines of p.top_label ('|' splits lines) plus an arrow, fitted into the plate
    between its -z edge and z_hi, read with the loop up; clipped 1 mm inside the plate outline (reported)."""
    lines = p.top_label.split('|')
    plate = profile(p, box, y1 - 0.01).offset2D(-p.wall - p.print_clr - 1.0).extrude(p.mark_h + 0.02)
    regs = [plate.intersect(_box(bx0 + 1.5, box[1], y1 - 1, y1 + 2, -200, seat_z[0])),      # beside the seat, -z
            plate.intersect(_box(bx0 + 1.5, box[1], y1 - 1, y1 + 2, seat_z[1], 200))]       # or +z: the wider one
    reg = max(regs, key=lambda r: r.val().BoundingBox().zlen)
    rb = reg.val().BoundingBox()
    za, zb, xa, xb = rb.zmin, rb.zmax, rb.xmin, rb.xmax
    n = len(lines) + 1                                  # the arrow row on top (the narrow curved end), then the lines
    h = min((xb - xa) / (n * 1.25), 6.0)
    zc = (za + zb) / 2
    avail = 0.92 * (zb - za)

    def txt(t, xc, hh):
        return (cq.Workplane(cq.Plane(origin=(xc, y1 - 0.01, zc), xDir=(0, 0, 1), normal=(0, 1, 0)))
                .text(t, hh, p.mark_h + 0.01, halign='center', valign='center', kind='bold'))
    parts = None
    for i, t in enumerate(lines):
        xc = xb - (i + 1.5) * (xb - xa) / n
        w = txt(t, xc, h)
        zl = w.val().BoundingBox().zlen
        if zl > avail:
            w = txt(t, xc, h * avail / zl)
        parts = w if parts is None else parts.union(w)
    xc = xb - 0.62 * (xb - xa) / n                  # a little down, clear of the curved corner
    aw, al = h * 0.9, h * 1.0
    pl = cq.Plane(origin=(0, y1 - 0.01, 0), xDir=(1, 0, 0), normal=(0, 1, 0))     # local (x, -z)
    arrow = cq.Workplane(pl).polyline([(xc - al / 2, -(zc - aw * 0.2)), (xc + al * 0.1, -(zc - aw * 0.2)), (xc + al * 0.1, -(zc - aw * 0.5)),
                                       (xc + al / 2, -zc), (xc + al * 0.1, -(zc + aw * 0.5)), (xc + al * 0.1, -(zc + aw * 0.2)),
                                       (xc - al / 2, -(zc + aw * 0.2))]).close().extrude(p.mark_h + 0.01)
    parts = parts.union(arrow)
    full = sum(v.Volume() for v in parts.vals())
    clipped = parts.intersect(reg)
    kept = sum(v.Volume() for v in clipped.vals())
    if p.top_label_deboss:       # cut, not raised: the same letters moved down by the depth (they stand mark_h tall)
        assert p.top_label_deboss <= p.mark_h
        return shelf.cut(clipped.translate((0, -p.top_label_deboss, 0))), dict(
            text=lines, h=round(h, 1), z=(round(za, 1), round(zb, 1)), x=(round(xa, 1), round(xb, 1)),
            kept_pct=round(100 * kept / full, 1), deboss=p.top_label_deboss)
    return shelf.union(clipped), dict(text=lines, h=round(h, 1), z=(round(za, 1), round(zb, 1)), x=(round(xa, 1), round(xb, 1)),
                                      kept_pct=round(100 * kept / full, 1))


def shelf_17_2b(proto=17, suffix='b', tray_version='v1.3'):
    """17.2b (owner, 2026-09-29): the approved 13.2B source (v1.1-fence, fence B) plus the exact 11.2 post solids,
    fitted to the printed 17.1 tray, nothing else:
      C1  the -z side trimmed to 0.2 off the 17.1 cavity (13.2B was 1.08 wider there), the left fence leg's end to
          0.2 off the -z ledge; the +z plate edge moved out 0.3 (0.2 play, as 13.2B had in #7)
      key the notch for the printed 17.1's tray key (the same shape v1.3 cut)
      the underside number reads '17.2b' (13.2B's size and place); ВЕРХ|МОТОР as 13.2B.
    Returns (shelf, info); info['pieces'] keeps the parts for the residual diff."""
    p13 = version_p('v1.1-fence', proto=proto, under_fence=('full', 2.0, 0.0), part_suffix=suffix)
    b13, _ = parts(p13)
    p11 = version_p('v1.1-throne', proto=11)
    b11, _ = parts(p11)
    sh13 = packing(p13)['parts']['shelf']
    sy0, sy1 = sh13[2], sh13[3]
    posts = [s_ for s_ in b11['shelf'].val().intersect(_box(150, 260, sy1 + 0.001, 40, -60, 60).val()).Solids()
             if s_.BoundingBox().ymax > sy1 + 2.0]                       # the 4 posts (not the raised label letters)
    S = b13['shelf']
    p17 = version_p(tray_version, closure='back')
    k17 = packing(p17)
    x0, x1, y0, y1, z0, z1 = k17['cavity']
    sh17 = k17['parts']['shelf']
    c = 0.2
    za_leg = z0 + p17.ledge_w + c                                          # the -z ledge + 0.2
    # C1 -z: the plate to z0 + 0.2, the fence (under the plate) to the ledge + 0.2
    S = S.cut(_box(100, 300, -30, 40, -200, z0 + c))
    S = S.cut(_box(100, 300, -30, sy0 - 0.001, -200, za_leg))
    # C1 +z: the plate's +z edge strip moved out 0.3 (the plate layer only)
    edge = S.intersect(_box(100, 300, sy0, sy1, 25.20 - 0.5, 40)).translate((0, 0, z1 - c - 25.20))
    S = S.union(edge)
    # the 17.1 outline limits it all (0.2 play)
    box17 = outer_box(p17)
    env = profile(p17, box17, -30).offset2D(-p17.wall - c).extrude(70)
    S = S.intersect(env)
    # the key notch for the printed 17.1 key (as v1.3 cut it)
    kl, kd = p17.shelf_key
    sc = p17.shelf_clr if p17.shelf_clr is not None else p17.print_clr
    ka = sh17[0] + sc + 4.0
    nd = kd + sc
    notch = _xyz_prism([(ka - sc, z1 + 1), (ka - sc, z1 - nd), (ka + kl + sc, z1 - nd), (ka + kl + sc + nd + 1, z1 + 1)], sy0 - 1, sy1 + 1)
    S = S.cut(notch)
    for s_ in posts:
        S = S.union(cq.Workplane().add(s_))
    return S, dict(p13=p13, p17=p17, posts=posts, notch=notch, trims=dict(minus_z_plate=round(z0 + c, 2), minus_z_leg=round(za_leg, 2),
                   plus_z_edge=round(z1 - c, 2)), key=(round(ka, 2), round(ka + kl, 2), kd))


# SparkFun Qwiic Haptic DA7280 (ROB-17590), from its Eagle board: 25.4 x 29.21 x 1.6, NPTH Ø3.048 at (2.54, 2.54),
# (2.54, 22.86), (22.86, 22.86); LRA Ø10 x 4.05 on top at (12.7, 23.09); Qwiic SM04B-SRSS-TB side entry J1 at (5.08, 12.7)
# facing -X (mouth at X 0.48), J2 at (20.32, 12.7) facing +X; bottom side bare (J3 / J4 are empty PTH rows)
DA7280 = dict(size=(25.4, 29.21, 1.6), holes=((2.54, 2.54), (2.54, 22.86), (22.86, 22.86)), hole_d=3.048,
              lra=(12.7, 23.09, 5.0, 4.05), j1=(0.48, 5.8, 9.7, 15.7, 2.95), j2=(19.6, 24.92, 9.7, 15.7, 2.95))


# The Expansion Board's battery socket as built (17.3b fit, owner photos 66ad06d2 / 0d6df690 / d55f4fc0 / f3428bdf): an
# SMT side-entry JST PH (S2B-PH-SM4-TB: 7.9 wide with its tabs, 5.5 max tall; the STEP's 8.15 width is kept), mouth +x.
# The STEP ends it at x 176.89; the printed 17.3b ledge (x 178.3) sat on it, and a mated PHR-2 (5.8 x 4.5 x 6.0, 3.4 past
# the front) under 17.2b's plate (x 181.89) and over the cell (x 182.19, top 0.95) bounds its front to <= ~178.8: the
# envelope takes the front plus latch ramp at 179.0 + 0.3. The plug: 5.8 wide on the socket's centre, top 3.4, to the
# cell face (182.2); its wires go down through the partition's lead notch to the cell's end.
BAT_SOCKET = dict(x=(168.64, 179.3), y=(-1.59, -1.60 + 5.5), z=(6.89, 15.04))
BAT_PLUG = dict(x=(176.0, 181.8), y=(-1.1, 3.4), z=(10.965 - 2.9, 10.965 + 2.9))


def bat_envelope():
    """(socket, mated plug) solids in the case frame (see BAT_SOCKET / BAT_PLUG)."""
    return (_box(*BAT_SOCKET['x'], *BAT_SOCKET['y'], *BAT_SOCKET['z']), _box(*BAT_PLUG['x'], *BAT_PLUG['y'], *BAT_PLUG['z']))


def da7280_place(bx0=177.3, zlo=-12.2, yb=2.05):
    """The DA7280 as placed on shelf 17.3 (top up, not mirrored: board X -> +x, board Y -> -z): board (X, Y) ->
    case (bx0 + X, zlo + 29.21 - Y); its underside at yb. Returns dict of case-frame solids and the hole centres."""
    L, W, t = DA7280['size']
    to = lambda X, Y: (bx0 + X, zlo + W - Y)
    top = yb + t
    out = dict(board=_box(bx0, bx0 + L, yb, top, zlo, zlo + W))
    lx, ly, lr, lh = DA7280['lra']
    cx, cz = to(lx, ly)
    out['lra'] = _cyl(cx, cz, lr, top, top + lh)
    for n in ('j1', 'j2'):
        x0, x1, y0, y1, h = DA7280[n]
        z0, z1 = sorted((to(0, y0)[1], to(0, y1)[1]))
        out[n] = _box(bx0 + x0, bx0 + x1, top, top + h, z0, z1)
    return out, [to(X, Y) for X, Y in DA7280['holes']], dict(bx0=bx0, zlo=zlo, yb=yb, top=top)


def shelf_17_3(proto=17, bx0=177.3, zlo=-12.2, pin_d=2.8, rib_tip=3.25, rib_w=0.6, ledge_x0=178.3, suffix='',
               seat_dy=0.0, pad_d=5.0, band_clr=0.8, arrow=False):
    """17.3 (2026-09-30): 17.2b (13.2B fence, key notch, C1 fit, ВЕРХ|МОТОР) without the 11.2 posts, for the DA7280
    lying flat on the plate top (2.05). The board overhangs the plate 4.8 toward the partition; a 0.6 ledge (0.2 over
    the partition's top) carries that edge and two of the three pins. The pins go through the board's Ø3.048 holes:
    a Ø{pin_d} core with 3 crush ribs to Ø{rib_tip} over the board, a 45 degree lead-in on top (as the face holes
    grip the dowels). The underside number reads 17.3."""
    S, inf = shelf_17_2b(proto=proto)
    p17 = inf['p17']
    k17 = packing(p17)
    sh = k17['parts']['shelf']
    sy0, sy1 = sh[2], sh[3]
    S = S.cut(_box(100, 300, sy1 + 0.001, 40, -60, 60))                       # the 11.2 posts off
    if arrow:   # 17.3c (owner, 2026-09-30): no «ВЕРХ / МОТОР»; one up arrow centred on the plate, pointing to the loop end
        lb = _box(182.9, 204.6, sy1 - 0.52, sy1 + 0.001, -21.2, -9.6)            # 13.2B's 0.5 deboss (x 183.4-204, z -20.6..-10.2)
        under = S.intersect(_box(150, 260, sy1 - 0.72, sy1 - 0.52, -60, 60))       # the full plate just under the deboss
        fill = None
        for dy in (0.18, 0.36, 0.52):
            piece = under.translate((0, dy, 0)).intersect(lb)
            fill = piece if fill is None else fill.union(piece)
        S = S.union(fill)
        ax_, az_ = (sh[0] + sh[1]) / 2, (sh[4] + sh[5]) / 2 - 0.0
        L_, W_, sw_ = 14.0, 9.0, 3.6                                                 # arrow length, head width, shaft width
        pts_ = [(-L_ / 2, -sw_ / 2), (L_ / 2 - W_ / 2, -sw_ / 2), (L_ / 2 - W_ / 2, -W_ / 2), (L_ / 2, 0),
                (L_ / 2 - W_ / 2, W_ / 2), (L_ / 2 - W_ / 2, sw_ / 2), (-L_ / 2, sw_ / 2)]
        arr = _xyp([(ax_ + u, v) for u, v in pts_], 0, 1) if False else None
        wp = cq.Workplane(cq.Plane(origin=(0, sy1 - 0.5, 0), xDir=(1, 0, 0), normal=(0, 1, 0)))
        arr = wp.polyline([(ax_ + u, -(az_ + v)) for u, v in pts_]).close().extrude(0.6)   # cut after the number fill below
        mark_info = dict(arrow_at=(round(ax_, 2), round(az_, 2)), length=L_, head=W_, shaft=sw_, deboss=0.5)
    # the underside number: fill 17.2b's, cut 17.3
    p13 = inf['p13']
    k13 = packing(p13)
    old = _under_number(p13, k13, sy0)
    bb = old.val().BoundingBox()
    fill = _box(bb.xmin - 0.3, bb.xmax + 0.3, sy0, sy0 + p13.under_depth + 0.01, bb.zmin - 0.3, bb.zmax + 0.3)
    S = S.union(fill.intersect(_box(100, 300, sy0, sy1, -60, 60)))
    lp = k13['parts']['lipo']
    mz0, mz1 = k13['parts']['motor'][4:6]
    nz_ = -12.0 if arrow else (mz0 + mz1) / 2     # 17.3c: the number off the top arrow (both 0.5 deep in a 0.8 plate)
    txt = (cq.Workplane(cq.Plane(origin=((lp[0] + lp[1]) / 2, sy0 - 0.01, nz_), xDir=(0, 0, -1), normal=(0, -1, 0)))
           .text(f'{proto}.3{suffix}', 6.0 if not suffix else (4.5 if arrow else 5.0), -p13.under_depth, halign='center',
                 valign='center', kind='bold'))
    if proto is not None:          # production (no part codes): no number on the underside
        S = S.cut(txt)
    if arrow:
        S = S.cut(arr)
    comps, holes, pl = da7280_place(bx0, zlo, sy1 + seat_dy)
    top = pl['top']
    x_edge = sh[0] + (p17.shelf_edge_clr or 0.2)                              # the plate's partition-side edge
    ledge = _box(ledge_x0, x_edge + 1.0, sy0 + 0.2, sy1, zlo - 1.0, zlo + DA7280['size'][1] + 1.0)
    bz0, bz1 = BAT_SOCKET['z'][0] - band_clr, BAT_SOCKET['z'][1] + band_clr
    if seat_dy > 0:   # 17.3c: the ledge stops short of the battery socket's band (the plate and fence stay as 17.2b's)
        ledge = ledge.cut(_box(ledge_x0 - 1.0, sh[0], sy0 - 1.0, sy1 + 1.0, bz0, bz1))
    S = S.union(ledge)
    for hx, hz in holes:
        if seat_dy > 0:   # a pad under each board hole, up to the raised underside; off the band, on the ledge / plate
            pad = _cyl(hx, hz, pad_d / 2, sy1 - 0.01, pl['yb']).intersect(_box(ledge_x0, 300, -50, 50, -60, 60))
            S = S.union(pad.cut(_box(0, 300, -50, 50, bz0, bz1)))
        pin = _cyl(hx, hz, pin_d / 2, sy1 - 0.01, top + 0.6).union(_cone(hx, hz, pin_d, pin_d - 0.8, top + 0.6, top + 1.0))
        for i_ in range(3):
            a_ = 2 * math.pi * i_ / 3 + math.pi / 2
            rib = _box(0.0, rib_tip / 2, (pl['yb'] - 0.01) if seat_dy > 0 else (sy1 - 0.01), top + 0.2, -rib_w / 2, rib_w / 2)
            rib = rib.union(_xyp([(0.0, top + 0.2), (rib_tip / 2, top + 0.2), (0.0, top + 0.2 + rib_tip / 2)], -rib_w / 2, rib_w / 2)
                            .intersect(_box(0.0, rib_tip / 2, top + 0.19, top + 1.0, -rib_w / 2, rib_w / 2)))
            pin = pin.union(rib.rotate((0, 0, 0), (0, 1, 0), math.degrees(a_)).translate((hx, 0, hz)))
        S = S.union(pin)
    inf.update(p17=p17, holes=holes, place=pl, comps=comps, ledge=(ledge_x0, round(x_edge + 1.0, 2), round(sy0 + 0.2, 2), sy1))
    if arrow:
        inf['mark'] = mark_info
    return S, inf


def shelf_17_3c(proto=17, seat_dy=2.65):
    """17.3c (2026-09-30, after 17.3b's ledge sat on the battery socket): 17.3b with the board raised seat_dy on three
    pads (underside 4.70: 0.8 over the socket's 5.5 top, 1.3 over the mated plug), the ledge cut back 0.8 either side of
    the socket's band; the plate, fence, key notch and C1 fit to tray 17.1 unchanged."""
    return shelf_17_3(proto=proto, bx0=177.5, zlo=-9.3, suffix='c', seat_dy=seat_dy, arrow=True)


def shelf_17_3b(proto=17):
    """17.3b (owner, 2026-09-30, option 3): 17.3 with the board, pins and ledge 2.9 toward +z (zlo -9.3) for frame
    18.1e's moved +z legs: J1's Qwiic cable runs >= 1.0 clear of the expansion board's 2 x 4 pin column."""
    return shelf_17_3(proto=proto, zlo=-9.3, suffix='b')


def _under_number(p: P, k, y_under, z_c=None):
    """The prototype number to cut into a shelf underside at y_under, 6 mm tall, read from below with the loop up."""
    lp = k['parts']['lipo']
    xc = (lp[0] + lp[1]) / 2
    mz0, mz1 = k['parts']['motor'][4:6]
    zc = (mz0 + mz1) / 2 if z_c is None else z_c
    return (cq.Workplane(cq.Plane(origin=(xc, y_under - 0.01, zc), xDir=(0, 0, -1), normal=(0, -1, 0)))
            .text(f'{p.proto}.2{p.part_suffix}', 6.0 if not p.part_suffix else 5.0, -p.under_depth, halign='center', valign='center', kind='bold'))


def proto_marks(p: P, tray, shelf, hold, k):
    """The prototype number, debossed 0.4 and 6 mm tall where it is hidden but readable: on the +z pad's top (under
    the shelf, clear of the cell, the board and the screws; loop up, read from above) and under the shelf's plate
    beneath the motor (read from below)."""
    x0, x1, y0, y1, z0, z1 = k['cavity']
    sh = k['parts']['shelf']
    zp = hold['pads']['plus_z_face']
    txt, h, d = f'{p.proto}.1', 6.0, 0.4             # part numbers: N.1 tray, N.2 shelf, N.3 cover
    lp = k['parts']['lipo']
    xc = (lp[0] + lp[1]) / 2
    ty = y0 + p.pad_h
    pad_zc = zp + 4.2                                     # the +z pad runs from zp to the wall (about 9 mm)
    if p.floor_arrow:             # #10: the pads are 6 mm wide; the number goes on the bay floor by the arrow
        ty, pad_zc, xc, h, d = y0, (lp[4] + lp[5]) / 2, xc + 5.0, 5.0, 0.5
    t1 = (cq.Workplane(cq.Plane(origin=(xc, ty - d, pad_zc), xDir=(0, 0, 1), normal=(0, 1, 0)))
          .text(txt, h, d + 0.01, halign='center', valign='center', kind='bold'))
    if p.mark_outside:            # #17: outside bottom under the bay, 5 mm, 0.4 deep; read from below with the loop up
        yb_ = outer_box(p)[2]
        xc, pad_zc, h, d = (lp[0] + lp[1]) / 2, (z0 + z1) / 2, 5.0, 0.4
        t1 = (cq.Workplane(cq.Plane(origin=(xc, yb_ - 0.01, pad_zc), xDir=(0, 0, -1), normal=(0, -1, 0)))
              .text(txt, h, -(d + 0.01), halign='center', valign='center', kind='bold'))
    # read from below (looking +y, loop up): right is -z
    mz0, mz1 = k['parts']['motor'][4:6]
    t2 = (cq.Workplane(cq.Plane(origin=((lp[0] + lp[1]) / 2, sh[2] - 0.01, (mz0 + mz1) / 2), xDir=(0, 0, -1), normal=(0, -1, 0)))
          .text(f'{p.proto}.2', h, -(d + 0.01), halign='center', valign='center', kind='bold'))
    out_ = dict(number=p.proto, tray=(round(xc, 1), round(pad_zc, 1)), shelf=(round(xc, 1), round((mz0 + mz1) / 2, 1)))
    if p.shelf_ribs:              # 10.2C: the underside prints on ribs; its number goes on top (_shelf_ribs)
        return tray.cut(t1), shelf, out_
    if p.shelf_support and not p.seat_split:   # 17.2: the underside prints on support: the number on top, as 10.2C
        t_ = (cq.Workplane(cq.Plane(origin=((k['parts']['motor'][0] + k['parts']['motor'][1]) / 2, sh[3] + 0.01, mz1 + 6.5),
                                    xDir=(0, 0, 1), normal=(0, 1, 0)))
              .text(f'{p.proto}.2{p.part_suffix}', 4.5, -0.51, halign='center', valign='center', kind='bold'))
        return tray.cut(t1), shelf.cut(t_), out_
    return tray.cut(t1), shelf.cut(t2), out_


def _outline_x_at(p: P, box, z, side='bottom'):
    poly, _ = outline_pts(p, box, 200)
    P_ = np.array(poly)
    xs = []
    for i in range(len(P_)):
        a, b = P_[i], P_[(i + 1) % len(P_)]
        if (a[1] - z) * (b[1] - z) <= 0 and a[1] != b[1]:
            t = (z - a[1]) / (b[1] - a[1])
            xs.append(a[0] + t * (b[0] - a[0]))
    return min(xs) if side == 'bottom' else max(xs)


# body -> which printed part and which filament
BODY_PART = dict(tray='tray', button='tray', pad='tray', loop='tray', cover='cover', zone1='cover', zone2='cover', led='cover',
                 shelf='shelf')

# Colour schemes. Solid fronts (the owner's choice, 2026-09-27): body colour (tray, loop, button, shelf),
# one front colour, and the LED skin (a separate piece only when the front spool is too opaque).
# Zoned fronts (option, P.front_zones=True): body + zone 3 / zone 1 / zone 2 + LED spot.
# 'light' picks the render setting. Every scheme uses at most 4 spools.
SCHEMES = dict(
    # --- shipping colourways (owner, 2026-09-27) ---
    v1=dict(body='pm_cotton_white', front='pm_pastel_periwinkle', led='pm_pastel_periwinkle', light='studio'),
    v5=dict(body='pm_cotton_white', front='pm_pastel_periwinkle', loop='pm_pastel_periwinkle',
            button='pm_pastel_periwinkle', led='pm_pastel_periwinkle', light='studio'),
    # --- archived (kept for reference) ---
    v1_whiteled=dict(body='pm_cotton_white', front='pm_pastel_periwinkle', led='pm_cotton_white', light='studio',
                     archived=True),
    v4=dict(body='pm_charcoal_black', front='pm_pastel_periwinkle', loop='pm_pastel_periwinkle',
            button='pm_pastel_periwinkle', led='pm_pastel_periwinkle', light='studio', archived=True),
    v2=dict(body='pm_charcoal_black', front='pm_pastel_periwinkle', led='pm_pastel_periwinkle', light='studio',
            archived=True),
    v3=dict(body='pm_army_blue', front='pm_raspberry_blue', led='pm_pastel_periwinkle', light='studio', archived=True),
    v3_gallery=dict(body='pm_army_blue', front='pm_raspberry_blue', led='pm_pastel_periwinkle', light='gallery',
                    archived=True),
    white_deep=dict(archived=True, body='pm_cotton_white', z3='pm_pastel_periwinkle', z1='pm_raspberry_blue', z2='pm_army_blue',
                    led='pm_cotton_white', light='studio'),
    white_pale=dict(archived=True, body='pm_cotton_white', z3='pm_army_blue', z1='pm_raspberry_blue', z2='pm_pastel_periwinkle',
                    led='pm_cotton_white', light='studio'),
    black_deep=dict(archived=True, body='pm_charcoal_black', z3='pm_pastel_periwinkle', z1='pm_raspberry_blue', z2='pm_army_blue',
                    led='pm_pastel_periwinkle', light='studio'),
    black_pale=dict(archived=True, body='pm_charcoal_black', z3='pm_army_blue', z1='pm_raspberry_blue', z2='pm_pastel_periwinkle',
                    led='pm_pastel_periwinkle', light='studio'),
    graphite_g1=dict(archived=True, body='pm_charcoal_black', z3='pm_ash_grey', z1='pm_charcoal_black', z2='pm_raspberry_blue',
                     led='pm_pastel_periwinkle', light='gallery'),
    graphite_g2=dict(archived=True, body='pm_charcoal_black', z3='pm_charcoal_black', z1='pm_army_blue', z2='pm_raspberry_blue',
                     led='pm_pastel_periwinkle', light='gallery'),
)


def scheme_zoned(name):
    return 'z3' in SCHEMES[name]


def body_filament(p: P, name, scheme=None):
    sc = SCHEMES[scheme or p.scheme]
    front = sc.get('front', sc.get('z3'))
    return dict(tray=sc['body'], pad=sc['body'], loop=sc.get('loop', sc['body']),
                button=sc.get('button', sc['body']), shelf=sc['body'], cover=front,
                zone1=sc.get('z1'), zone2=sc.get('z2'), led=sc['led'], support_fins=front, frame=front, frame_tie=front, seat=sc['body'], shelf_ribs=sc['body'])[name]


def scheme_spools(name):
    sc = SCHEMES[name]
    out = []
    for k in ('body', 'front', 'loop', 'button', 'z3', 'z1', 'z2', 'led'):
        if k in sc and sc[k] not in out:
            out.append(sc[k])
    return out


def schemes_for(p: P):
    """Schemes that match the current geometry (zoned or solid front)."""
    return [n for n in SCHEMES if scheme_zoned(n) == p.front_zones]


SHIPPING = ('v1', 'v5')


def grams(w):
    return sum(v.Volume() for v in w.vals()) / 1000 * DATASHEET['pm_matte_density']


EDGE_PROFILES = (('C1.2', ('chamfer', 1.2)), ('C0.8', ('chamfer', 0.8)), ('R1', ('round', 1.0)), ('R2.5', ('round', 2.5)),
                 ('C+r', ('blend', 1.2)))


def edge_coupon(profile, label, p: P = None, L=24.0, W=13.0, H=4.5, r=4.0):
    """Bottom-edge test piece in print orientation (z up, floor on the plate): a small open box with the tray's wall and
    floor, its outer bottom edge in one profile (chamfer c, round r, or 'blend': a chamfer with an r0.5 round where it
    meets the wall), the 45 degree floor-corner fill inside and the label raised on the floor."""
    p = p or P()
    kind, v = profile
    body = cq.Workplane().rect(L, W).extrude(H).edges('|Z').fillet(r)
    if kind == 'round':
        body = body.faces('<Z').edges().fillet(v)
    else:
        body = body.faces('<Z').edges().chamfer(v)
        if kind == 'blend':
            body = body.edges(cq.selectors.BoxSelector((-L, -W, v - 0.05), (L, W, v + 0.05))).fillet(0.5)
    t = p.wall
    inner = (cq.Workplane().workplane(offset=p.floor).rect(L - 2 * t, W - 2 * t).extrude(H).edges('|Z').fillet(max(r - t, 0.5))
             .faces('<Z').edges().chamfer(p.floor_chamfer))
    body = body.cut(inner)
    txt = cq.Workplane().workplane(offset=p.floor - 0.01).text(label, 4.0, 0.41, halign='center', valign='center', kind='bold')
    return body.union(txt).translate((L / 2, W / 2, 0))


# Side-button coupon (owner, 2026-09-28, after tray #7): a stretch of the 1.6 mm +z wall standing as printed (height =
# print Z), one side-button variant per cell, the variant's letters and the prototype number debossed on the outer face.
#   A.4 / A.6  skin button: the U cut from the inside only, a continuous 0.4 / 0.6 skin outside, a 0.3 dot to press
#   B1         through-slit, tongue 1.6, square free end, two 0.3 breakaway ties across the end slit (snap them)
#   B2         through-slit, tongue 1.6, square free end 2.4 thick, no ties
#   C          through-slit, the tongue stands up from a hinge at its bottom (print Z): no layer of it overhangs
BC = dict(T=1.6, H=20.5, cell=20.0, first=10.0, foot=(1.2, 8.0), slit=0.7, groove=1.5, za=11.0, zb=16.0,
          tab_len=16.0, nub_at=13.5, text_z=(3.5, 8.5), text_depth=0.4, c_z=(6.0, 17.6), c_w=5.0, c_t=1.25)
BC_VARIANTS = ('A.4', 'A.6', 'B1', 'B2', 'C')


def _yzp(pts, x0, x1):
    """Prism along x over a polygon in (y, z) (the coupon's print frame: x along the wall, y thickness, z up)."""
    pl = cq.Plane(origin=(x0, 0, 0), xDir=(0, 1, 0), normal=(1, 0, 0))
    return cq.Workplane(pl).polyline(pts).close().extrude(x1 - x0)


def button_coupon(number=None, variants=BC_VARIANTS):
    """In print frame: x along the wall, y through it (outer face y = 0, facing -y), z up; a foot inside at the base."""
    b = BC
    T, s, W = b['T'], b['slit'], b['groove']
    L = b['first'] + b['cell'] * len(variants)
    body = _box(0, L, 0, T, 0, b['H']).union(_box(0, L, 0, b['foot'][1], 0, b['foot'][0]))
    sv = s * 2 ** 0.5
    za, zb = b['za'], b['zb']

    def deboss(txt, x, z0, z1):
        h = z1 - z0
        pl = cq.Plane(origin=(x, -0.01, (z0 + z1) / 2), xDir=(1, 0, 0), normal=(0, -1, 0))   # reads from the front
        return cq.Workplane(pl).text(txt, h, -(b['text_depth'] + 0.01), halign='center', valign='center', kind='bold')
    cuts = []
    if number is not None:
        cuts.append(deboss(str(number), b['first'] / 2, 7.0, 15.0))
    for i, v in enumerate(variants):
        c0 = b['first'] + b['cell'] * i
        h0, h1 = c0 + 2.0, c0 + 2.0 + b['tab_len']
        if v == 'C':
            ca, cb = b['c_z']
            xa, xb = c0 + 8.5, c0 + 8.5 + b['c_w']
            cuts.append(_box(xa - s, xa, -1, T + 1, ca, cb + T + sv))
            cuts.append(_box(xb, xb + s, -1, T + 1, ca, cb + T + sv))
            cuts.append(_yzp([(-0.5, cb - 0.5), (T + 0.5, cb + T + 0.5), (T + 0.5, cb + T + 0.5 + sv), (-0.5, cb - 0.5 + sv)],
                             xa - s, xb + s))
            cuts.append(_box(xa, xb, b['c_t'], T + 1, ca, cb + T + 1))            # tongue thinned from inside
            cuts.append(deboss('C', c0 + 4.0, *b['text_z']))
            continue
        if v.startswith('A'):
            sk = float('0' + v[1:])                                             # 'A.4' -> 0.4
            d = T + 1 - sk
            cuts.append(_yzp([(sk, za - W), (T + 1, za - W), (T + 1, za + d), (sk, za)], h0, h1 + W))
            cuts.append(_yzp([(sk, zb), (T + 1, zb + d), (T + 1, zb + d + W), (sk, zb + W)], h0, h1 + W))
            cuts.append(_box(h1, h1 + W, sk, T + 1, za - W, zb + W + d))
            dot = cq.Workplane(cq.Plane(origin=(h0 + b['nub_at'], -0.01, (za + zb) / 2), xDir=(1, 0, 0), normal=(0, 1, 0))) \
                .circle(1.0).extrude(0.31)
            cuts.append(dot)
        else:
            cuts.append(_box(h0, h1 + s, -1, T + 1, za - s, za))                              # bottom slit (bridged)
            cuts.append(_yzp([(-0.5, zb - 0.5), (T + 0.5, zb + T + 0.5), (T + 0.5, zb + T + 0.5 + sv), (-0.5, zb - 0.5 + sv)],
                             h0, h1 + s))                                                    # 45 degree roofed top slit
            cuts.append(_box(h1, h1 + s, -1, T + 1, za - s, zb + T + sv))                    # end slit
        cuts.append(deboss(v, (h0 + h1) / 2, *b['text_z']))
    for c in cuts:
        body = body.cut(c)
    for i, v in enumerate(variants):
        c0 = b['first'] + b['cell'] * i
        h1 = c0 + 2.0 + b['tab_len']
        if v == 'B1':      # two 0.3 ties across the end slit on the inner half: the tongue's first layer bridges to one
            for z in (za, zb - 0.62):
                body = body.union(_box(h1 - 0.01, h1 + s + 0.01, T - 0.5, T, z, z + 0.32))
        if v == 'B2':      # the free end thickened to 2.4 inward over its last 3 mm
            body = body.union(_box(h1 - 3.0, h1, T - 0.01, T + 0.8, za, zb))
    return body


def button_coupon_mechanics(sw_force=1.6, travel=0.25, gap=0.15):
    """Press force at the nub for each coupon variant: flexure + switch. Skin variants: the skin across the inside
    groove bends as a fixed-guided strip along the U (stiffness E t^3 / W^3 per mm), weighted by the tongue's
    cantilever deflection shape; the tongue itself is the full wall."""
    b = BC
    E = DATASHEET['pla_E_xy']
    stroke = gap + travel
    L, H = b['nub_at'], b['zb'] - b['za']
    out = {}

    def cant(t, l=L, w=H):
        return 3 * E * (w * t ** 3 / 12) / l ** 3

    shape = lambda x: (x / L) ** 2 * (3 - x / L) / 2 if x <= L else 1 + 1.5 * (x - L) / L   # cantilever, unit at the nub
    for v in BC_VARIANTS:
        if v.startswith('A'):
            sk = float('0' + v[1:])
            kp = E * sk ** 3 / b['groove'] ** 3                  # N/mm per mm of groove
            xs = np.linspace(0, b['tab_len'], 400)
            dx = xs[1] - xs[0]
            e2 = sum(shape(x) ** 2 for x in xs) * dx * 2 + shape(b['tab_len']) ** 2 * H
            k = cant(b['T']) + kp * e2
        elif v == 'C':
            ca, cb = b['c_z']
            k = cant(b['c_t'], l=(cb - ca) - 1.5, w=b['c_w'])
        else:
            k = cant(b['T'])
        out[v] = dict(k_N_per_mm=round(k, 2), flex_N=round(k * stroke, 2), press_N=round(k * stroke + sw_force, 2))
    out['current_1.25'] = dict(k_N_per_mm=round(cant(1.25), 2), flex_N=round(cant(1.25) * stroke, 2),
                               press_N=round(cant(1.25) * stroke + sw_force, 2))
    out['stroke_mm'] = stroke
    return out


def led_coupon(p: P = None, thicknesses=(0.3, 0.4)):
    """LED skin test coupon: a 34 x 16 x 2.4 mm plate with a Ø3.0 pocket from below per thickness, leaving
    that skin on top (same pocket as the case). Notches on one long edge mark the order: one notch = the
    first thickness, two = the second."""
    p = p or P()
    L, W, T = 34.0, 16.0, 2.4
    c = cq.Workplane().box(L, W, T).edges('|Z').fillet(2.0).translate((0, 0, T / 2))
    n = len(thicknesses)
    for i, t in enumerate(thicknesses):
        x = -L / 2 + L * (i + 1) / (n + 1)
        c = c.cut(cq.Workplane().cylinder(T - t + 0.01, p.led_d / 2).translate((x, 0, (T - t) / 2 - 0.005)))
        for j in range(i + 1):
            c = c.cut(cq.Workplane().box(0.8, 1.2, T + 1).translate((x - 1.0 * i + 2.0 * j, W / 2, T / 2)))
    return c


# ---------------------------------------------------------------- inside parts
def _render_module():
    sys.path.insert(0, CAD)
    cwd = os.getcwd(); os.chdir(CAD)
    import render as R          # guide's line-art tools (read-only reuse)
    return R, cwd


def board_parts():
    """Expansion Board + two 1x7 headers + XIAO, posed as in the guide (render.py)."""
    R, cwd = _render_module()
    try:
        E = R.expansion()
        H = R.headers(2.9)
        X = R.xiao(2.9 + 2.54 + XIAO_DY)
    finally:
        os.chdir(cwd)
    return dict(expansion=E, headers=H, xiao=X)


def lipo(p: P = None):
    p = p or P()
    x0, x1, y0, y1, z0, z1 = packing(p)['parts']['lipo']
    return cq.Workplane().box(x1 - x0, y1 - y0, z1 - z0).edges('|Y').fillet(1.0).translate(((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2))


def _cell(p: P, hold, L, t, w=20.0):
    """A real cell, L long and t thick, against the +z pad face, on the floor: centred across the bay, or (a shelf with
    an under fence) 0.1 past the fence's battery-side face, where the fence's lead-in pushes it."""
    x0, x1, y0, _, _, _ = packing(p)['parts']['lipo']
    zp = hold['pads']['plus_z_face']
    xc = (x0 + x1) / 2
    if p.under_fence and hold.get('under_fence'):
        xa = hold['under_fence']['x'][1] + 0.1           # a pouch cell's corners are rounded (about 1 mm)
        return _box(xa, xa + w, y0, y0 + t, zp - L, zp).edges('|Y').fillet(1.0)
    return _box(xc - w / 2, xc + w / 2, y0, y0 + t, zp - L, zp)


def motor(p: P = None):
    """Grove Vibration Motor envelope: tails / PCB / socket + coin motor, socket opening toward +z (layout B)."""
    p = p or P()
    x0, x1, y0, y1, z0, z1 = packing(p)['parts']['motor']
    z0, z1 = z0 + p.motor_dz, z1 + p.motor_dz
    tails, pcb, sock = DATASHEET['motor_split']
    xc = (x0 + x1) / 2
    w = x1 - x0; l = z1 - z0
    yb = y0 + tails
    wp = cq.Workplane().box(w, pcb, l).translate((xc, yb + pcb / 2, (z0 + z1) / 2))
    sd = 6.0 if l < 22 else 8.3                     # Grove socket depth along z (real module: about 6)
    if p.motor_rot180:            # J1: vertical, on the -x edge, 5 x 10 x sock; the coin (10 x 3.4) on the +x half
        zc_ = (z0 + z1) / 2
        wp = wp.union(_box(x0 + 0.5, x0 + 5.5, yb + pcb, yb + pcb + sock, zc_ - 5.0, zc_ + 5.0))
        cdx, cdz = p.coin_at or (2.0, 0.0)
        wp = wp.union(cq.Workplane().cylinder(3.4, 5.0, direct=(0, 1, 0)).translate((xc + cdx, yb + pcb + 1.7, zc_ + cdz)))
    else:
        wp = wp.union(cq.Workplane().box(10.0, sock, sd).translate((xc, yb + pcb + sock / 2, z1 - sd / 2)))
        cz = z0 + 6.0 if l < 22 else z0 + 9.0
        wp = wp.union(cq.Workplane().cylinder(3.4, 5.0, direct=(0, 1, 0)).translate((xc, yb + pcb + 1.7, cz)))
    for i in range(4):     # the socket's through-hole pins under the PCB (#11 fit: 1.6)
        wp = wp.union(cq.Workplane().box(0.6, p.motor_pins, 0.6).translate((xc - 3 + 2 * i, yb - p.motor_pins / 2, z1 - 3.0)))
    if p.motor_retain:     # the M2 ears mid-edge on the +-x edges (audit probe: 2 out, 5 wide), as the owner fitted #7
        zc = (z0 + z1) / 2
        for xa, xb in ((x0 - p.ear_out, x0), (x1, x1 + p.ear_out)):
            wp = wp.union(_box(xa, xb, yb, yb + pcb, zc - 2.5, zc + 2.5))
    return wp


# ---------------------------------------------------------------- reports
def measure():
    bp = board_parts()
    for name, w in bp.items():
        bb = cq.Compound.makeCompound([s for v in w.vals() for s in v.Solids()]).BoundingBox()
        print('%-10s x %.2f..%.2f  y %.2f..%.2f  z %.2f..%.2f' % (name, bb.xmin, bb.xmax, bb.ymin, bb.ymax, bb.zmin, bb.zmax))


def options():
    rows = []
    for lay in 'ABC':
        p = P(layout=lay, corner='circle')
        X0, X1, Y0, Y1, Z0, Z1 = base_outer_box(p)
        _, lp = loop_lug(p, (X0, X1, Y0, Y1, Z0, Z1))
        rows.append((lay, Z1 - Z0, X1 - X0, Y1 - Y0, Y1 - Y0 + 2.0, lp['end_x'] - X0,
                     (Z1 - Z0) * (X1 - X0) * (Y1 - Y0) / 1000))
    print('layout  W(z)   H(x)   D(rim) D(crown) H+loop  box cm3')
    for r in rows:
        print('  %s   %5.1f  %5.1f  %5.1f   %5.1f    %5.1f   %5.1f' % r)
    return rows


def width_alternative(p: P):
    """Instead of lengthening the bottom (which sinks the USB-C mouth), widen the case symmetrically
    until the bottom end fits with the USB-C mouth on the wall; then grow the top as needed."""
    X0, X1, Y0, Y1, Z0, Z1 = base_outer_box(p)
    mid = (X0 + X1) / 2

    def ok(dw, dt, side):
        box = (X0, X1 + dt, Y0, Y1, Z0 - dw, Z1 + dw)
        poly, _ = outline_pts(p, box, 90)
        for pts, clr in footprints(p):
            sel = [q for q in pts if (q[0] < mid) == (side == 'bottom')]
            if sel and (_inside_margin(poly, sel) - p.wall - clr).min() < -1e-3:
                return False
        return True

    def solve(f):
        if f(0.0):
            return 0.0
        lo, hi = 0.0, 40.0
        for _ in range(40):
            m = (lo + hi) / 2
            lo, hi = (lo, m) if f(m) else (m, hi)
        return hi
    dw = solve(lambda m: ok(m, 40, 'bottom'))
    dt = solve(lambda m: ok(dw, m, 'top'))
    return (X0, X1 + dt, Y0, Y1, Z0 - dw, Z1 + dw), dw, dt


def variants():
    rows = []
    for v, kw in VARIANTS.items():
        p = P(**kw)
        base = base_outer_box(p)
        X0, X1, Y0, Y1, Z0, Z1 = outer_box(p)
        _, lp = loop_lug(p, (X0, X1, Y0, Y1, Z0, Z1))
        rows.append(dict(v=v, corner=p.corner, extent=corner_extent(p, Z1 - Z0), W=Z1 - Z0, H=X1 - X0,
                         D=Y1 - Y0, Dc=Y1 - Y0 + 2.0, Hloop=lp['end_x'] - X0,
                         grow_bottom=base[0] - X0, grow_top=X1 - base[1], usb_recess=(base[0] - X0) + p.wall + 0.2))
    print('var corner    extent  W      H      D(rim) D(crown) H+loop  +bottom +top  USB-C mouth below outer')
    for r in rows:
        print(' %(v)s  %(corner)-8s %(extent)5.1f  %(W)5.1f  %(H)5.1f  %(D)5.1f   %(Dc)5.1f    %(Hloop)5.1f   %(grow_bottom)5.2f  %(grow_top)5.2f  %(usb_recess)5.2f' % r)
    print('width alternative (USB-C mouth kept on the wall):')
    for v, kw in VARIANTS.items():
        p = P(**kw)
        (X0, X1, Y0, Y1, Z0, Z1), dw, dt = width_alternative(p)
        print(' %s  W %.1f  H %.1f  D %.1f  (+%.2f each side, top +%.2f)' % (v, Z1 - Z0, X1 - X0, Y1 - Y0, dw, dt))
    return rows


# ---------------------------------------------------------------- line art
def shade_png(shapes, view, up, box, px_per_mm, path, colours=None):
    """Z-buffered flat shading under the line art: light greys from a fixed upper-left light,
    in the same projection as render.py's HLR (screen x = p.xdir, screen y = -p.ydir)."""
    from PIL import Image
    vv = cq.Vector(*view).normalized(); xd = cq.Vector(*up).cross(vv).normalized(); yd = vv.cross(xd)
    L = (xd * -0.45 + yd * 0.65 + vv * 0.6).normalized()
    x0, y0, W, H = box
    w, h = int(W * px_per_mm), int(H * px_per_mm)
    zbuf = np.full((h, w), -np.inf); img = np.full((h, w, 3), 255.0); alpha = np.zeros((h, w))
    vva, xda, yda, La = (np.array(v.toTuple()) for v in (vv, xd, yd, L))
    for si, sh in enumerate(shapes):
        base = None
        if colours and colours[si]:
            hexc = colours[si]
            base = np.array([int(hexc[i:i + 2], 16) for i in (1, 3, 5)], dtype=float)
        for sol in sh.vals():
            for f in sol.Faces():
                vs, ts = f.tessellate(0.02, 0.15)
                if not ts:
                    continue
                P3 = np.array([[v.x, v.y, v.z] for v in vs])
                sx = (P3 @ xda - x0) * px_per_mm
                sy = (-(P3 @ yda) - y0) * px_per_mm
                dz = P3 @ vva
                for a, b, c in ts:
                    n = np.cross(P3[b] - P3[a], P3[c] - P3[a]); ln = np.linalg.norm(n)
                    if ln < 1e-12:
                        continue
                    n /= ln
                    if n @ vva < 0:
                        n = -n
                    lum = 0.25 + 0.75 * max(0.0, float(n @ La))
                    col = (np.array([224 + 31 * lum - 2, 224 + 31 * lum - 2, 226 + 29 * lum]) if base is None
                           else np.clip(base * (0.72 + 0.34 * lum), 0, 255))
                    xa, ya = sx[[a, b, c]], sy[[a, b, c]]
                    i0, i1 = max(int(xa.min()), 0), min(int(xa.max()) + 2, w)
                    j0, j1 = max(int(ya.min()), 0), min(int(ya.max()) + 2, h)
                    if i0 >= i1 or j0 >= j1:
                        continue
                    gx, gy = np.meshgrid(np.arange(i0, i1) + 0.5, np.arange(j0, j1) + 0.5)
                    d = (ya[1] - ya[2]) * (xa[0] - xa[2]) + (xa[2] - xa[1]) * (ya[0] - ya[2])
                    if abs(d) < 1e-12:
                        continue
                    l1 = ((ya[1] - ya[2]) * (gx - xa[2]) + (xa[2] - xa[1]) * (gy - ya[2])) / d
                    l2 = ((ya[2] - ya[0]) * (gx - xa[2]) + (xa[0] - xa[2]) * (gy - ya[2])) / d
                    l3 = 1 - l1 - l2
                    m = (l1 >= -1e-3) & (l2 >= -1e-3) & (l3 >= -1e-3)
                    z = l1 * dz[a] + l2 * dz[b] + l3 * dz[c]
                    zb = zbuf[j0:j1, i0:i1]
                    m &= z > zb
                    zb[m] = z[m]
                    img[j0:j1, i0:i1][m] = col
                    alpha[j0:j1, i0:i1][m] = 255
    Image.fromarray(np.dstack([img, alpha]).astype(np.uint8), 'RGBA').save(path)


def line_art(groups, views, prefix, crop=None, width=1400, styles=None):
    """groups: [(name, [Workplane])]; the first group is the object (dark), the rest context (grey)."""
    import base64
    import cairosvg
    R, cwd = _render_module()
    os.chdir(cwd)
    os.makedirs(PREVIEW, exist_ok=True); os.makedirs(BUILD, exist_ok=True)
    from OCP.BRepLib import BRepLib
    for _, ws in groups:         # mark tangent seams smooth so HLR does not draw fillet/arc joins
        for w in ws:
            for v in w.vals():
                BRepLib.EncodeRegularity_s(v.wrapped, 1e-3)
    styles = styles or {n: (R.OBJ if i == 0 else R.CTX) for i, (n, _) in enumerate(groups)}
    for name, (view, up) in views.items():
        res = R.project_groups(groups, view, up)
        if crop:
            bx = crop
        else:
            polys = [pl for o, i in res.values() for pl in o + i]
            xs = [x for pl in polys for x, _ in pl]; ys = [y for pl in polys for _, y in pl]
            pad = 3.0
            bx = (min(xs) - pad, min(ys) - pad, max(xs) - min(xs) + 2 * pad, max(ys) - min(ys) + 2 * pad)
        png = os.path.join(BUILD, f'fill-{prefix}-{name}.png')
        shade_png([w for _, ws in groups for w in ws], view, up, bx, max(12, 700 / bx[2]), png)
        href = 'data:image/png;base64,' + base64.b64encode(open(png, 'rb').read()).decode()
        svg, _ = R.styled_svg(res, styles, f'Raily Keyring case, {name}', png=href,
                              crop=(bx[0], bx[1], bx[0] + bx[2], bx[1] + bx[3]), base=0.0022)
        path = os.path.join(BUILD, f'{prefix}-{name}.svg')
        open(path, 'w').write(svg)
        cairosvg.svg2png(url=path, write_to=os.path.join(PREVIEW, f'{prefix}-{name}.png'),
                         output_width=width, background_color='white')


def usb_overmold(p: P = None, length=20.0, size=(12.0, 6.5)):
    """A standard USB-C cable overmold, seated: front face 0.3 mm short of the receptacle mouth."""
    u = MEASURED['usb_c']; w, h = size
    uy, uz = sum(u['y']) / 2, sum(u['z']) / 2
    xf = u['x_mouth'] - 0.3
    return _box(xf - length, xf, uy - h / 2, uy + h / 2, uz - w / 2, uz + w / 2).edges('|X').fillet(1.5)


def usb_plug(p: P, box, length=20.0):
    """A USB-C cable plug as mated in v1.1: the spec-max overmold (12.35 x 6.5) stops 0.05 mm short of the case's
    outer face, the spec-max metal shell (8.4 x 2.6) runs 6.65 mm on from it into the receptacle."""
    uy, uz = usb_axis()
    xo = _outline_x_at(p, box, uz, side='bottom')
    (ow, oh), (sw, sh, sl) = DATASHEET['usb_c_overmold'], DATASHEET['usb_c_plug_shell']
    om = _box(xo - 0.05 - length, xo - 0.05, uy - oh / 2, uy + oh / 2, uz - ow / 2, uz + ow / 2).edges('|X').fillet(1.5)
    return om.union(_box(xo - 0.06, xo - 0.05 + sl, uy - sh / 2, uy + sh / 2, uz - sw / 2, uz + sw / 2).edges('|X').fillet(1.2))


def screws(p: P, info):
    """As fitted, alternating screw, nut. 'front': ISO 1207 M2 x screw_len cheese head + ISO 4032 M2 nut.
    'back': ISO 7046-1 M2 x back_screw_len countersunk (dk 3.8, k 1.2) + ISO 4032 M2 nut. 'snap': none."""
    f = info['fastening']
    out = []
    if f['closure'] == 'snap':
        return out
    if f['closure'] == 'back':
        for hx, hz in f['holes']:
            head = _cone(hx, hz, 3.8, 2.0, f['seat'], f['seat'] + 0.9).union(_cyl(hx, hz, 1.9, f['seat'], f['seat'] + 0.05))
            out.append(head.union(_cyl(hx, hz, 1.0, f['seat'] + 0.8, f['tip']))
                       .cut(_box(hx - 0.25, hx + 0.25, f['seat'] - 0.1, f['seat'] + 0.5, hz - 1.5, hz + 1.5))
                       .cut(_box(hx - 1.5, hx + 1.5, f['seat'] - 0.1, f['seat'] + 0.5, hz - 0.25, hz + 0.25)))
            out.append(_hexprism(hx, hz, p.nut_af, f['nut'][0], f['nut'][1]).cut(_cyl(hx, hz, 0.8, -20, 20)))
        return out
    for hx, hz in f['holes']:
        out.append(_cyl(hx, hz, 1.9, f['seat'], f['seat'] + 1.3).union(_cyl(hx, hz, 1.0, f['tip'], f['seat']))
                   .cut(_box(hx - 0.25, hx + 0.25, f['seat'] + 0.8, f['seat'] + 1.4, hz - 1.5, hz + 1.5)))
        out.append(_hexprism(hx, hz, p.nut_af, f['nut'][0], f['nut'][1]).cut(_cyl(hx, hz, 0.8, -20, 20)))
    return out


def cable_zone(p: P):
    """Free volume over the A0/D0 shroud row, from the shroud tops to under the cover lip."""
    M = MEASURED
    x0, x1, y0, y1, z0, z1 = packing(p)['cavity']
    return (M['grove_a0']['x'][0], 166.0, M['grove_a0']['y'][1] + p.clr, y1 - p.lip_h - p.clr,
            13.06 + p.clr, z1 - p.rim_band - p.print_clr - p.lip_w)


def cable_budget(p: P = None):
    p = p or P()
    zx0, zx1, zy0, zy1, zz0, zz1 = cable_zone(p)
    path = 50.0      # A0/D0 plug -> along the +z wall above the nub -> motor socket (see check_cable)
    excess = p.cable_len - 2 * DATASHEET['grove_plug_len'] - path
    stacks = int((zz1 - zz0) // p.cable_w)
    fold = zx1 - zx0 - 2.0
    layers = math.ceil(excess / fold / max(stacks, 1))
    return dict(zone=(round(zx1 - zx0, 1), round(zy1 - zy0, 1), round(zz1 - zz0, 1)), excess=excess,
                stacks=stacks, fold=fold, layers=layers, stack_h=layers * p.cable_t, fits=layers * p.cable_t <= zy1 - zy0)


def check(p: P = None, bodies=None, info=None, strict=True):
    """Overlap (must be 0) and minimum gap of every inside part against every printed body. strict (audit,
    2026-09-28): any overlap above 0.001 mm3 fails the build (AssertionError), not just a printed line."""
    from OCP.BRepExtrema import BRepExtrema_DistShapeShape
    p = p or P()
    if bodies is None:
        bodies, info = parts(p)
    case = cq.Compound.makeCompound([v for k_, w in bodies.items() if k_ not in ('support_fins', 'frame_tie', 'shelf_ribs') for v in w.vals()])
    groups = dict(board_parts())
    groups['lipo'] = lipo(p)
    hold = (info or {}).get('hold')
    if hold and p.bay_leaf:
        # the bay now holds real cells (cell_len x 20 x cell_t), not the 32 mm envelope: the free check uses the
        # shortest, thinnest cell less the hook preload; the hook's working contacts are reported below
        groups['lipo'] = _cell(p, hold, p.cell_len[0], p.cell_t[0] - p.hook_pre * 2 ** 0.5 - 0.05)
    elif hold:                    # rigid bay: both real cells must be free, against the +z pad
        groups['cell_thor'] = _cell(p, hold, p.cell_len[1], p.cell_t[1])
        groups['cell_eemb'] = _cell(p, hold, p.cell_len[0], p.cell_t[0])
        del groups['lipo']
    groups['motor'] = motor(p)
    if p.motor_kind == 'da7280':   # the DA7280 on 17.3c (its board, LRA, J1, J2) and the battery socket + mated plug
        del groups['motor']
        S3c, inf3c = shelf_17_3c()
        for n_, w_ in inf3c['comps'].items():
            if n_ == 'board':    # the board's holes: the pins' crush ribs in them are the intended contact
                for hx_, hz_ in inf3c['holes']:
                    w_ = w_.cut(_cyl(hx_, hz_, 3.25 / 2 + 0.01, -50, 50))
            groups[f'da7280_{n_}'] = w_
        groups['bat_socket'], groups['bat_plug'] = bat_envelope()
        bodies = dict(bodies, shelf=S3c)
        case = cq.Compound.makeCompound([v for k_, w in bodies.items() if k_ not in ('support_fins', 'frame_tie', 'shelf_ribs') for v in w.vals()])
    groups['usb_plug'] = usb_plug(p, info['box'])
    zx0, zx1, zy0, zy1, zz0, zz1 = cable_zone(p)
    groups['cable_zone'] = _box(zx0, zx1, zy0, zy1, zz0, zz1)
    if p.window != 'none':      # the bezel over the OLED glass (the board group's own minimum is the standoffs)
        g = MEASURED['oled']
        groups['oled_glass'] = _box(g['x'][0], g['x'][1], g['y_top'] - 1.0, g['y_top'], *g['glass_z'])
    # intended contacts, not overlaps: the crush ribs' crush into the board edge, and the STEP's switch lever, which
    # sits about 1.7 lower than the real one (MEASURED) and so runs into the wall under the slot built for the real one
    allow = None
    bl = (info or {}).get('board_locate')
    if bl:
        M_ = MEASURED
        bz1 = M_['board_z'][1]
        for r_ in bl['ribs']:
            a_ = _box(r_['x'] - 0.8, r_['x'] + 0.8, M_['pcb_y'][0] - 0.01, M_['pcb_y'][1] + 0.01, bz1 - p.crush - 0.01, bz1 + 0.01)
            allow = a_ if allow is None else allow.union(a_)
    if p.switch_wall_clr is not None:
        sl_ = MEASURED['switch_lever']
        cav_ = packing(p)['cavity']
        a_ = _box(sl_['x'][0] - 0.05, sl_['x'][1] + 0.05, cav_[2] - 1, cav_[3], info['box'][4] - 1, cav_[4] + 0.01)
        allow = a_ if allow is None else allow.union(a_)
    res = {}
    # motor_dz: the module as built sits in the printed 17.2b (the exact 11.2 posts), not in this version's shelf body
    shelf_skip = p.motor_dz != 0 and 'shelf' in bodies
    if shelf_skip:
        case_m = cq.Compound.makeCompound([v for k_, w in bodies.items() if k_ not in ('support_fins', 'frame_tie', 'shelf_ribs', 'shelf')
                                           for v in w.vals()])
    for name, w in groups.items():
        if name == 'expansion' and allow is not None:
            w = w.cut(allow)
        comp = cq.Compound.makeCompound([x for v in w.vals() for x in v.Solids()])
        vol = 0.0
        case_ = case_m if (shelf_skip and name == 'motor') else case
        for v in case_.Solids():
            try:
                vol += v.intersect(comp).Volume()
            except Exception:
                pass
        d = BRepExtrema_DistShapeShape(case_.wrapped, comp.wrapped) if name == 'motor' else BRepExtrema_DistShapeShape(case.wrapped, comp.wrapped)
        d.Perform()
        res[name] = (round(d.Value(), 3), round(vol, 4))
        print('%-13s min gap to the case %.2f mm, overlap %.4f mm3' % (name, d.Value(), vol))
    # the cable zone must also be free of board parts
    zone = groups['cable_zone'].val()
    bvol = sum(zone.intersect(s).Volume() for w in board_parts().values() for v in w.vals() for s in v.Solids())
    print('cable zone vs board parts overlap %.4f mm3' % bvol)
    res['cable_zone_board'] = bvol
    if hold and p.bay_leaf:
        # spring contacts: the shelf's hook is meant to lift (the leaf bends) out of these volumes
        for L in p.cell_len:
            for t in p.cell_t:
                cell = _cell(p, hold, L, t).val()
                v = {n: sum(cell.intersect(s).Volume() for s in bodies[n].vals()) for n in ('tray', 'shelf', 'cover')}
                print('cell %.1f x %.1f spring contact: tray %.2f, shelf %.2f, cover %.2f mm3' % (L, t, v['tray'], v['shelf'], v['cover']))
                res[f'cell_{L}_{t}'] = {n: round(x, 3) for n, x in v.items()}
    # printed parts against each other: the closed case must not overlap itself (cover lip / catches vs tray pads)
    for a_, b_ in (('cover', 'tray'), ('cover', 'button'), ('tray', 'button'), ('shelf', 'tray'), ('shelf', 'cover'),
                   ('frame', 'tray'), ('frame', 'shelf'), ('frame', 'cover'), ('frame', 'button')):
        if a_ in bodies and b_ in bodies:
            v = sum(x.intersect(y).Volume() for x in bodies[a_].vals() for y in bodies[b_].vals())
            print('%s vs %s overlap %.4f mm3' % (a_, b_, v))
            res[f'{a_}_{b_}_overlap'] = round(v, 4)
    # the pillars' M2 nuts go in sideways through slots toward the case middle (#18.1c: two corner posts stood 0.3 into
    # that path): nothing of the frame may sit in the slot's extension, a nut width and more past its mouth
    if 'frame' in bodies and p.closure == 'back' and (info or {}).get('fastening'):
        f_ = info['fastening']
        n0_, n1_ = f_['nut']
        af_ = p.nut_af + p.nut_slot_clr
        xm_ = (info['box'][0] + info['box'][1]) / 2
        for hx_, hz_ in f_['holes']:
            xa_, xb_ = (hx_ + 2.05, hx_ + 16) if hx_ < xm_ else (hx_ - 16, hx_ - 2.05)
            v_ = sum(x_.Volume() for x_ in bodies['frame'].intersect(_box(xa_, xb_, n0_, n1_, hz_ - af_ / 2, hz_ + af_ / 2)).vals())
            print('nut path at (%.2f, %.2f): frame in it %.4f mm3' % (hx_, hz_, v_))
            res[f'nut_path_{hx_}_{hz_}_overlap'] = round(v_, 4)
    if strict:
        bad = {k_: v_ for k_, v_ in res.items() if (isinstance(v_, tuple) and v_[1] > 1e-3)
               or (k_.endswith('_overlap') and v_ > 1e-3) or (k_ == 'cable_zone_board' and v_ > 1e-3)}
        assert not bad, f'check: overlaps {bad}'
    return res


def _bridge_span(reg, held):
    """(longest run, anchored fraction) of a layer region along its better axis: a run is anchored when the cells
    just past both of its ends are held (a bridge); a cantilever has one free end."""
    best = (0, 0.0)
    for ax in (0, 1):
        R, Hd = (reg, held) if ax == 0 else (reg.T, held.T)
        runs = ok = longest = 0
        for i in range(R.shape[0]):
            row = R[i]
            if not row.any():
                continue
            d = np.diff(np.r_[0, row.astype(np.int8), 0])
            for a, b in zip(np.where(d == 1)[0], np.where(d == -1)[0]):
                runs += 1
                longest = max(longest, b - a)
                ok += bool(a > 0 and Hd[i, a - 1] and b < len(row) and Hd[i, b])
        f = ok / runs if runs else 0.0
        if f > best[1]:
            best = (int(longest), round(f, 2))
    return best


def printability(solid, up=(0, 0, 1), pitch=0.2, bridge_max=10.0, hang_max=0.9):
    """Print-orientation checks for one printed part (audit 2026-09-28), on a voxel grid (pitch = the layer height).
    up: the part's own direction that points up on the plate. Each layer's cells with nothing under them are grouped;
    a group's hang is its farthest cell from a supported cell of the same layer. hang <= hang_max: fine (a 45 degree
    face steps one cell per layer). Longer: a bridge if it is held on two or more separate sides and no wider than
    bridge_max, else FAIL. A group touching no supported cell is an island (it starts in the air)."""
    import trimesh
    from scipy import ndimage
    vs, ts = [], []
    for v in solid.vals():
        vv, tt = v.tessellate(0.02, 0.2)
        o = len(vs)
        vs += [(q.x, q.y, q.z) for q in vv]
        ts += [(i + o, j + o, l + o) for i, j, l in tt]
    m = trimesh.Trimesh(np.array(vs), np.array(ts), process=True)
    u = np.array(up, float) / np.linalg.norm(up)
    m.apply_transform(trimesh.geometry.align_vectors(u, [0, 0, 1]))
    m.apply_translation(-m.bounds[0])
    M3 = m.voxelized(pitch=pitch).fill().matrix        # (x, y, z)
    groups, eight = [], np.ones((3, 3), bool)
    for k in range(1, M3.shape[2]):
        cur, below = M3[:, :, k], M3[:, :, k - 1]
        air = cur & ~below
        if not air.any():
            continue
        held = cur & below
        dist = ndimage.distance_transform_edt(~held) * pitch if held.any() else None
        labs, nl = ndimage.label(air, structure=eight)
        for j in range(1, nl + 1):
            reg = labs == j
            if reg.sum() < 2:
                continue
            ring = ndimage.binary_dilation(reg, structure=eight) & held
            at = np.argwhere(reg).mean(0) * pitch
            g = dict(z=round(k * pitch, 2), cells=int(reg.sum()), at=(round(float(at[0]), 1), round(float(at[1]), 1)))
            if not ring.any():
                g.update(kind='ISLAND', hang=None)
            else:
                hang = float(dist[reg].max())
                span, anch = _bridge_span(reg, held)
                g.update(hang=round(hang, 2), span=span, anchored=anch,
                         kind='ok' if hang <= hang_max + 1e-6 else
                         ('bridge' if anch >= 0.9 and span * pitch <= bridge_max else 'FAIL'))
            if g['kind'] != 'ok':
                groups.append(g)
    return dict(height=round(float(m.bounds[1][2]), 2), groups=groups,
                fails=[q for q in groups if q['kind'] in ('FAIL', 'ISLAND')], bridges=[q for q in groups if q['kind'] == 'bridge'])


# ---------------------------------------------------------------- line art / export
FRONT = ((0, 1, 0), (1, 0, 0))
BACK = ((0, -1, 0), (1, 0, 0))
SIDE = ((0, 0, 1), (1, 0, 0))
ISO = ((0.45, 1.0, 0.9), (1, 0, 0))


def closed(pr):
    """The closed case as a compound of its bodies (not fused), so colour-region borders stay visible."""
    return cq.Workplane().add(cq.Compound.makeCompound(
        [v for k in ('tray', 'button', 'cover', 'pad', 'loop', 'zone1', 'zone2', 'led') if k in pr for v in pr[k].vals()]))


def export_print(p: P, bodies, info, out=None):
    """print/ (v2: print/v2/): one STL per colour body (the slicer assigns a filament per body; the geometry is
    the same for every scheme), one STEP assembly (bodies named, coloured in P.scheme), and filaments.json with
    the filament of every body in every scheme."""
    out = out or os.path.join(HERE, 'print'); os.makedirs(out, exist_ok=True)
    asm = cq.Assembly(name='raily-keyring-case')
    rows = []
    for name, w in bodies.items():
        fil = body_filament(p, name)
        part = BODY_PART[name]
        fn = f'keyring-{part}-{name}.stl'
        cq.exporters.export(w, os.path.join(out, fn), tolerance=0.02, angularTolerance=0.1)
        h = FILAMENTS[fil]['hex']
        rgb = [int(h[i:i + 2], 16) / 255 for i in (1, 3, 5)]
        asm.add(w, name=f'{part}-{name}', color=cq.Color(*rgb))
        rows.append((part, name, fn, FILAMENTS[fil]['name'], round(sum(v.Volume() for v in w.vals()) / 1000, 2)))
    asm.save(os.path.join(out, 'keyring-case.step'))
    with open(os.path.join(out, 'bodies.json'), 'w') as fh:
        json.dump([dict(part=a, body=b, file=c, cm3=e) for a, b, c, d, e in rows], fh, indent=1)
    fil = dict(shipping={}, archived={}, density_g_cm3=DATASHEET['pm_matte_density'],
               note='grams are solid volume x density (100 % infill); the slicer and AMS purge add to this')
    for sc in schemes_for(p):
        g = {}
        for n, w in bodies.items():
            k = body_filament(p, n, sc)
            g[k] = g.get(k, 0) + grams(w)
        fil['archived' if SCHEMES[sc].get('archived') else 'shipping'][sc] = dict(
            spools=[dict(key=k, name=FILAMENTS[k]['name'], sku=FILAMENTS[k].get('sku'), hex=FILAMENTS[k]['hex'],
                         td=FILAMENTS[k].get('td')) for k in scheme_spools(sc)],
            bodies={n: FILAMENTS[body_filament(p, n, sc)]['name'] for n in bodies},
            grams_per_case={FILAMENTS[k]['name']: round(v, 1) for k, v in g.items()})
    with open(os.path.join(out, 'filaments.json'), 'w') as fh:
        json.dump(fil, fh, indent=1)
    coupon = led_coupon(p)
    cq.exporters.export(coupon, os.path.join(out, 'keyring-led-coupon.stl'), tolerance=0.01, angularTolerance=0.1)
    if info.get('window_insert') is not None:      # optional clear insert for the window recess (not a colour body)
        cq.exporters.export(info['window_insert'], os.path.join(out, 'keyring-window-insert.stl'),
                            tolerance=0.01, angularTolerance=0.1)
    return rows


def export_scene(p: P, bodies, info, tag='stage2'):
    """build/<tag>/: meshes + scene.json for blender_render.py."""
    d = os.path.join(BUILD, tag); os.makedirs(d, exist_ok=True)
    for name, w in bodies.items():
        cq.exporters.export(w, os.path.join(d, f'{name}.stl'), tolerance=0.01, angularTolerance=0.03)
    sc = screws(p, info)
    cq.exporters.export(cq.Workplane().add(cq.Compound.makeCompound([v for w in sc for v in w.vals()])),
                        os.path.join(d, 'screws.stl'), tolerance=0.01, angularTolerance=0.1)
    Ld = MEASURED['xiao_rgb_led']
    cq.exporters.export(_box(Ld['x'][0], Ld['x'][1], Ld['y'][0], Ld['y'][1], Ld['z'][0], Ld['z'][1]), os.path.join(d, 'led_die.stl'))
    extras = {}
    if info.get('window'):      # what the window shows: the OLED glass (off) on the board
        g = MEASURED['oled']
        cq.exporters.export(_box(g['x'][0], g['x'][1], g['y_top'] - 1.0, g['y_top'], *g['glass_z']), os.path.join(d, 'oled_glass.stl'))
        cq.exporters.export(_box(g['x'][0] - 3, g['x'][1] + 3, MEASURED['pcb_y'][0], MEASURED['pcb_y'][1], g['z'][0] - 3, g['z'][1] + 3),
                            os.path.join(d, 'oled_pcb.stl'))
        extras = dict(oled_glass=dict(hex='#07080A', rough=0.06), oled_pcb=dict(hex='#15161A', rough=0.5))
    box = info['box']
    json.dump(dict(centre=[(box[0] + box[1]) / 2, (box[2] + box[3]) / 2, (box[4] + box[5]) / 2], box=box,
                   colours={n: FILAMENTS[body_filament(p, n)]['hex'] for n in bodies},
                   schemes={sc: dict(light=SCHEMES[sc]['light'], led_td=FILAMENTS[SCHEMES[sc]['led']].get('td') or 0.5,
                                     colours={n: FILAMENTS[body_filament(p, n, sc)]['hex'] for n in bodies})
                            for sc in schemes_for(p)},
                   ocean=FILAMENTS['esun_ocean']['stops'], tab=info['tab'], led=info['led'], extras=extras,
                   window=info.get('window') and dict(aperture=info['window']['aperture'], front=info['window']['front'])),
              open(os.path.join(d, 'scene.json'), 'w'), indent=1)


def tab_section(p: P, pr, info, prefix='tab'):
    """Section at the D1 actuator height, seen from the front: wall, slits, tongue, nub, D1."""
    g = info['tab']
    bp = board_parts()
    sols = [s for v in bp['expansion'].vals() for s in v.Solids()]
    near = [s for s in sols if s.BoundingBox().xmin < g['hx1'] + 6 and s.BoundingBox().xmax > g['hx0'] - 4
            and s.BoundingBox().zmax > 14]
    keep = cq.Workplane().box(60, 40, 40).translate(((g['hx0'] + g['hx1']) / 2, g['yc'] - 20, 20))
    tray = pr['tray'].intersect(keep)
    board = cq.Workplane().add(cq.Compound.makeCompound(near)).intersect(keep)
    view, up = (0, 1, 0), (0, 0, 1)
    vv = cq.Vector(*view); xd = cq.Vector(*up).cross(vv).normalized(); yd = vv.cross(xd)
    pts = [(x, z) for x in (g['hx0'] - 4, g['hx1'] + 4) for z in (g['zout'] + 2.0, 15.5)]
    sx = [cq.Vector(x, 0, z).dot(xd) for x, z in pts]; sy = [-cq.Vector(x, 0, z).dot(yd) for x, z in pts]
    crop = (min(sx), min(sy), max(sx) - min(sx), max(sy) - min(sy))
    line_art([('tray', [tray] + ([pr['pad']] if 'pad' in pr else [])), ('board', [board])], dict(section=(view, up)),
             prefix, crop=crop, width=1600)
    # across the tongue, seen from the loop end (+x): at the middle of its free length (the 45 degree roofed slit
    # above the tongue, the tongue, the slit below) and through the nub at the D1 centre (there the free-end chamfer
    # also cuts the tongue's top). Thin slices, so the slits read as gaps
    d1 = cq.Workplane().add(cq.Compound.makeCompound(near))
    for name, xm in (('across', (g['hx0'] + g['hx1'] - p.tab_end_chamfer) / 2), ('across-nub', g['xc'])):
        keep = _box(xm - 0.4, xm, g['ya'] - 8, g['yb'] + 8, g['zin'] - 8, g['zout'] + 3)
        parts_ = [pr[k].intersect(keep) for k in ('tray', 'button') if k in pr]
        view, up = (1, 0, 0), (0, 1, 0)
        line_art([('tray', parts_), ('board', [d1.intersect(keep)])], {name: (view, up)}, prefix,
                 crop=_crop(view, up, [(xm, y, z) for y in (g['ya'] - 1.5, g['yb'] + 3.5)
                                       for z in (g['nub_z0'] - 2.5, g['zout'] + 1.5)]), width=1400)


def _crop(view, up, pts):
    """line_art crop (x, y, w, h) around 3D points, in the projection of (view, up)."""
    vv = cq.Vector(*view).normalized(); xd = cq.Vector(*up).cross(vv).normalized(); yd = vv.cross(xd)
    sx = [cq.Vector(*q).dot(xd) for q in pts]; sy = [-cq.Vector(*q).dot(yd) for q in pts]
    return (min(sx), min(sy), max(sx) - min(sx), max(sy) - min(sy))


def usb_views(p: P, pr, info, prefix):
    """USB-C end: the closed case seen from outside (-x) with the XIAO at its soldered height behind the opening, and
    a section along the USB axis (seen from +z) with a spec-max plug mated: overmold at the outer face, metal shell in."""
    uy, uz = usb_axis()
    bp = board_parts()
    near = lambda w: [s for v in w.vals() for s in v.Solids() if s.BoundingBox().xmin < 128]
    board = cq.Workplane().add(cq.Compound.makeCompound(near(bp['xiao']) + near(bp['expansion'])))
    case = [pr[k] for k in ('tray', 'cover', 'button', 'loop', 'led') if k in pr]
    X0 = info['box'][0]
    view, up = (-1, 0, 0), (0, 1, 0)
    line_art([('case', case), ('board', [board])], {'usb-end': (view, up)}, prefix,
             crop=_crop(view, up, [(X0, y, z) for y in (uy - 7, info['facets']['top'] + 1) for z in (uz - 13, uz + 13)]), width=1400)
    keep = _box(X0 - 30, X0 + 22, -20, 30, -40, uz)
    plug = usb_plug(p, info['box'], length=8.0)
    view, up = (0, 0, 1), (0, 1, 0)
    line_art([('case', [w.intersect(keep) for w in case]), ('board', [board.intersect(keep), plug.intersect(keep)])],
             {'usb-section': (view, up)}, prefix,
             crop=_crop(view, up, [(x, y, uz) for x in (X0 - 7, X0 + 12) for y in (uy - 6, info['facets']['top'] + 0.5)]), width=1400)


def window_section(p: P, pr, info, prefix):
    """Sections through the OLED window: across (at the aperture centre in x, seen from +x) and along (at the
    aperture centre in z, seen from +z). The board is drawn as boxes from MEASURED (PCB, OLED glass, XIAO board
    on its headers), which keeps the drawing light."""
    wg = info['window']
    ap = wg['aperture']
    xc, zc = (ap[0] + ap[1]) / 2, (ap[2] + ap[3]) / 2
    M = MEASURED
    g = M['oled']
    board = [_box(*M['board_x'], *M['pcb_y'], *M['board_z']),
             _box(g['x'][0], g['x'][1], g['y_top'] - 1.0, g['y_top'], *g['glass_z']),
             _box(g['x'][0], g['x'][1], M['pcb_y'][1], -1.02, g['glass_z'][1], g['z'][1]),
             _box(119.48, 140.44, 5.39 + XIAO_DY, 6.63 + XIAO_DY, -8.00, 9.78),  # XIAO PCB (board_parts)
             _box(121.13, 138.91, 2.90, 5.44 + XIAO_DY, -8.01, -5.47), _box(121.13, 138.91, 2.90, 5.44 + XIAO_DY, 7.24, 9.78)]
    for name, keep, view in (('across', _box(xc - 30, xc, -30, 30, -40, 40), ((1, 0, 0), (0, 1, 0))),
                             ('along', _box(100, 230, -30, 30, zc - 40, zc), ((0, 0, 1), (0, 1, 0)))):
        case = [pr[k].intersect(keep) for k in ('cover', 'tray') if k in pr]
        brd = [b.intersect(keep) for b in board]
        line_art([('case', case), ('board', brd)], {name: view}, f'{prefix}-window', width=1600)


def stage2(p: P = None, version='v1'):
    """Build one case version. v1 writes print/, build/stage2, preview/case-*; other versions write
    print/<version>/, build/stage2-<version>, preview/case-<version>-*."""
    if version not in VERSIONS:
        raise SystemExit(f'unknown case version {version!r}; known: {", ".join(VERSIONS)}')
    p = p or version_p(version)
    sfx = '' if version == 'v1' else f'-{version}'
    out = os.path.join(HERE, 'print') if version == 'v1' else os.path.join(HERE, 'print', version)
    bodies, info = parts(p)
    print('bodies:', {k: round(sum(v.Volume() for v in w.vals()) / 1000, 2) for k, w in bodies.items()}, 'cm3')
    rows = export_print(p, bodies, info, out)
    for r in rows:
        print('  %-6s %-8s %-32s %-44s %5.2f cm3' % r)
    for sc in schemes_for(p):
        print('  %-12s %s' % (sc, ', '.join(FILAMENTS[k]['name'].replace('Polymaker Panchroma Matte ', '')
                                            for k in scheme_spools(sc))))
    export_scene(p, bodies, info, 'stage2' + sfx)
    c = closed(bodies)
    line_art([('case', [c])], dict(front=FRONT, back=BACK, side=SIDE, iso=ISO), 'case' + sfx)
    tab_section(p, bodies, info, 'tab' + sfx)
    usb_views(p, bodies, info, 'usb' + sfx)
    if info.get('window'):
        window_section(p, bodies, info, 'case' + sfx)
    res = check(p, bodies, info)
    # closure B (snap-fit) alternate: the tray and cover bodies that differ, in <out>/closure-b/
    pb = replace(p, closure='snap')
    bb_, ib_ = parts(pb)
    db = os.path.join(out, 'closure-b'); os.makedirs(db, exist_ok=True)
    for name in ('tray', 'button', 'loop', 'cover', 'led'):
        if name in bb_:
            cq.exporters.export(bb_[name], os.path.join(db, f'keyring-{BODY_PART[name]}-{name}.stl'),
                                tolerance=0.02, angularTolerance=0.1)
    print('closure B check:')
    res_b = check(pb, bb_, ib_)
    X0, X1, Y0, Y1, Z0, Z1 = info['box']
    size = dict(width=round(Z1 - Z0, 2), length=round(X1 - X0, 2), rim=round(Y1 - Y0, 2),
                crown=round(info['facets']['top'] - Y0, 2), with_loop=round(info['loop']['end_x'] - X0, 2),
                box=[round(v, 3) for v in info['box']])
    wnd = None
    if info.get('window'):
        wg = info['window']
        r3 = lambda t: [round(v, 2) for v in t]
        wnd = dict(mode=p.window, aperture=r3(wg['aperture']),
                   aperture_size=r3((wg['aperture'][1] - wg['aperture'][0], wg['aperture'][3] - wg['aperture'][2])),
                   glass_top=wg['glass_top'], bezel=r3(wg['bezel']), front=round(wg['front'], 2), depth=round(wg['depth'], 2),
                   top_opening=r3(wg['top']), top_size=r3((wg['top'][1] - wg['top'][0], wg['top'][3] - wg['top'][2])),
                   field=wg['field'], full_view_cone_deg=wg['cone_deg'], viewing_area=r3(wg['viewing_area']),
                   active_area=r3(wg['active_area']), insert=wg['insert_size'],
                   from_case_centre=r3(((wg['aperture'][0] + wg['aperture'][1]) / 2 - (X0 + X1) / 2,
                                        (wg['aperture'][2] + wg['aperture'][3]) / 2 - (Z0 + Z1) / 2)))
    json.dump(dict(version=version, params={k: v for k, v in asdict(p).items() if k in VERSIONS.get(version, {}) or k.startswith('window')},
                   size=size, window=wnd, closure=p.closure, check=res, check_closure_b=res_b, snap=snap_mechanics(pb),
                   tab=tab_mechanics(p), fit=fit_check(p, info), floor=floor_deflection(p), floor_v1=floor_deflection(replace(p, floor=1.4)),
                   cable=cable_budget(p), usb_depth=info['usb_depth'], usb=usb_window_report(p, info),
                   facets={k: (round(v, 3) if isinstance(v, float) else v) for k, v in info['facets'].items()},
                   fastening=info['fastening'], switch_slot=info['switch_slot'], led=info['led']),
              open(os.path.join(out, 'report.json'), 'w'), indent=1, default=float)
    print('size:', size)
    fc = fit_check(p, info)
    print('fit:', json.dumps(fc))
    if not fc['ok']:
        print('FIT CHECK FAILED: a board-referenced opening misses its part (see fit in report.json)')
    if wnd:
        print('window:', wnd)
    print('cable:', cable_budget(p))
    return bodies, info


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'variants'
    if cmd == 'measure':
        measure()
    elif cmd == 'options':
        options()
    elif cmd == 'variants':
        variants()
    elif cmd == 'tab':
        for k, v in tab_mechanics().items():
            print('%-14s %s' % (k, round(v, 4) if isinstance(v, float) else v))
    elif cmd == 'cable':
        print(cable_budget())
    elif cmd == 'check':
        check(P(**VARIANTS[sys.argv[2]]) if len(sys.argv) > 2 else P())
    elif cmd == 'stage2':         # stage2 [v1|v2|v2flat]
        stage2(version=sys.argv[2] if len(sys.argv) > 2 else 'v1')
    elif cmd == 'flatten':      # Blender writes RGBA; put it on white for the preview folder
        from PIL import Image
        src = sys.argv[2]
        h = sys.argv[3] if len(sys.argv) > 3 else '#FFFFFF'      # e.g. #EDEEF1, the white ads' paper
        im = Image.open(src).convert('RGBA')
        bg = Image.new('RGBA', im.size, tuple(int(h[i:i + 2], 16) for i in (1, 3, 5)) + (255,))
        Image.alpha_composite(bg, im).convert('RGB').save(src)
    else:
        raise SystemExit(__doc__)

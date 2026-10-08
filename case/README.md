# Raily Keyring case v1.1 (CAD)

`keyring_case.py` is the one parametric CadQuery model of the keyring case. Print files (STL/STEP), the assembly guide's line art and the Blender renders all come from it. Nothing about the case is drawn by hand or by an image model.

**Status: stage 2.** The internals are modelled, print files are exported, and the interference check passes. Owner decisions so far:

- **Layout B:** the cell and motor sit in an end bay.
- **Corner variant b:** a continuous-curvature (G2) corner.
- **Matte PLA.**
- **Solid single-colour front:** the X is only the facet relief. Facets now tilt 7° (arms) / 12° (corners); no V-groove. The 3-zone front stays in the model as an option (`P.front_zones`).
- **No OLED window:** the firmware does not use the OLED.
- **RGB LED spot:** a thin skin of the lightest filament over the XIAO RGB LED, or a through-hole.
- **Print in one go:** no glued parts or inserts; at most 4 spools including the body colour.
- **Shipping colourways (locked):** V1 (Cotton White body, loop and button; Pastel Periwinkle front) and V5 (Cotton White body; Pastel Periwinkle front, loop and button). Facets 7° / 12°, 23.7 mm. The LED sits under a 0.3 mm Periwinkle skin in both, with no white dot (0.4 mm until 2026-09-28, see [LED skin](#shipping-colourways-v1-and-v5)). Every other scheme is archived.
- **Power-switch slot:** on the −z wall, over the lever.
- **Grove cable:** the kit's 20 cm cable, coiled inside.
- **Closure (2026-09-28): the front stays clean.** Default **A**: four M2 countersunk screws from the back into nuts in the cover pillars. Alternate **B**: screwless, four snap latches. Both are printable pieces of the same model (`P.closure`); see [Closure](#closure). The first version's screws from the front stay as `closure='front'`.

**Case v2 (2026-09-28):** the same board stack with a window over the OLED and a thinner shell. It is a separate
version of the same model (`version_p('v2')`). See [Case v2: OLED window](#case-v2-oled-window).

**Revision v1.1 (2026-09-28)**, after the owner's first print of tray A (A1 mini, Overture Matte PLA, 0.16 mm). It
replaces v1 in `print/` and the Bambu projects; v2 takes the same USB-C end, button and screw-hole changes and
keeps its 1.6 / 1.2 shell. See [Revision v1.1](#revision-v11).

## Revision v1.1

The owner printed the v1 tray, then a wall test (1.0 / 1.25 / 1.6 walls) with the new USB-C window and button, and
fitted the real boards (2026-09-28). v1.1 is the result. The numbers below come from `stage2` (`print/report.json`).

| | v1 | **v1.1** |
| --- | --- | --- |
| Side walls / floor | 2.0 / 1.4 | **1.6 / 1.4** (the owner's pick from the wall test) |
| Case W × L at the rim | 51.2 × 97.8 | **50.4 × 89.5** |
| Thickness at the rim / over the crown | 20.3 / 23.7 | **20.3 / 24.0** |
| Length with the loop | 106.1 | **97.8** |
| Filament per case, solid (V1) | 23.8 g white + 19.0 g periwinkle = 42.8 g | **20.3 + 19.1 = 39.4 g** |
| Back (build plate) edge | 2.5 round | **1.2 × 45° chamfer** |
| USB-C opening | 13.0 × 7.2, r 1.8, 0.8 chamfer, 8.3 mm tunnel | **9.8 wide, r 1.6, 0.25 chamfer**, fitted to the receptacle shell; a notch to the parting line |
| Receptacle mouth under the outer face | 8.3 mm (the plug's overmold went in) | **0.2 mm** |
| Power-switch slot | 3.7 × 1.9, for the lever as in the STEP | **3.7 × 2.5, 1.6 higher** (the real lever) |
| Board height over the floor | 3.48 | **2.55**, the CR1220 holder in a floor pocket |
| Side-button tongue | 1.0 thick, 6 high, 14 long, 0.6 slits, a 14 mm flat bridge over it, a 1.4 nub | **1.25 thick, 5 high, 16 long, 0.7 slits, a 45° roof over it, a 45° free-end chamfer, the nub a 4.4 mm rib** |

**Real board heights (owner's fit test, 2026-09-28).** The Expansion Board lies flat on its four standoffs at the
model height (bare board in the printed tray: no PCB edge in the switch slot, no rocking). Two parts differ from the
Seeed STEP:
- **XIAO: +2.0.** The board's XIAO sockets are about 5.5 mm tall, not the STEP's 4.5, and the header spacer stands
  about 0.5 off them (side photos, the 2.54 spacer as the scale). `XIAO_DY` moves every XIAO-referenced number
  (USB-C receptacle, XIAO top, its LEDs). The window also takes +1.5 (`XIAO_DY_TOL`).
- **Headers the reversed way:** the short ends (≈ 3 mm) go down into the board's sockets, the spacer on the
  sockets, the XIAO on the long ends, soldered off the board (a breadboard keeps the pins straight), the tails
  clipped to ≤ 2 mm. Long ends down bottom out in the sockets and hold the XIAO about 6 mm high; the guide shows
  that as wrong. Before the cover goes on, the SWD spring pins under the USB-C end lift the XIAO slightly; that is
  normal.
- **Power-switch lever: about +1.7** on its body (`SWITCH_DY`; entirely above the v1 slot with the board seated,
  an earlier photo showed its bottom sliver). The slot takes a lever at +1.0…+2.4.
- **Board lower over the floor.** The parts under the board end 1.3–2.3 mm below it (microSD slot, through-hole
  tails, CR1220 holder; photos), so the floor rises under the board until the battery bay sets the depth
  (`low_floor`): the board stands 2.55 above the floor top (v1 3.48). The CR1220 holder gets a floor pocket
  that leaves 0.75 mm of floor. With the XIAO at +2.0 the case keeps a 20.3 rim: 1.4 floor + 17.9 bay (cell 6.7,
  swell 0.3, shelf 0.8, motor 9.8, clearance 0.3) + 1.0 cover.
- `fit_check()` (in `report.json` and printed by `stage2`) measures every board-referenced opening against the real
  part: the receptacle in the window (at +1.5 and +2.0) and the cover over it, the switch lever in its slot over
  the full ON–OFF travel, the nub on the D1 actuator, the LED under its skin spot, and the solid wall around the
  button slits. It fails below 0.2 mm (1.5 mm for the slit walls). The side button keeps its v1 relation to D1:
  0.15 rest gap, 0.275 nub overlap.

**USB-C.** The opening fits the receptacle shell (8.94 × 3.21 at its mouth, a section of the STEP shell solid), not
a plug overmold: 0.43 mm per side across.
- In height the window runs from 0.3 mm under the receptacle of a XIAO at +1.5 (y 7.84) up to the parting line
  (y 12.15): an open notch in the tray rim that the cover closes. At +2.0 the receptacle has 0.8 below and the
  cover 0.3 above. (A window top within 0.8 mm of the parting line always snaps onto it, so neither part keeps a
  sliver thinner than 0.8 mm; `usb_window_rect()`.)
- A thinner wall alone could not bring the mouth out. In v1 the bottom end had grown 6.1 mm past the base box so
  the G2 corners (circle-equivalent 15) cleared the PCB's corners. v1.1 makes the end flat: the USB-C end's
  corners are smaller (`g2_req_bottom` 4.25), the flat end wall is 1.25 mm (`end_wall`; the walls are 1.6), and its
  outer face sits `usb_recess` 0.2 mm before the mouth. The loop end keeps its corners.
- A spec-max plug (overmold 12.35 × 6.5, metal shell 8.4 × 2.6 × 6.65) stops with its overmold on the outer face
  and its shell 6.45 mm into the 7.3 mm deep receptacle. `check()` places that plug: no overlap.

**Side button.** The upper slit (print Z) was a 14 mm bridge, and the 1.0 mm tongue was 2.4 nozzle lines: its top
edge and free end printed ragged. In the 1.25 wall test the lower slit ran into the back fillet and tore.
- The upper slit rises at 45° from the outer face inward, so the wall above grows over it at 45° and the tongue's
  top leans back on its inner side. Vertical gap 0.99 mm, 0.7 mm across the slope.
- The free end's top corner is cut at 45° (2 mm), and the slit follows it.
- The nub is a vertical rib over the tongue's height (y −2.84…1.57, `nub_h` 0): the D1 actuator stays under it for
  ±1.8 mm of height (v1 ±0.28). It only ever meets the actuator, which stands proud of everything else there.
- The tongue is 1.25 mm (3 lines), 5.0 high, centred on D1; the arm is 16 mm (hinge to the D1 centre 13.5), so the
  force stays where it was. Solid wall from the lower slit to the bottom chamfer: 2.1 mm; from the upper slit to
  the parting line: 8.0 mm.

**Bottom edge.** The 2.5 round on the build-plate side printed as stair steps (owner photo): a round starts almost
flat at the plate, so its first layers overhang by most of a line. A 1.2 × 45° chamfer (`back_chamfer`) steps
0.16 mm per 0.16 mm layer. Across the corner the thinnest material is 1.84 mm. `preview/edge-before-after.png`;
`build_bambu.py edgecoupon` prints five profiles (C1.2, C0.8, R1, R2.5, C1.2 + r0.5) side by side on the A1 mini.

**Screw holes (closure A).** See [Printing notes](#printing-notes-v11) for the root cause. The 1.4 floor rings
around the countersinks of the wall test printed clean; with the 1.4 floor they are the floor itself.

**Floor under a squeeze** (`floor_deflection()`: 20 N spread over the bay floor, PLA 1997 MPa, plate tables for
a/b = 2): 1.4 mm sags 0.03–0.11 mm (1.0 mm: 0.08–0.31), 3 MPa. The cell lies on the floor with 0.3 mm of swell room.

**Local thickening.**
- **Parting line:** the 3.0 front fillet reaches below the parting line. A 0.2 mm band inside the top 1.5 mm of
  the wall (45° underneath; `rim_band`), with the cover's lip moved in by as much, keeps the tray's top edge ≥ 1 mm.
- **Floor corner:** a 0.8 mm 45° fill along the inside floor corner (`floor_chamfer`).
- **Loop:** a 1.5 mm boss inside the end wall behind the lug, between the shelf and the cover lip.
- **Snap latches (B):** a pad inside the wall brings each tongue to 2.0 mm (`latch_t`); the latch tongues start at
  y 5.8 to clear the button's roofed slit.

**Wall test plate (A1 mini).** Three closure-A trays by layer in filament 1, the wall / floor raised on the floor's
inner face: «1.0» (1.0 / 0.8), «1.25» (1.25 / 1.0), «1.6» (1.6 / 1.2). `build_bambu.py walltest`. The owner
printed it on 2026-09-28 and picked 1.6; the 1.0 tongue was too thin, 1.25 and 1.6 printed clean.

**Battery in the bay.** The cell lies with its protection board toward the button side; the leads go over the
stop rib through its 8 × 4 notch to the JST. Gaps: 0.3 to the rib, 5.6 on the switch side, 9.2 on the button side,
2.9 to the loop end. The ledge is cut back only at the switch-side far corner: that cell corner is 0.4 mm from the
curved wall and would sit 1.1 mm under the 1.5 ledge (the case is 1.8 mm off-centre toward the Grove side).

### Printing notes (v1.1)

**Ragged countersinks: root cause.** The owner printed plate 2 of the A1 mini project; 3 of the 4 countersinks
came out ragged. The G-code shows the same toolpath at all four holes: inner wall, then outer wall, no overhang or
bridge moves, no support in the cone. So the model's geometry does not tell the clean hole from the others. The
project's printer and process settings do:
- `build_bambu.flatten()` ignored the `include` lists of Bambu's system presets, so every project up to PR #1180
  carried `fdm_machine_common`'s generic start G-code instead of the printer's own. On the A1 mini that G-code
  heats the nozzle to a fixed 205 °C and prints the whole tray there (the preset says 220). It also skips the
  nozzle wipe, the flow calibration and the plate routine.
- The plate type was the preset default, Cool Plate, at 35 °C. The owner prints on textured PEI, which wants
  65 °C. A cool plate leaves a 4.1 mm first-layer ring on a textured surface with poor grip. Which ring lifts is
  down to where it sits on the plate, which fits one clean hole out of four.
- Travel used the sloped «Auto Lift». After each ring of the cone the nozzle wiped and then moved straight across
  the hole while still rising (0.07–0.11 mm over the fresh layer), which drew a hair in every hole on every cone
  layer.

Fixed in `bambu/build_bambu.py`: the includes are resolved (the real start G-code, `M109 S[nozzle_temperature_initial_layer]`),
and `PROCESS_OVERRIDES` sets a Textured PEI plate (65 °C), `reduce_crossing_wall` (avoid crossing walls),
`z_hop_types` Normal Lift (lift first, then travel), and 0.1 mm elephant-foot compensation. These are all safe
defaults: avoiding walls only adds a little travel, and Normal Lift is Bambu's standard lift. The same changes cut
the stringing the owner saw on the walls (print-log item 6). If hairs remain, dry the Overture Matte spool.

## Where the files live

This folder holds the source only: the model, the scripts and this README. The scripts write their output next to
them (`print/`, `preview/`, `guide-figures/`, `build/`, and the projects in `bambu/`); those folders are
gitignored here. After a rebuild, copy what changed into the published folders:

| Output | Published copy |
| --- | --- |
| `print/` (STL, STEP, JSON, shopping list) | [`../published/keyring/case-3d/print/`](../published/keyring/case-3d/print/) |
| `bambu/` projects, `stats.*`, previews, MakerWorld draft | [`../published/keyring/case-3d/bambu/`](../published/keyring/case-3d/bambu/) |
| `guide-figures/` | [`../published/keyring/case/guide-figures/`](../published/keyring/case/guide-figures/) |
| selected renders | [`../published/keyring/case/renders/`](../published/keyring/case/renders/) |
| case v2: `print/v2/` → `v2/print/`, `print/v2flat/` → `v2/print-flat/`, `bambu/v2/` → `v2/bambu/`, `preview/render-v2*` → `v2/renders/`, other `preview/*-v2*` → `v2/line-art/` | [`../published/keyring/case-3d/v2/`](../published/keyring/case-3d/v2/) |

The `preview/` images named below are working images and are not in the repository; the full set of rounds is on
the branch `claude/keyring-case-v1` (PR #1159). The model reads Seeed's STEP files from `../guide/cad`, which
arrives with the assembly guide source (PR #1128).

## Run

```sh
# from the repository root: the venv lives at <repo>/.venv, which render_all.sh expects (override with PY=)
uv venv .venv --python 3.12 && uv pip install --python .venv/bin/python cadquery pillow numpy cairosvg matplotlib scipy
source .venv/bin/activate
cd hardware/raily-pin-public/case
export DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib      # macOS: cairo for cairosvg
python keyring_case.py stage2        # bodies -> print/ (STL per filament body + STEP assembly + report.json),
                                     # preview/case-*.png line art, preview/tab-section.png, build/stage2 for Blender
./render_all.sh                      # preview/render-*.png (Blender, one job at a time)
python guide_figures.py              # guide-figures/: overview, exploded, closed, hanging, step-1..6, step-5b, colour maps
python closure_study.py              # preview/closure-*.png sections and print/closure-study.json (options A, B, C)
python atlas_layers_p1.py <new_dir>  # atlas line-art layers of edition B (17.1 + 17.3c + 18.1e + DA7280) in the
                                     # Keyring Air C3 format (../case-compact/atlas_layers.py): one SVG per part,
                                     # assembled.svg, the visible-only masks, layers.json with exploded offsets, checks
python bambu/build_bambu.py profiles && python bambu/build_bambu.py prepare && bash bambu/run.sh
                                     # Bambu Studio projects, see bambu/README.md
python shelf_variants.py build       # shelf variants A-F: print/shelf-variants/ (STL + report.json), preview/shelf-*.png
python shelf_variants.py bambu       # the A1 mini test plate with all six (bambu/out/a1m-shelf-variants/)
python keyring_case.py variants      # corner variants a/b/c (history of the shape decision)
python keyring_case.py tab           # side-button flexure numbers
python keyring_case.py cable         # Grove cable coil budget
```

## Frame

Seeed's Expansion Board STEP frame, in mm:

- **x** is the long axis: x min is the bottom end (XIAO USB-C), x max is the top end (loop).
- **y** is the thickness: +y is the front, the component side.
- **z** is the width: +z is the long side with Button D1 and Grove A0/D0.

## Size

| | W × H × D, mm |
| --- | --- |
| Case (v1.1) | **50.4 × 89.5 × 20.3** at the rim; 24.0 over the facet crown (7°) (v1: 51.2 × 97.8 × 20.3; 23.7) |
| With the loop | 97.6 long (v1: 106.1) |

## Printed parts and colour bodies

One STL per colour body; the geometry is the same for every scheme of the same front type.

- `print/keyring-case.step`: the same bodies as one named assembly, coloured in the default scheme (V1).
- `print/bodies.json`: the body list.
- `print/filaments.json`: the filament of every body in every scheme.
- `print/report.json`: the check results.

| Print | Body | Role | cm³ |
| --- | --- | --- | --- |
| tray | `keyring-tray-tray.stl` | body colour | 10.48 (v1: 16.0) |
| tray | `keyring-tray-button.stl` | the side-button tongue: body colour (V1) or front colour (V5) | 0.14 |
| tray | `keyring-tray-loop.stl` | body colour (V1) or front colour (V5) | 0.26 |
| cover | `keyring-cover-cover.stl` | the front, one colour | 13.35 |
| cover | `keyring-cover-led.stl` | the 0.3 mm skin over the RGB LED | 0.003 |
| shelf | `keyring-shelf-shelf.stl` | inside, body spool | 1.04 |

These are closure A. `print/closure-b/` holds the tray and cover bodies of closure B (the shelf is the same).

### Facet angle and thickness

The facets are set by their tilt (`P.facet_angle`, the four X arms). The corner facets are steeper, and the crown follows from the tilt over the actual facet run, from the centre diamond to the panel edge. The rim is kept as low as the cover lip allows: the front plate is 1.0 mm over the cavity at the rim.

| Arm facets | Corner facets | Crown over the rim | Total thickness (back to the diamond top) | Change |
| --- | --- | --- | --- | --- |
| 4° (round 5) | 7° | 2.0 mm | 22.3 mm | — |
| **7° (default now)** | 12° | 3.4 mm | **23.7 mm** | +1.4 |
| 10° | 18° | 4.8 mm | 25.1 mm | +2.8 |
| 14° | 24° | 6.8 mm | 27.1 mm | +4.8 |
| 10°, capped at 2.0 mm (`crown=2.0`) | 18° | 2.0 mm | 22.3 mm | 0 |

This table is from v1 (51.2 × 97.8 mm, 20.3 mm at the rim in every row); v1.1 keeps the 20.3 rim (the crown is 24.0 at 7°).

The capped option keeps today's thickness. The facets run at the steeper angle only until they reach the rim level, and the outer field is flat. So the X arms end in short creases instead of running to the edge.

Renders: `preview/render-v1-front.png` (7°), `render-angle10-front.png`, `render-angle14-front.png`, `render-angle10cap-front.png`, and the sheet `sheet-angles.png` in that order.

The round-5 conclusion that 4–7° reads only faintly was partly a render fault:

- The Blender import smoothed the normals across the facet creases, and the pale fronts clipped to white.
- Both are fixed: the creases now stay sharp (`shade_smooth_by_angle`, 1.5°), the exposure is −0.35, and the studio key is a single directional light high on the left, like a window.
- With that, 7° reads clearly and 14° reads strongly.
- Under a fully diffuse, overcast light, any matte relief fades.

The front has no screw holes (closure A or B), so the facet angle no longer changes any counterbore depth.

### Shipping colourways: V1 and V5

| | V1 | V5 |
| --- | --- | --- |
| Tray body, shelf | Matte Cotton White | Matte Cotton White |
| Loop, side button | Matte Cotton White | Matte Pastel Periwinkle |
| Front (cover) | Matte Pastel Periwinkle | Matte Pastel Periwinkle |
| LED skin | Matte Pastel Periwinkle, 0.3 mm | Matte Pastel Periwinkle, 0.3 mm |
| Spools | 2 | 2 |

**LED skin:** 0.3 mm is the default (`P.led_skin`). The owner printed the coupon on 2026-09-28 and held it over the real board LED: the 0.3 mm skin shows the light clearly better than 0.4 mm, which was the default until then. At 0.16 mm layers the 0.3 mm skin prints as a bridge layer and a top-surface layer; the slice closes the Ø3.0 spot on the coupon and on the case. `print/keyring-led-coupon.stl` is a 34 × 16 × 2.4 mm test piece (1.7 g) with both skins over the same Ø3.0 pocket as the case. One edge notch marks 0.3 mm (the case), two mark 0.4 mm. Print it in the Periwinkle spool and hold it over a lit LED.

**Shopping list:** exact product names, grams per case, purge and the Bambu AMS slot plan are in [`print/shopping-list.md`](../published/keyring/case-3d/print/shopping-list.md).

**Archived schemes:** `v1_whiteled`, `v2`, `v3`, `v3_gallery`, `v4` and the zoned schemes stay in `SCHEMES` with `archived=True`, listed under `archived` in `print/filaments.json`. Nothing was deleted.

**Renders:** `preview/render-v1-{iso,front}.png`, `render-v5-{iso,front}.png` and the sheet `preview/sheet-final.png` (V1 iso, V1 front, V5 iso, V5 front).

### Earlier solid-front variants (archived)

| Variant | Body (tray) | Loop, button | Front | LED skin | Spools |
| --- | --- | --- | --- | --- | --- |
| `v1_whiteled` | Cotton White | Cotton White | Pastel Periwinkle | Cotton White piece | 2 |
| `v4` | Charcoal Black `#2F2E30` | Pastel Periwinkle | Pastel Periwinkle | Pastel Periwinkle | 2 |
| `v2` | Charcoal Black | Charcoal Black | Pastel Periwinkle | Pastel Periwinkle | 2 |
| `v3` | Army Blue `#2E4462` | Army Blue | Raspberry Blue `#5472D0` | Pastel Periwinkle piece | 3 |

- By Polymaker's TD, Cotton White (4.1 mm) passes more light than Pastel Periwinkle (1.5 mm). The owner chose no white dot, so both shipping colourways use the Periwinkle skin.
- LED close-ups from round 6: `preview/render-led-v1-periwinkle04.png`, `render-led-v1-white04.png`.

### Zoned fronts (option, `P.front_zones = True`)

The zones follow the owner's sketch. The X is read from the colour boundaries; there are no inlay lines any more.

- **Zone 2:** the centre diamond.
- **Zone 1:** the four diamonds of the same size that share its edges, forming a cross.
- **Zone 3:** everything else on the front, including the rim and the cover's rounded edge.

Geometry:

- The lattice is centred on the case.
- The diamond half-axes are 21.3 mm along the length and 10.5 mm across, so the outer diamonds span 97 % of the faceted panel's width and 95 % of its height.
- The facet creases sit on the centre diamond's edges, which are the inner edges of zone 1. The outer edges of zone 1 are flat colour boundaries.
- Zones 1 and 2 are a 1.0 mm skin under the facet surface over a zone-3 base. The printer changes colour only in the top 5 layers.

### Colour schemes

Every scheme uses at most 4 spools, all Polymaker Panchroma Matte PLA. The hex and TD (transmission distance) values are from Polymaker's published table (wiki.polymaker.com, «HEX Codes and Transmission Distances»). Raspberry Blue (CA04092) is not in that table; its hex comes from the colourway agent.

| Spool | SKU | Hex | TD, mm | Brand role |
| --- | --- | --- | --- | --- |
| Cotton White | CA04016 | `#F4EFEB` | 4.1 | body |
| Charcoal Black | CA04015 | `#2F2E30` | 0.1 | body |
| Ash Grey | CA04014 | `#485155` | 0.1 | graphite front |
| Army Blue | CA04008 | `#2E4462` | 0.1 | deep (deepBlue role) |
| Raspberry Blue | CA04092 | `#5472D0` | — | mid (railyBlue role) |
| Pastel Periwinkle | CA04036 | `#ADB4E6` | 1.5 | pale (lilac role) |

The brand blues are more saturated than any matte PLA. Army Blue → Raspberry Blue → Pastel Periwinkle is picked for a clear lightness step (L\* 28 → 50 → 74), not for a hue match. Electric Indigo `#6858A9` is the more violet deep option, but it steps less from Raspberry Blue (L\* 42).

| Scheme | Body | Zone 3 | Zone 1 | Zone 2 | LED spot | Spools | Light |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `white_deep` | Cotton White | Pastel Periwinkle | Raspberry Blue | Army Blue | Cotton White | 4 | studio |
| `white_pale` | Cotton White | Army Blue | Raspberry Blue | Pastel Periwinkle | Cotton White | 4 | studio |
| `black_deep` | Charcoal Black | Pastel Periwinkle | Raspberry Blue | Army Blue | Pastel Periwinkle | 4 | studio |
| `black_pale` | Charcoal Black | Army Blue | Raspberry Blue | Pastel Periwinkle | Pastel Periwinkle | 4 | studio |
| `graphite_g1` | Charcoal Black | Ash Grey | Charcoal Black | Raspberry Blue | Pastel Periwinkle | 4 | gallery |
| `graphite_g2` | Charcoal Black | Charcoal Black | Army Blue | Raspberry Blue | Pastel Periwinkle | 4 | gallery |

In every scheme the loop and the button are in the body colour.

The LED spot is always the palest spool of the set. In the graphite sets that is Pastel Periwinkle, their 4th spool; Charcoal, Ash Grey and Army Blue have a TD of 0.1 mm and would not pass the LED.

Renders use `preview/render-<scheme>-{iso,front}.png`, with overview sheets in `preview/sheet-schemes-{iso,front}.png` (order: row 1 white_deep, white_pale, black_deep; row 2 black_pale, graphite_g1, graphite_g2). Two light settings come from the brandbook ads:

- **studio:** after `4K/01-fold-sequence-white`. Pale paper background `#EDEEF1`, a large soft key light, a soft shadow.
- **gallery:** after `4K/01-gallery-dark`. A near-black room, blue rim lights, a reflective dark floor, and the RGB LED lit.

### RGB LED spot

The XIAO's RGB user LED is the part named `USR` in `xiao.step`. As fitted it is 1.0 × 1.0 mm at x 121.77–122.77, z −5.32…−4.32, top y 6.94, next to the USB-C. The charge LED (`LED`) sits beside it at x 119.80–120.43.

| | |
| --- | --- |
| LED top to the cover's inner face | 4.28 mm |
| Front zone at the LED (zoned option) | zone 3, but its centre is only 0.64 mm from the zone 1 edge, so the Ø3 spot straddles both zones; that is why the spot is its own body |
| USB-C opening | outer face at x 117.76, 4.5 mm short of the spot |

Two options are modelled (`P.led_mode`):

- **Skin (`skin`, default):** a Ø3.0 pocket from inside leaves a thin skin over the LED, flush with the facets. The shipping value is 0.3 mm (`P.led_skin`, see [Shipping colourways](#shipping-colourways-v1-and-v5)); 0.4 mm was the value until 2026-09-28.
  - That skin is its own body (`keyring-cover-led.stl`) in the scheme's LED spool: Pastel Periwinkle (TD 1.5) in V1 and V5.
  - Round 3 started at 0.6 mm (3 layers at 0.2 mm) in the palest spool; that is history.
- **Through-hole (`hole`):** a Ø2.0 hole straight over the LED.
  - The LED sits 6.5 mm below the surface, so the eye sees the die only within about 13° of looking straight down the hole.
  - Otherwise the hole is a dark well with a faintly lit rim, and it lets dust and water onto the XIAO.

The round-3 LED renders show both options: `preview/render-led-skin06.png`, `render-led-skin04.png`, `render-led-hole.png`. They predate the colour zones, so the front in them still has the X lines.

**Round-3 recommendation (superseded):** skin, 0.6 mm. The locked choice is 0.3 mm Periwinkle (0.4 mm until the coupon test on 2026-09-28); `print/keyring-led-coupon.stl` tests 0.3 against 0.4 mm.

Off, the spot reads as a small pale dot on the darker zones.

## Internals

- **Fastening:** see [Closure](#closure).
- **Board clamp:**
  - The PCB sits on Ø5.0 tray standoffs.
  - Cover pillars come down to 0.1 mm above the PCB top: Ø5.8 over y 1.5, a 45° cone from y 0.7 to 1.5, Ø4.2 below that.
  - The lower pillar diameter clears Reset and D1 by 0.4 mm, and the upper one clears the JST by 0.4 mm (STEP).
- **Cover lip:** 1.0 × 1.5 mm into the tray, 0.2 mm clearance.
- **Bay:** the cell lies on the floor.
  - A 1.2 mm stop rib separates the bay from the JST zone, with an 8 × 4 mm notch for the leads.
  - A drop-in **shelf** (separate print) rests on 1.5 mm ledges 0.3 mm above the cell. The ledges are cut back around the cell's corners.
  - The motor stands on four corner posts on the shelf, 3.1 mm tall: its through-hole tails hang between them, and 1.2 mm fences locate the PCB.
  - Fix the motor with a small square of double-sided tape (foam tape up to 1 mm is best; any double-sided tape works).
- **USB-C:**
  - v1.1: a 9.8 mm wide window with r 1.6 corners and a 0.25 mm lead-in, open to the parting line, in a flat 1.25 mm end wall. The receptacle mouth is 0.2 mm under the outer face (v1: a 13.0 × 7.2 overmold-sized opening, mouth 8.3 mm deep). See [Revision v1.1](#revision-v11).
  - Check: a spec-max plug with its overmold on the outer face and its 8.4 × 2.6 shell 6.45 mm into the receptacle; no overlap.
- **Power switch:**
  - The slot is 3.7 × 2.5 mm in the −z wall: x 132.86–136.56, y −0.11–2.42 (v1.1; v1 y −1.51–0.42).
  - It is sized from the lever in the STEP (x 134.71–136.16, y −1.11–0.02) raised 1.7 mm (the real lever, v1.1). The travel is taken as one lever width (1.45 mm) toward −x, plus 0.4 mm clearance; 0.7 mm up and down.
  - The lever tip sits 0.3 mm inside the wall's inner face, so you switch it with a pen tip through the slot.
- **Side button:** a print-in-place flexure over D1; v1.1 tongue 1.25 × 5 mm, 16 mm long, under a 45° roofed slit, the nub a vertical rib (see the table below and [Revision v1.1](#revision-v11)).
  - The Grove cable must run above y 2.5 past the nub.
- **Grove cable:** the kit's 20 cm cable.
  - About 50 mm runs A0/D0 → along the +z wall → motor socket.
  - The remaining 136 mm coils in the free zone over the A0/D0 shroud row: x 143.5–166, z 13.4–24.2, y 3.8–9.4 (22.5 × 10.8 × 5.6 mm, free of case and board parts).
  - That is two zig-zag stacks of 4.4 mm ribbon, 20 mm folds, 4 layers each: 4.4 mm tall, which fits in 5.6.

### Side-button numbers

| Item | Value | Source |
| --- | --- | --- |
| Actuator out of the switch body | 1.00 mm | STEP |
| Nub gap at rest | 0.15 mm | parameter |
| Switch travel | 0.25 mm assumed (D1 part number not in the STEP) | typical |
| Tongue (v1.1) | 1.25 thick, 5.0 high, hinge to the D1 centre 13.5 (v1: 1.0, 11.5); nub a 4.4 mm rib (v1: 1.4) | parameter |
| Flexure stiffness / force at 0.40 mm stroke | 2.0 N/mm / 0.79 N plus the switch's own force (v1: 2.0 / 0.79) | E 1997 MPa, Panchroma Matte TDS V2.1 |
| Root strain at actuation / at the hard stop (1.15 mm) | 0.41 % / 1.2 %, against 7.2 % at break (v1: 0.45 / 1.3) | eSUN PLA-Matte Rainbow TDS |

## Shelf variants A–F (test fit, 2026-09-28)

The owner test-fitted the v1 shelf in a printed tray. Three things came out of it:

- The cell slides along its length (z).
- The shelf goes in upside down too easily.
- The corner posts are bulky.

`shelf_variants.py` builds six drop-in shelves for the **printed** v1 trays. Closures A and B have the same bay,
and the tray is not changed. Since case v1.1 the script builds from `version_p('v1.0')`, the printed trays'
2.0 / 1.4 walls: its output and checks are unchanged. The thinner v1.1 walls give way outward, so the bay's inner
faces stay where they were; but v1.1 adds a 0.8 mm fill along the inside floor corner, and D's floor stops were
checked against the v1 trays only. Every variant:

- **Rests where v1 does:** on the 1.5 mm ledges, 0.3 mm over the cell envelope. A tongue also rests on the
  stop-rib top, but not over the lead notch or the JST plug (z 6.4–15.4).
- **Holds the motor at the v1 x/z and height** (PCB underside at y 4.22, the v1 post tops), so the cover,
  Grove-cable and interference numbers above still hold. The four corner posts become three thin rails
  (1.0 mm fence + 0.8 × 0.8 mm lip under the PCB edge):
  - −x rail: 9 mm.
  - −z rail: 12–16 mm, ending in a hook round the +x/−z PCB corner. The +x/+z corner is 0.5 mm from the
    curved end wall.
  - +z rail: 10 mm, below the Grove socket.
- **Goes in only rails-up.** The −x rail stands over the stop rib. Turned over, it lands on the rib top
  (31 mm³ overlap in the model), so the shelf sits 4.3 mm proud and the cover does not close. Tested in
  `report.json → upside_down`.
- **Has a letter and an arrow** embossed 0.4 mm on the top face.
- **Keeps the lead path.** The +z end of the cell has no wall from the rib to x 187, so the leads from the
  PCM end rise past the cell corner and run into the rib notch. Put the cell in with the PCM end toward +z,
  the Grove/JST side.

**Why no wall along the long sides of the cell:**

- The cell is 0.3 mm from the stop rib.
- On the far side, the ledge ring comes within 0.9 mm of the cell (the ledge is cut back around its corners).
- The rib and the curved end already hold the cell across. In the bare bay it moves 1.1–1.7 mm toward −z
  and 4.7–5.3 mm toward +z (envelope / the owner's 20 × 32 cell), so the fix is at the two ends.

| | What holds the cell | Cell play along z | Solid g | Print |
| --- | --- | --- | --- | --- |
| **A** cradle | two end walls under the shelf, 1.2 mm thick, 2.5 mm deep, 0.3 mm off the cell ends, 45° lead-in | 0.6 mm | 1.5 | on edge |
| **B** corner clips | four end fingers, 1.2 mm, 3.5 mm deep, two per end | 0.6 mm | 1.5 | on edge |
| **C** gripping cradle | A, plus a 8 mm flex tongue in each end wall with a bump 0.3 mm into the cell (0.8 % strain, about 1 N per tongue) | 0 (held) | 1.5 | on edge |
| **D** floor stops | two 1.8 mm pads on the bay floor at the cell ends, 0.3 mm off the bay wall under the ledges; flat shelf | 1.2 mm | 1.4 + 0.2 + 0.4 | flat |
| **E** thin rails (the owner's idea) | one 1.0 × 10 mm rail, 2.5 mm deep, under the shelf at each cell end | 0.6 mm | 1.5 | on edge |
| **F** cradle, loose | A with 0.5 mm off the cell ends, in case the pocket prints tight | 1.0 mm | 1.5 | on edge |

C is the owner's «cell in the shelf» idea: push the cell up into the shelf, lower both into the bay, and close
the cover. With A, B, E and F the cell goes into the bay first, and the shelf's walls drop over its ends.

The cell is never glued or taped (EU Battery Regulation 2023/1542 Art. 11):

- A, B, D, E, F: lift the shelf and the cell comes out.
- C: push the cell down out of the tongues with a finger.

**D's pads go in before the cell.** Set each pad down in the middle of the bay, 1.3 mm in from its seat
toward the middle and toward the rib, then slide it out under the ledge to the wall. The model checks that
the lowered pad clears the ledge ring and the rib (`report.json → floor_stop_insertion`).

**Printing:**

- A, B, C, E and F print standing on their rib-side edge: the tongue and the −x rail on the plate, with a
  5 mm brim. Walls, rails and tongues stand vertical, every downward face is 45° or steeper, and nothing
  needs support.
  - The only horizontal spans are a 9 mm bridge (the tongue gap over the notch) and a 1.5 × 1.2 mm ledge
    under the hook.
- D's shelf and pads print flat.

**Check** (`report.json`):

- No overlap with the A or B tray, the covers, the cell envelope, the motor or the cable zone. The only
  overlap is C's grip bumps into the cell, 0.84 mm³ by design.
- The shelf touches the ledges and the rib top, and the motor touches the rail lips, both by design.

**Files:**

- `print/shelf-variants/shelf-{A..F}.stl`, `shelf-D-floor-stop-{minus,plus}-z.stl`: already in print
  orientation.
- `report.json`.
- The A1 mini project with all six on one plate: `bambu/out/a1m-shelf-variants/`, published as
  `shelf-variants-a1m.3mf`.
  - Plate: Cotton White in slot 1, 0.16 mm layers, no supports, by layer.
  - Slicer estimate: 48 min and 9.9 g, brims included.
- Previews: `preview/shelf-variants-{top,bottom}.png`, `shelf-seated-{C,D}.png`,
  `shelf-seated-{A,C}-section.png`.

**How to test** (one plate, white):

1. For each letter: cell into the bay with the PCM end toward +z, leads through the notch. For C, push the
   cell up into the shelf first.
2. Shelf in, rails up. It should drop flat onto the ledges with no rocking.
3. Push the cell toward ±z with a toothpick. Note the play.
4. Motor onto the rails; check that the hook and the fences locate it.
5. Close the cover (closure A or B). Check that it closes flush and that the Grove plug still goes in.
6. Try the shelf upside down: it must not sit flat.
7. For C: lift the shelf and check that the cell comes with it; push the cell out.

## Interference check

Run with `keyring_case.py stage2`, against every printed body. There is no overlap anywhere.

| Inside part | Min gap | Note |
| --- | --- | --- |
| Expansion Board PCB | 0 | sits on the standoffs by design; cover pillars 0.10 above |
| D1 actuator | 0.15 | nub rest gap, by design |
| CR1220 holder (lowest part) | 0.30 | floor |
| D1 body / Reset | 0.39 / 0.40 | pillars |
| JST | 0.42 | pillar |
| Power switch | 0.50 | slot |
| XIAO (USB-C) | 0.30 | the receptacle under the cover (v1.1, XIAO +2.0; v1: 1.37) |
| Headers | 2.78 | |
| LiPo (20.5 × 32 × 6.7 envelope) | 0 | rests on the floor by design; 0.3 swell gap under the shelf |
| Motor (20 × 24 × 9.8 envelope) | 0 | rests on its posts by design; 0.3 under the cover |
| USB-C plug (spec max: overmold 12.35 × 6.5, shell 8.4 × 2.6 × 6.65) | 0.05 | overmold on the outer face by construction |
| Printed parts against each other (cover, tray, button) | no overlap | |
| Grove cable zone | 0.30 | free of board parts too |
| RGB LED to the skin underside | 5.91 | glow only; nothing touches the LED |

## Closure

The owner asked to keep the front clean (2026-09-28). The model builds three closures (`P.closure`); A is the default, B the alternate, both in the print files and the Bambu projects. `closure_study.py` draws the sections (`preview/closure-*.png`) and writes the numbers (`print/closure-study.json`).

| | A: screws from the back (default) | B: snap-fit (alternate) | C: sliding lid (not built) |
| --- | --- | --- | --- |
| Hardware | 4 × ISO 7046-1 M2 × 16 countersunk (H0 cross), 4 × ISO 4032 M2 nuts | none | none |
| Outside | four flush heads in the back; clean front | four L-shaped latch slits on the long sides; pry notch beside USB-C; clean front and back | white frame around an inset lid |
| Width × thickness | 50.4 × 20.3 mm at the rim (v1.1) | no change | +2.0 mm wide (3 mm walls for the rails) |
| Opening the case (EU Batteries Regulation Art. 11) | a PH0 screwdriver, a commercially available tool | no tool: fingernail or coin in the pry notch | no tool |

**A.** The heads sit flush in 90° countersinks (Ø4.1) in the tray back, through the standoffs and the board holes. The nuts slide sideways into slots in the cover pillars, which open toward the middle of the case; the slot is AF + 0.1 wide, so a nut stays put while the cover is turned over. Around the slot the pillar is Ø7.4, from 1 mm under the slot to the front plate, on a 45° cone. The nut sits at y 5.82–7.42 and the screw tip at y 7.92, 3.3 mm under the front plate, so the 7° crown and the rim are untouched.

**B.** Each latch is a tongue cut into the long wall like the side button: hinge toward the middle of the straight side, free end toward the case end, top edge = wall top under the cover rim. A catch block under the front plate carries a bump that snaps into a groove on the tongue's inner face. Bumps at x 140 and 178 on both long walls. The retention faces are 45° and the lead-in 30°, so both parts print without support. Locating pins on the standoffs hold the board in place of the screws.

| B: latch numbers (`snap_mechanics()`, Bayer snap-fit formulas) | Value |
| --- | --- |
| Tongue: length to the bump / height / thickness | 14.0 / 6.4 / 2.0 mm (v1.1: a 2.0 mm pad in the 1.6 wall; the tongue starts at y 5.8, v1 6.2 high) |
| Engagement (deflection) | 0.6 mm |
| Strain | 0.92 %, against 7.23 % at break (PLA-matte proxy): about 13 %, fine for 20 open/close cycles |
| Push-on force | 26 N for the four latches (v1: 25) |
| Pull-off force | 46 N for the four latches (37–58 N for friction 0.25–0.45; 0.35 assumed, PLA on PLA) (v1: 45) |
| Pull-off / cover weight (18.9 g) | about 240 |

A 1.5 m drop mostly presses the cover onto the tray; the parts inside have 0.6 mm of play at most, so they cannot build up speed and hit the cover off from inside. A drop test is still part of the first print (see `bambu/makerworld-draft.md`).

**C (not built).** Rails need 3 mm walls (+2 mm width), the pillars would drag across the board so they would go, the lid becomes an inset panel inside a white frame, and the G2 curved ends stop a lid from sliding out lengthwise unless the sides run straight to the ends.

## Printing

- **Tray:** floor down.
  - Supports only under the loop (a painted zone in the Bambu project); nothing reaches into the side-button or latch slits.
  - The slits, switch slot and USB opening are vertical or short bridges; the countersinks (A) are open at the bed.
- **Cover:** front up; the pillars stand on the plate.
  - The front plate's inner face, the lip and (B) the catch blocks hang over the plate, so they need supports: grid supports, 4 mm apart, only inside the painted zone under the inner face. A blocker keeps them off the LED skin, which bridges its Ø3.0 pocket. The nut slots (A) bridge their roof and get no support.
  - The faceted front is the top surface; 0.16 mm layers.
- **Shelf:** flat, posts up. The test variants A, B, C, E, F print on edge (see [Shelf variants](#shelf-variants-af-test-fit-2026-09-28)).

## Assembly order (for the guide)

Closure A:

1. Cell into the bay, leads through the notch.
2. Shelf onto its ledges, motor onto its posts.
3. Board onto the standoffs.
4. Plug in the JST and the Grove cable; coil the cable over the A0/D0 row.
5. Nuts into the four pillar slots of the cover (the cover lies front down).
6. Cover on.
7. Turn the case over; four screws from the back.

Closure B: steps 1–4, then press the cover on until the four latches click. To open, a fingernail or coin in the pry notch beside USB-C.

## Bambu Studio projects

`bambu/` holds the print-ready Bambu Studio projects for V1 and V5 (closure A and B plates, a five-case V5 batch
plate, a V1 set for printers without an AMS), for the X1 Carbon and seven other Bambu printers, with the slice stats
of every plate, sliced previews and the MakerWorld listing draft. See `bambu/README.md`.

## Guide figures

`guide_figures.py` writes to `guide-figures/`, as SVG and PNG: `overview`, `exploded`, `closed`, `hanging`, the assembly steps `step-1` … `step-6` and `step-5b`, and the colour maps `colour-v1-{front,iso}` and `colour-v5-{front,iso}`. In each step the part being added is dark and lifted above its seat; every other part is grey. The steps (closure A) are: 1 cell into the bay, 2 shelf and motor, 3 board, 4 nuts into the cover's pillar slots (cover seen from below), 5 cover with its nuts, 6 screws from the back. `step-5b` is closure B: the snap-fit cover pressed on (the latch slits show on the side). The colour maps fill each piece in its filament colour. They use the guide's renderer and style, contain no text, and the guide adds its labels as vector overlays. The guide takes them from `../published/keyring/case/guide-figures/`.

## Measured (from the STEP files)

| Item | Value |
| --- | --- |
| Expansion Board PCB | x 119.50–177.50, z −20.39–22.11 (58.0 × 42.5), corner r 3.0, y −3.20…−1.60 |
| Mounting holes | Ø3.0 at (123.49, 18.36), (173.49, 18.36), (123.49, −16.64), (173.49, −16.64) |
| Lowest part under the board | CR1220 holder, y −6.38 |
| Tallest part | XIAO USB-C, y 9.85 (XIAO on 2.54 mm header spacers on the 2.90 sockets) |
| XIAO USB-C | mouth x 117.96, y 5.65–9.85, z −3.58–5.36 |
| XIAO RGB LED (`USR`) | x 121.77–122.77, y 6.64–6.94, z −5.32…−4.32 |
| XIAO charge LED (`LED`) | x 119.80–120.43, y 6.64–6.94, z −5.45…−4.18 |
| JST | mouth x 176.89, z 6.89–15.04, top y 3.79 |
| Grove A0/D0 | x 143.53–153.53, mouth z 21.36, top y 3.50 |
| Power switch lever | x 134.71–136.16, y −1.11–0.02, tip z −21.47 |
| Button D1 | body x 166.40–171.10, face z 22.01; actuator x 167.78–169.73, y −1.06…−0.21, tip z 23.01 |
| Reset button | x 125.99–130.69, z 19.36–22.01, top y 0.31 |

## Datasheet values

| Item | Value | Source |
| --- | --- | --- |
| 602030 LiPo with PCM | envelope 20.5 × 32 × 6.7 | EEMB LP602030, `docs/pins/eu-compliance.md` |
| Grove Vibration Motor 105020003 | 20 × 24 × 9.8 (parameter) | Distrelec product data, 30069914 |
| Grove HY2.0 plug | body 7.0 long | `../guide/cad/parts.py` |
| USB-C plug overmold | ≤ 12.35 × 6.5 | USB Type-C specification |
| M2 screw / nut (closure A) | ISO 7046-1 countersunk dk 3.8, k 1.2; ISO 4032 s 4.0, m 1.6 | ISO |
| Panchroma Matte Young's modulus (X-Y) / density | 1997 MPa / 1.37 g/cm³ | Polymaker Panchroma TDS V2.1 |
| PLA-matte elongation at break (proxy; not in the Panchroma TDS) | 7.23 % (XY) | eSUN PLA-Matte Rainbow TDS |

## Parameters

`P` in `keyring_case.py` holds every design parameter, each with a comment. The main groups:

- shape: `corner`, `g2_req`, `g2_n`, `wall`, `floor`, `cover`, `crown`, `rim_w`, fillets;
- front and LED: `lattice_fill_w`, `lattice_fill_h`, `zone_depth`, `led_mode`, `led_d`, `led_skin`, `led_hole_d`;
- closure: `closure` ('back' = A, 'snap' = B, 'front'); A: `back_screw_len`, `csk_d`, `nut_*`, `nut_boss_d`; B: `latch_*`, `pry_notch`, `pin_d`; both: `standoff_d`, `pillar_*`, `lip_*`;
- bay: `rib_t`, `lead_notch`, `ledge_*`, `shelf`, `motor_post`, `fence*`;
- side button: `tab_*`, `nub_*`, `pad`;
- openings: `usb_*`, `switch_slot_clr`;
- cable: `cable_len`, `cable_w`, `cable_t`;
- colour: `scheme`, a key of `SCHEMES` (spools in `FILAMENTS`); brand tokens are in `PALETTE`.
- OLED window (v2): `window` ('none', 'open', 'lip'), `window_inset`, `window_r`, `window_gap`, `window_bezel`, `window_chamfer`,
  `window_ledge`, `window_wall`, `window_deg`, `window_kink_*`, `window_top_chamfer`, `window_field*`, `window_lip`;
  the case versions are `VERSIONS` / `version_p()`.

## Previews

- `preview/render-{v1,v2,v3,v3_gallery}-{iso,front}.png` and `sheet-solid.png`: the current solid-front variants.
- `preview/render-<zoned scheme>-{iso,front}.png` and `sheet-schemes-{iso,front}.png`: round 4, the zoned-front option.
- `preview/case-{front,back,side,iso}.png` and `tab-section.png`: line art.
- `preview/usb-usb-{end,section}.png` (v1.1 USB-C end, the XIAO at its soldered height; the section with a spec-max plug), `usb-v1-*` (the same for v1, from `origin/main` before v1.1) and the sheet `usb-before-after.png`; `tab-across.png`, `tab-across-nub.png`, `tab-v1-across.png` and the sheet `tab-before-after.png` (sections across the side-button tongue).
- `preview/closure-{A-section,A-detail,A-back,A-front,B-section,B-detail,B-side,B-otherside,C-section}.png`: the closure study.
- Earlier rounds, kept as the record of those decisions:
  - `render-{front,back,iso,tab,iso-ocean}.png` and `render-led-*.png`: round 3, the flat 4-shade layout with X lines.
  - `variant-*`, `render-variant-*`: round 2, the corner comparison.

## Case v2: OLED window

Owner request (2026-09-28): show the Expansion Board's 0.96" OLED through the front, and make the case as thin as
reasonably possible around the same board stack. v2 shares v1.1's USB-C end, side button and countersink rings (the table below compares it with v1); v2 is `version_p('v2')` (`VERSIONS` in
`keyring_case.py`), built with `python keyring_case.py stage2 v2`.

### What changes

| | v1 | v1.1 | v2 | v2flat (option) |
| --- | --- | --- | --- | --- |
| Side walls / floor / front plate at the rim | 2.0 / 1.4 / 1.0 | 1.6 / 1.4 / 1.0 | **1.6 / 1.2 / 0.8** | same as v2 |
| Front edge fillet | 3.0 | 3.0 | 2.5 (a smaller one made the slicer run the cover's supports off the plate ends) | 2.5 |
| Front | 7° facets | 7° facets | 7° facets, a flat field around the window | flat, no facets |
| OLED | covered | covered | window (open cut-out + recess for a clear sheet) | window |
| USB-C end | round, mouth 8.3 mm deep | flat, mouth 0.2 mm deep, window open to the parting line | same as v1.1 | same as v1.1 |
| Snap latch (B) tongue / engagement | 2.0 wall / 0.6 | 2.0 pad / 0.6 | 2.0 pad / 0.7 | 2.0 pad / 0.7 |
| **Width × length, mm** | **51.2 × 97.8** | **50.4 × 89.5** | **50.4 × 89.5** | 50.4 × 89.5 |
| **Thickness at the rim / over the crown, mm** | **20.3 / 23.7** | **20.3 / 24.0** | **19.9 / 23.6** | **19.9 / 19.9** |
| Length with the loop | 106.1 | 97.8 | 97.8 | 97.8 |
| Filament per case, V1 colours (solid volume × density) | 23.8 g white, 19.0 g periwinkle | 20.3 g, 19.1 g | 19.1 g, 14.7 g | 19.1 g, 7.6 g |

The board stack and every clearance to it are shared by v1.1 and v2, including the real part heights (XIAO +2.0,
switch lever +1.7), the raised floor under the board and the USB-C end. The battery bay sets the depth (cell 6.7 +
0.3 swell + shelf 0.8 + motor 9.8 + 0.3): v2's thinner floor and front plate make it 0.4 mm thinner than v1.1; the
facet crown (3.7 mm) is the only other large term, which is why `v2flat` is offered.

Closure B in v2: since v1.1 the latch tongues get a 2.0 mm pad inside the wall (they were the 1.6 mm wall):
1.07 % strain at 0.7 mm engagement (7.2 % at break), push-on 27 N and pull-off 48 N for the four latches
(39–61 N for friction 0.25–0.45), against 25 / 45 N in v1 and 26 / 46 N in v1.1. The cover weighs about 14 g.

The 1.6 wall leaves 0.99 mm of wall top at the parting line (v1: 1.29) and 1.0 mm across the floor corner; v2 needs
neither the top band nor the floor-corner fill of v1.1.

### The window

The OLED glass sits 12 mm below the front (y −0.60; the front is at y 12.0). A plain hole in the front plate would
show the screen only straight on, and the inside of the case around it. So the cut-out is the floor of a **well**
that hangs from the front down to just above the glass.

| | Value (case frame, mm) |
| --- | --- |
| OLED glass (STEP) | x 141.92–166.60, z −7.96…5.76 (24.7 × 13.7), top y −0.60; the STEP has no active-area face |
| Active area (datasheet, assumed centred on the glass) | 21.74 × 10.86: x 143.39–165.13, z −6.53…4.33 |
| **Aperture** (glass − 0.2 per side, r 0.8, 0.3 × 45° chamfer on top) | **24.3 × 13.3**: x 142.12–166.40, z −7.76…5.56; centre 4.2 mm toward the USB end and 2.9 mm toward the switch side of the case centre |
| Bezel (well floor) | y −0.1…1.1: 0.5 mm over the glass (0.63 mm measured min gap) |
| Floor ledge around the aperture | 0.4 |
| Well walls | 1.0 thick; draft 12° toward the loop, 15° on the long sides; the USB-end wall is vertical up to y 7.6 (0.28 mm past the XIAO board), then opens at 30° |
| Top opening at the front | 31.4 × 21.5 (v2, into the flat field) / 30.9 × 21.1 (v2flat), 0.8 × 45° chamfer |
| Flat field (v2) | 1.5 mm around the top opening, 0.6 mm over the rim; the facets meet it at 45° |
| Whole viewing area in sight | ±9° across, 11° toward the loop, 5° toward the USB end (the vertical wall past the XIAO) |

The screen reads best straight on. Beyond these angles the edge rows go behind the wall first; the middle stays in
sight much further.

**Clear window (optional).** `window='lip'` (the v2 default) cuts a recess from inside: aperture + 1.0 mm per
side, 0.6 mm deep. A **0.5 mm clear acrylic or PETG sheet, 26.1 × 15.1 mm with r 1.7 corners**, drops in from
inside and is glued at its edge (a drop of UV resin or CA on the rim; keep glue off the viewing area). It then sits
flush with the bezel's underside, 0.5 mm over the glass. `print/v2/keyring-window-insert.stl` is that sheet as a
part: print it in clear PETG only as a cover plate (at 0.5 mm it is hazy, not clear), or use it as a cutting
template. With `window='open'` the recess is left out. A 1.0 mm sheet needs `window_lip=(1.0, 1.1)` and
`window_bezel=1.8` (0.4 mm left at the aperture edge).

**Firmware.** v1 hides the OLED because the firmware does not use it. v2 is only useful with firmware that draws on
the OLED. The panel's long side runs along the case, so the text must be rotated 90° in software (U8g2 `R1`/`R3`).

**Printing.** The cover prints front up as in v1. The bezel hangs 1.4 mm over the plate and gets supports from the
existing painted zone under the inner face; the well walls rise from the bezel and need none (their outer faces
lean at most 30°). All 32 projects (8 printers × 4) slice without an error or a warning, and
`build_bambu.py verify` reads every saved project back: each part prints from the planned slot (slot 1 Cotton
White: tray, shelf and, V1, loop and button; slot 2 Pastel Periwinkle: cover, LED skin and, V5, loop and button),
A1 mini included. The CLI writes a wrong `slice_info` for single-filament plates; that only touches sliced
exports, and the projects here are saved unsliced.

### Front: faceted (default) or flat

- **v2 (default):** the 7° X facets stay; around the window they stop at a flat field (0.6 mm over the rim),
  so the well opens with an even chamfer. The window sits below and beside the X centre because the OLED does.
- **v2flat:** no facets: 19.9 mm thick everywhere, 3.4 mm thinner than v2 at the crown. The window opens straight
  into the flat front. `version_p('v2flat')`, or `facet_angle=0, facet_min=0, window_field=0` on v2. Print files
  are in `print/v2flat/`; the Bambu projects are built for v2 only (`KC_VERSION=v2flat` builds them for v2flat).

### Files

- `print/v2/`, `print/v2flat/`: STL per colour body, `keyring-case.step`, `closure-b/`, `keyring-window-insert.stl`,
  `bodies.json`, `filaments.json`, `report.json` (sizes, window numbers, interference check, latch and button numbers).
- `bambu/v2/`: Bambu Studio projects of v2 for all eight printers, the same four projects as v1. Rebuild:

  ```sh
  export KC_VERSION=v2
  python bambu/build_bambu.py parts
  python bambu/build_bambu.py prepare
  ~/.local/bin/raily-heavy bash bambu/run-v2.sh
  python bambu/build_bambu.py fix
  python bambu/build_bambu.py stats
  python bambu/build_bambu.py verify           # part copies and filament slots of every saved project
  python bambu/build_bambu.py previews x1c a1m
  ```
- `preview/case-v2-{front,back,side,iso}.png`, `case-v2-window-{across,along}.png` (sections through the window),
  `render-v2-{v1,v5}-{iso,front}.png`, `render-v2flat-v1-{iso,front}.png` (`VERSION=v2 ./render_all.sh v1 v5`).

## Edition B cover 18.2-B (DA7280 motor)

The edition B cover for the DA7280 on shelf 17.3c is `version_p('v1.3-p18.2-B')`: the proven 18.2-G cover with the +z
hold-down leg and the +z hold-down boss piece removed, the dowel post at (179.4, 10.305) 2 mm shorter (it sat on the
DA7280's Qwiic plug) and the post at (179.4, -5.295) 1 mm shorter. The owner assembled it on 2026-10-03. Assembly: the
dowels go into the face first, then the frame is pressed on.

- **Dowels in three sizes.** `sml_sprues` puts one rack on the plate (`sml_rack()`): a row of 10 dowels per size, S 3.00,
  M 3.05 and L 3.10 mm, with the letter raised on each row's tab. A loose dowel is swapped for the next size up.
- **No part codes, no ТЕСТ joint.** `proto_label=None` and `test_joint=False`; build without `--proto-number`.
  `KC_NOLABEL=1` builds shelf 17.3c without its underside number.
- **Post trims.** `post_trims=((x, z, dy), ...)` shortens a dowel post by `dy`; its frame hole then runs through.

```sh
KC_VERSION=v1.3-p18.2-B python bambu/build_bambu.py cover2        # face, frame and the S/M/L rack
KC_NOLABEL=1 KC_VERSION=v1.3 python bambu/build_bambu.py shelf3c  # shelf 17.3c, no number
KC_VERSION=v1.3 python bambu/build_bambu.py parts back            # tray 17.1, no number
```

## Licence

The case model, scripts, print files and renders: **CC BY-NC-SA 4.0**, Raily.

Board models: Seeed Studio, CC BY-SA 4.0 (Printables 1336695 «Seeed Studio Expansion Board Base for XIAO with Grove OLED», 1336692 «XIAO-nRF52840 v3»). Images that show the boards (`tab-section.png`, `guide-figures/overview.png`, `guide-figures/exploded.png`) are adaptations of those models and stay CC BY-SA 4.0 with this credit.

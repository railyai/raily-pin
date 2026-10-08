# Raily Keyring case: Bambu Studio projects

Print-ready Bambu Studio projects for both shipping colourways, built from `../keyring_case.py` by
`build_bambu.py` with Bambu Studio's own CLI (02.06.00.51) and system presets. Nothing here is edited by hand.

## Files

| File | Plates |
| --- | --- |
| `v1.3mf` | 1 LED coupon (print first) · 2 V1 case A (screws) · 3 V1 case B (snap-fit) |
| `v5.3mf` | 1 LED coupon · 2 V5 case A · 3 V5 case B |
| `v5-batch.3mf` | 1 five V5 trays + five shelves (A) · 2 five V5 covers (A) |
| `v1-noams.3mf` | V1 for printers without an AMS: 1 white tray + shelf (A) · 2 periwinkle cover (A) · 3 white tray + shelf (B) · 4 periwinkle cover (B) |
| `printers/<printer>/…` | the same four projects for each other printer |

The top-level files are for the **X1 Carbon** (also X1E and X1). `printers/` holds `p1s`, `p1p`, `p2s`, `a1`,
`a1m` (A1 mini), `h2d` and `h2s`. On the A1 mini's 180 mm bed the batch plate carries three cases, not five, and
each single case splits into a tray + shelf plate and a cover plate (a full case does not fit by object with the
tool-head clearance). The X1, P1S and P1P projects split the same way (since case v1.1: on their 256 mm plate the
cover's support ran off the plate edge). On the H2D each colour gets its own nozzle, so the projects need no flushing between them.

The projects are saved unsliced (small, and MakerWorld slices on upload); open one and press *Slice all*.
`stats.md` / `stats.json` are the slice results of every plate on every printer, and `preview/` holds the sliced
previews of the X1 Carbon plates, plus the A1 mini batch plates and one H2D case plate.

Checked: every plate of every project slices without an error or a warning (`stats.json`: return code 0, no
warning, no floating regions), and the saved projects re-open and slice again in the CLI.

## What is in each project

- **Filaments:** slot 1 = Polymaker Panchroma Matte **Cotton White**, slot 2 = Panchroma Matte **Pastel Periwinkle**.
  The preset is Bambu PLA Matte for that printer with Panchroma Matte's density (1.37 g/cm³, Polymaker TDS V2.1)
  and colour; every other value is Bambu's. Where the slicer has Polymaker's own Panchroma Matte preset, it can
  replace it.
- **Colours per region:** V1: tray, button, loop and shelf white; cover and LED skin periwinkle. V5: tray and shelf
  white; button, loop, cover and LED skin periwinkle. The LED skin (0.3 mm) is part of the cover.
- **Printer and process:** the printer's 0.4 mm nozzle preset and its 0.16 mm quality process
  ("0.16mm High Quality"; H2D: "0.16mm Balanced Quality"), with the printer's own start G-code (its `include`
  templates; up to case v1 the projects carried a generic one that printed at 205 °C without the plate routine).
  Changed from the preset (case v1.1, after the owner's first print): **Textured PEI plate** (65 °C), avoid crossing
  walls, Normal Lift (lift, then travel) instead of the sloped Auto Lift, 0.1 mm elephant-foot compensation. On a
  smooth or cool plate, pick that plate in the Prepare tab before slicing.
- **Orientation:** tray floor down; cover front up (the pillars stand on the plate); shelf flat, posts up.
- **Supports:** only inside painted zones (support enforcers): under the cover's inner face and under the loop.
  Grid supports, 4 mm apart, on the build plate only. A support blocker keeps them off the LED skin. The side
  button, latch slits, nut slots and USB opening get none.
- **Layout:** the batch plates are laid out by `build_bambu.py`: 8 mm from the plate edge, covers 14 mm apart (the
  cover's grid support reaches a few mm past the cover on its first layer). The full-case plates use the slicer's
  arrange, which keeps the parts apart as print-by-object needs.
- **Sequence:** the single-case plates print *by object*, so V1 needs two colour changes per plate (tray, cover, shelf). The V5 tray
  changes colour on every layer that holds the button or loop (Z 4.5–12.2 mm, about 49 changes); the batch plate
  prints *by layer* so five trays share the same changes and the purge per case drops to a fifth.

## No AMS

- **V1:** every part is one colour. Use `v1-noams.3mf`: print the white plate and the periwinkle plate; no
  colour change at all. (The full-case plates in `v1.3mf` also work: the printer pauses twice for a manual swap.)
- **V5:** the button and loop share their layers with the white tray, so a single pause cannot colour them; it
  needs about 49 swaps. Without an AMS, print V1 (the same case with a white button and loop) or print V5 on a
  printer with an AMS.

## Rebuild

```sh
cd hardware/raily-pin-public/case
python bambu/build_bambu.py profiles          # flatten the app's system presets into bambu/profiles/
python bambu/build_bambu.py parts             # print-oriented STLs per colour region -> bambu/src/
python bambu/build_bambu.py prepare           # jobs + bambu/run.sh (all printers; or name them: x1c a1m)
~/.local/bin/raily-heavy bash bambu/run.sh    # slice (stats, G-code in bambu/out/) + save the projects
python bambu/build_bambu.py fix               # fill the settings the unsliced export leaves empty (H2D nozzles)
python bambu/build_bambu.py stats             # stats.json, stats.md
python bambu/build_bambu.py verify            # every saved project: part copies and filament slots as planned
python bambu/build_bambu.py previews x1c a1m h2d  # preview/*.png from the G-code
python bambu/build_bambu.py walltest && bash bambu/run-walltest.sh   # A1 mini wall test plate (three trays)
```

`gcode_preview.py` draws the previews from the G-code (the CLI cannot export a preview image in the same run as a
slice): every extrusion in its filament colour, supports light grey, seen from the front left like Bambu Studio's
Preview tab.

## Licence

The case: CC BY-NC-SA 4.0, Raily (see `../README.md`).

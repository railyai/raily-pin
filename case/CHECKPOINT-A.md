# Checkpoint A: Raily Keyring for the Grove vibration module

Status: **FROZEN 2026-09-30, official.** The owner accepted each part as printed:

- tray 17.1 (P17a) and shelf 17.2b (P17c) on 2026-09-29;
- cover 18.2-G on 2026-09-30. The face holds on the round dowels and the seam is flush on all four sides, including the
  loop end («произведение искусства — ИДЕАЛЬНО»).

The geometry changes only with the owner's yes. Version B (the DA7280) builds from A with motor-specific changes only;
it stays pre-validation until the DA7280 arrives.

The print files (tray, shelf, cover, pusher), checksums and the assembly sheet are in
[`../published/keyring/case-3d/checkpoint-a/`](../published/keyring/case-3d/checkpoint-a/). The git tag
`keyring-checkpoint-a` is on the merge commit.

## Parts

| Part | Version | Source | Printed as |
| --- | --- | --- | --- |
| Tray | 17.1 (`v1.3`, proto 17) | `version_p('v1.3', proto=17)` | P17a-tray-shelf (its 17.2 shelf is superseded) |
| Shelf + motor seat | 17.2b | `shelf_17_2b()` | P17c-shelf-2b |
| Face + frame | 18.2-G (`v1.3-p18.2-G`) | `version_p('v1.3-p18.2-G')` | P18-2G-cover |
| Dowels | Ø3.0 × 5.0, 0.3 end chamfers, round (no D-flat) | `dowel_sprue(p)` with `dowel_flat=0` | on the 18.2-G plate: 9 + 1 test + 3 spare |
| Screws | 4 × M2×16 countersunk + 4 M2 nuts | closure A | stock |

## What 18.2-G changes against 18.1d

These are cover retention changes only. Tray, shelf, closure, USB end, side joints and the Grove hold-down (bar, boss,
legs) are 18.1d's.

- The dowels are round. A D-flat turned onto one of a face hole's 3 ribs takes that rib out, so the fit depended on how
  each dowel happened to be turned.
- Face rib crest Ø3.075, 0.08 over the printed Ø2.997 dowel. The fit coupon (P18-1e-fitcoupon-b) had 3.125 as its
  tightest step; the owner found it held perfectly with a loose dowel pushed fully in.
- Protrusion into the face is 2.4 mm or more everywhere. Real grip is the protrusion minus the 0.3 chamfer and the 0.2
  rib start:
  - 4 middle dowels, 2 locator dowels and the loop-centre dowel at (192.5, 5.6): frame hole 2.28, face hole 2.76,
    2.72 proud, grip 2.22.
  - Loop corners, moved to (191.27, 11.5) and (186.5, −9.0), where the face is 3.22 thick: frame hole 2.44, face hole 2.6,
    2.56 proud, grip 2.06, skin 0.62.
- There are no USB-corner dowels (owner decision). The screws clamp that end.
- Every frame hole ends on a solid ring of 1.28 mm or more (1.44 on the middle holes) around a Ø1.6 through neck.
  Shearing a Ø2.4 plug through 1.28 takes about 240 N; a palm on the pusher gives 50–150 N. The 18.1d floors were
  0.48 / 0.80 thick and bridged, good for about 35–60 N, and they punched through.
- One ТЕСТ joint (a frame block and a face tab, the exact middle joint) prints on the plate. The owner feels it before
  assembling. Instructions: P18-2G-sborka.png.

## Print settings

Bambu A1 mini, 0.4 nozzle, PLA.

| Plate | Filament | Settings | Estimate |
| --- | --- | --- | --- |
| Tray 17.1 | white (A3) | `a1m-fit` job, painted grid support under the loop | 1h05m, 19.8 g (P17a) |
| Shelf 17.2b | white (A3) | `shelf2b` job: plate on painted grid support, fence on a 5 mm brim | P17c |
| Cover 18.2-G | blue (A4), one filament, no prime tower | face at 0.08 layers (CROWN_FINISH + T08 shell, ironed top), frame and dowels at 0.16, by layer | 1h54m, 17.52 g, no support |

## Regenerate

```bash
cd hardware/raily-pin-public/case
bash bambu/checkpoint_a.sh     # builds and slices the three plates, then runs gcode_gate and fit_gate
```

## Gates (cover 18.2-G, commit 83698a029)

- `gcode_gate.py`: GATE PASS. All 5 objects (face, frame, sprue, test frame, test face): 0 islands; flagged patches
  are short bridges only.
- `fit_gate.py --ref P18-1c-cover-A1mini.gcode.3mf`: FIT PASS, exit 0, no declared holes. Dowels 13, Ø2.997, no flat.
  Face crests are 3.065–3.077 and grips 2.40–2.56 (18.1c: 3.226 / 2.00). Frame stops are 1.28–1.44, all solid.
- `check()`: 0 overlaps with the Grove module, shelf 17.2b, tray, boards and the M2 nut paths.

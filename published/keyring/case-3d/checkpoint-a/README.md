# Raily Keyring case: Checkpoint A (official)

**Version:** Checkpoint A, frozen 2026-09-30.
**For:** the DIY kit with the Seeed Grove vibration motor module.
**Status:** the owner accepted it on printed parts. The geometry changes only with the owner's approval.

These are the current print files for this case. Print only from this folder. The older projects in
[`../bambu/`](../bambu/), [`../print/`](../print/) and [`../v2/`](../v2/) predate this checkpoint: don't print from them.

## Files

| # | File | Part | Colour (A1 mini AMS slot) | Time, filament |
| --- | --- | --- | --- | --- |
| 1 | `A1-tray-17.1-A1mini.gcode.3mf` | tray 17.1 | white (A3) | 1h00m, 18.2 g |
| 2 | `A2-shelf-17.2b-A1mini.gcode.3mf` | shelf 17.2b with the Grove motor seat | white (A3) | about 20 min |
| 3 | `A3-cover-18.2-G-A1mini.gcode.3mf` | cover 18.2-G: face, frame, 13 round dowels (9 + 1 test + 3 spare), TEST joint | blue (A4), one filament | 1h54m, 17.5 g |
| 4 | `A4-pusher-A1mini.gcode.3mf` | dowel pusher: Ø12 × 40, one cup Ø3.4 × 1.5 | blue (A4) | 35 min, 2.7 g |
| — | `assembly-sheet-ru.png` | how to assemble the cover (Russian) | — | — |

All four are sliced for the Bambu Lab A1 mini with a 0.4 nozzle, PLA and a textured PEI plate. Supports are painted
into the files; keep auto supports off. Print all four once; the pusher is reused for every later cover.

Hardware: 4 × M2×16 countersunk screws (ISO 7046-1) and 4 × M2 nuts (ISO 4032).

## Assembly

[`assembly-sheet-ru.png`](assembly-sheet-ru.png) shows the cover assembly:

1. **Test first.** Press a dowel into the TEST block and fit the TEST tab onto it. If the tab is loose, stop.
2. **Dowels.** With the frame face up, push a dowel into each of holes 1–9 with the pusher until it stops. Each dowel
   stands about 2.5 mm proud.
3. **Face.** Line up locators 2 and 6, then press the face on until it sits on the frame with no gap.
4. **Close.** Check the loop end for a gap. Turn the cover face down and slide the 4 nuts into the pillar slots. Put
   the cover on the tray USB-C end first, press the loop end down, then drive the 4 screws from the back.

## Acceptance

| Part | Printed as | Accepted |
| --- | --- | --- |
| Tray 17.1 | P17a (2026-09-29) | fit with the boards and the battery |
| Shelf 17.2b | P17c (2026-09-29) | motor seats; the Grove leg holds it |
| Cover 18.2-G | P18-2G (2026-09-30) | face holds on the dowels; flush seam on all four sides, including the loop end. Owner: «произведение искусства — ИДЕАЛЬНО» |

Covers 18.1c and 18.1d failed and are not part of this checkpoint:

- 18.1c: the dowels were loose in the face.
- 18.1d: the dowels fell out of the face and punched through the frame floor.

18.2-G fixes both:

- the dowels are round, with no flat;
- the face rib crests are Ø3.075 against the Ø2.997 dowel;
- each dowel goes at least 2.4 mm into the face;
- every frame hole ends on a solid ring at least 1.28 mm thick instead of a bridged floor.

Details: [`case/CHECKPOINT-A.md`](../../../../case/CHECKPOINT-A.md).

## Checks on these exact files

| File | `gcode_gate.py` | `fit_gate.py` (against 18.1c) |
| --- | --- | --- |
| A1 | 2 one-cell specks (0.2 mm², z 7.76 and 8.04), no other islands | — |
| A2 | 1 one-cell speck (0.2 mm², z 1.76), no other islands | — |
| A3 | GATE PASS (0 islands on all 5 objects) | FIT PASS: 13 dowels Ø2.997, face crests 3.065–3.077, grip 2.40–2.56, frame stops 1.28–1.44 |
| A4 | GATE PASS (0 islands) | — |

The specks on A1 and A2 are single 0.2 mm² cells that the gate reports as islands. The printed P17a and P17c show no
defect there, so they don't block printing.

The pusher's cup (1.5) is shallower than every dowel's protrusion on 18.2-G (2.72 and 2.56). The dowel lands on its
solid stop while the pusher rim is still 1.06–1.22 mm above the frame.

`SHA256SUMS` lists each file's hash.

- A2 and A3 are byte-identical to the printed files.
- A1 is the P17a tray alone on the plate, without the superseded 17.2 shelf. The layers and path length match P17a; only
  the skirt differs.
- A4 is the pusher from the P18-1c plate, alone: the same 250 layers, every vertex within 0.17 mm.

## Regenerating from source

```bash
cd hardware/raily-pin-public/case
bash bambu/checkpoint_a.sh
```

This rebuilds and slices the tray, shelf and cover plates, then runs both gates.

- **Tray and shelf:** the regenerated toolpaths are identical to the printed P17a and P17c. A1 comes from the same tray job with the superseded 17.2 shelf removed
  from the plate. To rebuild it, run `build_bambu.py profiles` once, then
  `KC_VERSION=v1.3 build_bambu.py --proto-number 17 traya` and `bash run-traya.sh`.
- **Cover:** the geometry is identical, but the G-code differs. A3 was sliced in Bambu Studio with its Bambu PLA Matte
  profile, no elephant-foot compensation and a prime tower. A3, the file that was printed and accepted, is the
  reference.

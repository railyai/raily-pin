# DA7280 edition (2026-09-30)

Source: the 3D printing lead's change on 2026-09-30, relayed by the atlas /build session. The Grove Vibration Motor and its 20 cm cable are out. The new chain is Grove UART port (D6/D7) → Grove 5 cm (Seeed 110990036) → Grove-Qwiic Hub (Seeed 103020292, on the display with foam tape) → Qwiic 50 mm (SparkFun PRT-17260) → Qwiic Haptic Driver DA7280 (SparkFun ROB-17590). The board is the pre-soldered XIAO nRF52840 Sense (102010632).

| Page | Change | Check |
| --- | --- | --- |
| 2 box | Eight electronic parts as line-art tiles (from the /build session: Gemini 3 Pro Image, no text; the DA7280 follows SparkFun's CC BY-SA board file). The Expansion Board stays the CAD tile. Plus the printed case, the split ring, 4 × M2 × 16 countersunk screws and 4 nuts, and a foam-tape note | PASS: no vendor photos, so the guide licence needs no exclusion |
| 3 overview | CAD: board, XIAO Sense, battery on the JST, Grove 5 cm from UART, hub, Qwiic 50 mm, DA7280 | PASS: labels ≥ 9 pt |
| 4 board | The soldering page is removed; click-in step for the pre-soldered XIAO (`hdr-module`, `hdr-seat`) | PASS |
| 5 motor | `motor-chain.svg`: UART port ring and the chain callouts; the USB step shows the new chain | PASS |
| 7 button | Cable stub from the UART plug | PASS |
| info | Soldering section removed; the bus is not mentioned (if it ever is: «software I2C on D6/D7», never Wire1) | PASS |

**UART port.** Seeed lists 2 × I2C, 1 × UART and 1 × A0/D0. In the Seeed STEP, the side-button edge carries A0/D0 (x 143.5–153.5) and the shroud next to it (x 155.2–165.2); the other edge carries the two I2C. So UART is the one next to A0, matching the 3D printing stack.

**Pending.** The case pages: stack steps 5, 7, 8 and 10 from 3D printing's figures. The dowels, the face-to-frame fixing and the motor seat wait for the owner.

## Case pages, edition B (pre-validation, 2026-09-30)

- **Source:** 3D printing's `guide-art/B/`: tray 17.1, shelf 17.3c, cover 18.1e-B (frame + face, 9 round Ø3 × 5 dowels). Copied in by `cad/case_b_figs.py`.
  - Hole numbers 1–9 are vector marks, placed from the `step-1-holes.json` sidecar.
  - The locator dowels 2 and 6 on the face step come from `step-4-holes.json`.
  - Both match the owner sheet `P18-1e-B-sborka.png`.
  - The side-button callout on the closed view is projected from the case frame (x 163, z +27.3).
- **Order**, from stack.md and the owner sheet:
  - TEST check;
  - 11 dowels, 12 face on, 13 nuts (cover face down; 3D printing 2026-09-30: one flip with the nuts in and nothing presses on them; the slots open inward, so the nuts go in before closing);
  - 14 battery, 15 shelf and motor, 16 board;
  - 17 hub and cables, 18 cover on, 19 screws from the back;
  - Done;
  - 20 split ring.
- **Box tile and p7** use the B closed view. p7 shows the phone alone, next to it.
- **Steps 12 and 18** use 3D printing's `step-3` (frame upside down, a nut beside each pillar slot) and `step-9` (cover onto tray 17.1, USB-C end seated, loop end raised about 7°). Nothing is printed yet; the DA7280 arrives 1–3 Oct.
- **Print page** (3D printing, from the sliced .gcode.3mf, A1 mini, 0.4 nozzle, PLA; commit 81debabe1):
  - tray 17.1 17.35 g + 0.50 g support, about 1 h, 0.16, support under the loop only;
  - shelf 17.3c 1.75 g + 0.87 g support, 19 min, fence down with a 5 mm brim;
  - cover plate 16.91 g, 1 h 48 min: face 7.31 g (0.08, ironed), frame 6.77 g, dowel sprue 1.71 g, TEST pieces 0.52 + 0.60 g; no supports.
  - Per V1 case about 20.5 g Cotton White + 16.9 g Pastel Periwinkle. The guide rounds to 21 g and 17 g.
  - Case size 49.6 × 89.5 × 24.0 mm (97.8 with the loop), from `keyring_case.py` `outer_box`. The old 50.4 had no source.
  - The colour renders are `colour-v1-iso` / `colour-v5-iso` (tray 17.1 + cover 18.1e).
- **Not in the guide yet:**
  - V5 is not sliced for 17.1/B: no purge or waste figure. The old «five trays per plate, 16 g → under 5 g» was for the older geometry on a 256 mm bed and is removed.
  - The LED test piece bullet is removed (3D printing 2026-09-30): the B skin sits on the face's edge slope, built from 0.08 wall lines, 0.37–0.7 mm thick in the G-code; the flat 0.3 mm `keyring-led-coupon.stl` no longer predicts it. The LED check is the finished face with the LED on.
  - The pusher is an optional separate print, not on the B plate; no grams.
- **Owner review 2026-09-30** (clearer figures):
  - Steps 14–16 are close-ups into the open tray (3D printing's step-6a/6b/6c, view (0.2, 1, 0.35), loop end up): cell, shelf over it, DA7280 on the shelf. The J1 socket that takes the Qwiic cable is ringed.
  - Step 11 carries a hole-9 inset (half-sections step-1-d1/d2): the dowel above its hole, then seated, «2.7 mm» from `dowel_top` to `frame_surface`.
  - Step 12 has an arrow and click marks for the face; step 13 has an arrow at each nut toward its pillar slot (nut centres read off step-3.png).
  - The «Side button» callout points at the nub over D1 (`closed-points.json` `button_nub`, x ≈ 168.8), not the tongue centre. 3D printing checked: the tongue in the renders matches the printed P17a tray.
  - The steps are renumbered 14–21: board 17, hub 18, cover on 19, screws 20, split ring 21.
  - Shelf 17.3c: the owner ruled no words on it, only a centred up arrow toward the loop; the version mark moved to the underside (3D printing 3ef7e54a0). No render in the guide may carry Russian text or internal part codes.
- **Dowel and face joint proven** (3D printing, 2026-09-30, checkpoint A frozen, commit b91d38d77): the owner assembled the 18.2-G cover and called it perfect. B uses the same joint (crest 3.075, round dowels, neck stops), so steps 11–12 hold on real hardware. The DA7280 shelf and cover fit still waits for the motor.
- **Re-check after the 17.3c + DA7280 fit:** step 15 (the motor on its seat). Compare the figure to the real part and fix it if it differs.
- **Dowel height** (print-log): 2.72 mm middle/locators/centre, 2.56 mm at the loop corners. The guide says «about 2.7 mm». 13 dowels are printed: 9, TEST and 3 spare.

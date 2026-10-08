# Raily Keyring case v2: 3D files

Case v2 is the same board stack as v1 with a window over the Expansion Board's 0.96" OLED and a thinner shell.
Source, numbers and decisions: [`../../../../case/README.md`](../../../../case/README.md#case-v2-oled-window).
Since case v1.1 (2026-09-28) v2 shares v1.1's board stack (the real XIAO and switch heights, the raised floor), the
flat USB-C end with its window, the taller switch slot and the side button; its shell is 1.6 / 1.2 / 0.8.

| | v1.1 (one folder up) | v2 | v2flat (option) |
| --- | --- | --- | --- |
| Width × length | 50.4 × 89.5 mm | 50.4 × 89.5 mm | 50.4 × 89.5 mm |
| Thickness at the rim / over the facets | 20.3 / 24.0 mm | 19.9 / 23.6 mm | 19.9 mm, flat |
| Length with the loop | 97.8 mm | 97.8 mm | 97.8 mm |
| Walls / floor / front plate | 1.6 / 1.4 / 1.0 | 1.6 / 1.2 / 0.8 | 1.6 / 1.2 / 0.8 |
| USB-C receptacle mouth under the outer face | 0.2 mm | 0.2 mm | 0.2 mm |
| OLED | covered | window | window |

**Window:** aperture 24.3 × 13.3 mm over the OLED glass (glass minus 0.2 mm per side), at the floor of a well that
reaches 0.5 mm above the glass. The well opens into the front at 31.4 × 21.5 mm. The whole screen stays in view up
to about ±9° across and 5–11° along the case. From inside there is a recess for an optional 0.5 mm clear sheet,
26.1 × 15.1 mm with r 1.7 corners (`print/keyring-window-insert.stl` is that sheet as a part or a cutting
template). The OLED needs firmware that draws on it, rotated 90°.

- `bambu/`: Bambu Studio projects of **case v2**, the same set as v1 (`v1` and `v5` in the file names are the
  colourways, not the case version): `v1.3mf`, `v5.3mf` (plates: LED test piece, case A with
  screws, case B snap-fit), `v5-batch.3mf`, `v1-noams.3mf`; top level for the X1C / X1E / X1, `printers/<model>/`
  for P1S, P1P, P2S, A1, A1 mini, H2D and H2S. Slot 1 = Cotton White (body), slot 2 = Pastel Periwinkle (front).
  On the A1 mini (180 mm bed) a case does not fit on one plate printed by object, so `v1.3mf` and `v5.3mf` have five
  plates: LED test piece, then case A and case B each as a tray + shelf plate and a cover plate; its batch plate
  holds three cases, not five. For v2 the X1 / X1E / X1C, P1S and P1P projects are split the same way: v2's thin
  front plate makes the cover's support reach about 6 mm past the cover, and a full case arranged on their 256 mm
  plate put it off the plate.
  `stats.md`: time, grams and purge per plate; `preview/`: sliced previews (X1C and A1 mini).
- `print/`: v2 STL per part and colour, `keyring-case.step`, `closure-b/`, `keyring-led-coupon.stl`,
  `keyring-window-insert.stl`, `report.json` (sizes, window, interference check).
- `print-flat/`: the same for v2flat (no facets).
- `renders/`: `render-v2-{v1,v5}-{front,iso}.png`, `render-v2flat-v1-{front,iso}.png`.
- `line-art/`: `case-v2-{front,back,side,iso}.png`, `case-v2-window-{across,along}.png` (sections through the
  window; the board is drawn as boxes), `tab-v2-section.png`, `tab-v2-across.png` (the side-button tongue),
  `usb-v2-usb-{end,section}.png` (the USB-C end).

Filament: Polymaker Panchroma Matte PLA, Cotton White (CA04016) and Pastel Periwinkle (CA04036).
Hardware for closure A: 4 × M2×16 countersunk screws and 4 × M2 nuts. Supports are pre-painted; keep auto supports
off. Print the LED coupon first.

Licence: CC BY-NC-SA 4.0, Raily; images that show the boards are CC BY-SA 4.0 (Seeed Studio models).

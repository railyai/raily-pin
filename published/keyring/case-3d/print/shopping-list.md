# Raily Keyring case: filament shopping list

Two colourways ship: **V1** and **V5**. Both use two spools of Polymaker Panchroma Matte PLA, 1.75 mm:

| Spool | Product name | SKU | Hex | TD |
| --- | --- | --- | --- | --- |
| White | Polymaker Panchroma™ Matte PLA — Matte Cotton White, 1.75 mm, 1 kg | CA04016 | `#F4EFEB` | 4.1 mm |
| Periwinkle | Polymaker Panchroma™ Matte PLA — Matte Pastel Periwinkle, 1.75 mm, 1 kg | CA04036 | `#ADB4E6` | 1.5 mm |

Hex and TD are from Polymaker's «HEX Codes and Transmission Distances» table. The density, 1.37 g/cm³, is from the Panchroma TDS V2.1 (Matte).

## Which piece takes which spool

| Piece (STL in this folder) | V1 | V5 |
| --- | --- | --- |
| `keyring-tray-tray.stl` | White | White |
| `keyring-tray-button.stl` (side button, flush) | White | Periwinkle |
| `keyring-tray-loop.stl` | White | Periwinkle |
| `keyring-shelf-shelf.stl` (inside) | White | White |
| `keyring-cover-cover.stl` (front) | Periwinkle | Periwinkle |
| `keyring-cover-led.stl` (0.3 mm skin over the LED) | Periwinkle | Periwinkle |

The LED skin is the front's own spool in both colourways, so there is no white dot.

## Grams per case

Case v1.1 (1.6 mm walls, 1.4 mm floor). From the slices (`../bambu/stats.md`, X1 Carbon, 0.16 mm, closure A; the
X1, P1S, P1P and A1 mini print a case as a tray + shelf plate and a cover plate): model + support per colour, plus
the purge. The cover's support (about 11 g of periwinkle) is part of every case; the white support under the loop
is 0.4 g. Other printers are within about 5 %.

| Per case | White | Periwinkle | Purge | Total |
| --- | --- | --- | --- | --- |
| V1 (tray plate + cover plate) | 19.8 g | 25.3 g | 0 g | 46.0 g |
| V5 (tray plate + cover plate) | 20.6 g | 27.2 g | 15.5 g | 64.3 g |
| V5 (five-case batch plates) | 19.2 g | 25.8 g | 4.5 g | 49.7 g |
| LED test coupon (print once per spool batch) | — | 1.5 g | — | 2.0 g |

Solid volume × 1.37 g/cm³ (`filaments.json`, no support, 100 % infill) is 20.3 + 19.1 g (V1) and 19.8 + 19.7 g (V5).
Case v1 (2.0 mm walls) took 22.8 + 25.6 g for V1 (49.2 g with the purge).

### Purge (AMS colour changes)

- **V1:** every part is one colour and each plate holds one colour: no purge. (On printers that print a full case
  on one plate, P2S, A1, H2S, the slicer changes colour twice: about 0.6 g.)
- **V5:** the tray carries periwinkle in the button and loop layers: 52 colour changes and 15.5 g of purge for one
  case. The purge is per plate, not per part: five trays on one plate share them, 22.3 g, 4.5 g per case.
- **H2D:** the two nozzles hold one colour each, so there is no flush between them.
- **P2S, H2S:** these flush in firmware; `stats.md` counts it from the slicer's filament totals.

### Spools needed

| Batch | V1 | V5 (five-case plates) |
| --- | --- | --- |
| Cases per 1 kg White | ~50 | ~50 |
| Cases per 1 kg Periwinkle | ~39 | ~38 |
| For 20 cases | 1 × White, 1 × Periwinkle | 1 × White, 1 × Periwinkle |
| For 100 cases | 2 × White, 3 × Periwinkle | 2 × White, 3 × Periwinkle |

## Hardware per case

- **Closure A (default):** 4 × ISO 7046-1 M2 × 16 countersunk screws, cross recess H0; 4 × ISO 4032 M2 nuts.
- **Closure B (snap-fit):** none.

## Bambu AMS (4 slots)

| Slot | Spool |
| --- | --- |
| 1 | Matte Cotton White |
| 2 | Matte Pastel Periwinkle |
| 3 | Matte Cotton White (backup: the AMS switches to it when slot 1 runs out) |
| 4 | Matte Pastel Periwinkle (backup for slot 2) |

- **Slicer:** the projects in `../bambu/` already assign every region to slot 1 or 2.
- **V1 without an AMS:** `../bambu/v1-noams.3mf`, a white plate and a periwinkle plate, no colour change.
- **V5 without an AMS:** not practical: the button and loop share their layers with the white tray (52 changes).
  Print V1 instead, or V5 on a printer with an AMS.

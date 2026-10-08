# MakerWorld listing: draft only

Nothing here has been uploaded or published. The owner publishes under the owner's account.

## Title

Raily Keyring: faceted case for a DIY Bluetooth keyring (Seeed XIAO nRF52840)

## Cover images

In this order, from `../preview/`:

1. `render-v1-iso.png`: V1, Cotton White with a Pastel Periwinkle front
2. `render-v5-iso.png`: V5, the periwinkle front with a periwinkle loop and button
3. `render-v1-front.png`, `render-v5-front.png`: the faceted fronts
4. `sheet-final.png`: both colourways side by side
5. Sliced previews: `preview/v1-plate2.png` (V1 case, two colour changes), `preview/v5-batch-plate1.png` (five V5 trays)

No text in any image; MakerWorld's own captions carry the words.

## Description

A pocket-size case for the Raily Keyring, a DIY Bluetooth keyring built on the Seeed Studio XIAO nRF52840 and
its Expansion Board, with a Grove vibration motor and a 602030 LiPo. The front is a solid faceted panel with a
low X relief; the RGB LED glows through a 0.3 mm skin of the front colour. A print-in-place flexure on the side
presses the board's button, and the power switch sits behind a slot.

Two colourways, both in Polymaker Panchroma Matte PLA:

- **V1:** Cotton White body, loop and button; Pastel Periwinkle front.
- **V5:** Cotton White body; Pastel Periwinkle front, loop and button.

Two closures, both keep the front clean:

- **A, screws:** four M2 × 16 countersunk screws from the back into nuts in the cover.
- **B, snap-fit:** no screws; four latches click in, and a coin in the notch beside USB-C opens it.

The cell is on a JST plug and the case opens with a screwdriver (A) or no tool at all (B), so the battery can be
replaced.

What you need besides the print: XIAO nRF52840 + Seeed Expansion Board Base for XIAO, Grove vibration motor and
the kit's 20 cm Grove cable, a 602030 LiPo with a JST-PH lead, and for closure A 4 × M2 × 16 countersunk screws
(ISO 7046, cross recess) and 4 × M2 nuts (ISO 4032).

## Print profiles

One print profile per printer: X1 Carbon / X1E / X1, P1S, P1P, P2S, A1, A1 mini, H2D, H2S (0.4 mm nozzle,
0.16 mm layers). Each carries these plates:

| Profile file | Plates |
| --- | --- |
| V1 | LED test coupon · V1 case A · V1 case B |
| V5 | LED test coupon · V5 case A · V5 case B |
| V5 five-case batch | five trays + shelves · five covers (three on the A1 mini) |
| V1 without AMS | white parts and periwinkle parts on separate plates, for A and B |

On the A1 mini, X1, P1S and P1P each case splits into a tray plate and a cover plate (a full case does not fit by
object on the A1 mini's 180 mm bed; on the 256 mm plates the cover's support would reach the plate edge).

Filament slots: **1 = Cotton White, 2 = Pastel Periwinkle.**

Slicer estimates per printer (0.16 mm; filament includes the cover support, about 11 g; A1 mini, X1, P1S, P1P: the case is two plates; every plate of every profile is in `stats.md`):

| Printer | V1 case A: time, colour changes, filament | V5 case A: time, colour changes, purge | V5 batch: cases, time for both plates, purge per case |
| --- | --- | --- | --- |
| X1 Carbon / X1E / X1 | 2 h 17 m, 0, 46 g | 3 h 36 m, 52, 15.5 g | 5, 11 h 56 m, 4.5 g |
| P1S | 2 h 13 m, 0, 46 g | 3 h 32 m, 52, 15.5 g | 5, 11 h 52 m, 4.5 g |
| P1P | 2 h 15 m, 0, 46 g | 3 h 34 m, 52, 15.5 g | 5, 11 h 52 m, 4.5 g |
| P2S | 2 h 10 m, 2, 47 g | 3 h 19 m, 54, 19.2 g | 5, 11 h 45 m, 4.4 g |
| A1 | 2 h 12 m, 2, 46 g | 3 h 24 m, 54, 11.5 g | 5, 12 h 03 m, 4.0 g |
| A1 mini | 2 h 19 m, 0, 46 g | 3 h 41 m, 52, 11.1 g | 3, 8 h 16 m, 6.7 g |
| H2D | 2 h 09 m, 0, 45 g | 2 h 17 m, 0, 0.0 g | 5, 10 h 52 m, 0.8 g |
| H2S | 2 h 09 m, 2, 46 g | 3 h 17 m, 54, 21.6 g | 5, 11 h 41 m, 4.6 g |

## Print notes

- **Print the LED coupon first** (plate 1, 1.5 g). It has a 0.3 mm and a 0.4 mm skin (one and two notches).
  Hold it over a lit LED: the case uses the 0.3 mm skin (one notch), which shows the light clearly better than
  0.4 mm.
- **Orientation is set:** tray floor down, cover front up (its pillars stand on the plate), shelf flat.
- **Supports are painted in:** only under the cover's inner face and under the keyring loop. Remove the cover's
  grid support from underneath; nothing touches the front or the LED skin. Do not switch on automatic supports:
  they would reach into the side-button slit.
- **V1** needs two colour swaps per full-case plate (white tray, periwinkle cover, white shelf; the parts are one colour each). **Without an AMS**, use the
  *V1 without AMS* profile: a white plate and a periwinkle plate, no change at all.
- **V5** colours the loop and button inside the white tray, so the tray changes colour on about 52 layers. That
  needs an AMS. The five-case batch plate shares those changes between five trays and cuts the purge per case to a
  fifth. Without an AMS, print V1.
- **First-print checks** (please report back; these are the numbers we could not measure yet):
  1. **LED coupon:** the glow through 0.3 mm (the case skin) with your spool.
  2. **Motor height:** the motor on its shelf posts must sit under the cover with a gap; the model assumes the
     motor module is 9.8 mm tall.
  3. **Plug room:** the USB-C cable plugs in fully through the bottom window (it fits the plug's metal shell; the receptacle sits 0.2 mm under the face); the JST and both Grove plugs fit with
     the cable coiled over the A0/D0 row.
  4. **Button and switch travel:** the side button clicks D1 with a light press and springs back; the power
     switch moves both ways through its slot with a pen tip.
  5. **Closure B:** the four latches click in and the cover comes off with a coin in the notch; after 20
     open/close cycles it still holds; a 1.5 m drop onto a hard floor does not open it.

## Licence

The case: **CC BY-NC-SA 4.0** (Attribution-NonCommercial-ShareAlike), Raily. The assembly guide's drawings adapt
Seeed Studio's board models (CC BY-SA 4.0).

## Tags

keyring, keychain, bluetooth, ble, xiao, seeed studio, nrf52840, grove, diy electronics, case, enclosure,
multicolor, ams, pla matte, panchroma, raily

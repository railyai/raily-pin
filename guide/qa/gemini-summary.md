# Gemini illustration QA: Raily Keyring prototype, 2026-09-27

Model: gemini-3-pro-image-preview via gem.py. Refs: the atlas renders (on white), ref-seated.png (Blueprint 02 photo) and, for rounds 2–3, my exact iso-geometry drawing plus the owner's style sample.
Checklist per image:
- every component present, in the reference positions;
- XIAO: 7 pads per long edge, none on the short edges, USB-C on a short edge, overhanging away from the OLED;
- sockets: 2 rows × 7, about 15 mm apart;
- Grove plugs 4-pin; JST 2-pin;
- case portrait, loop centred on the top edge, USB-C at the bottom;
- no text, logos or invented parts.

## Picked (PASS)
| Image | Result | Notes |
|---|---|---|
| cover-2 | PASS | Portrait case; faceted front; button cap in the centre hole; loop centred on top; split ring through it. No text. (The bottom USB-C face is not visible from this angle.) |
| part-xiao-3 | PASS | 7 pads on each long edge, none on the short edges, USB-C on a short edge, shield, no text. |
| part-headers-3 | PASS | Two strips of exactly 7 pins, long and short legs. |
| part-motor-1 | PASS | Board, coin motor, one 4-pin Grove socket, two mounting holes. |
| part-cable-3 | PASS | 4 wires, a 4-pin plug on each end. |
| part-battery-1 | PASS | Pouch, two leads, 2-pin JST plug. |
| part-front-2 | PASS | Portrait, facets, centred hole. |
| part-rear-2 | PASS | Portrait tray, loop centred on the top edge, USB-C slot centred at the bottom, 4 posts. |
| part-cap-1 | PASS | Round cap with rim. |
| part-ring-1 | PASS | Double-coil split ring. |
| part-usb-3 | PASS | USB-A to USB-C, no logo. |

## Best available, NOT passing (flaws named)
| Image | Result | Flaws |
|---|---|---|
| seat-3 (step 3, seat) | FAIL (minor) | Board matches the photo (2 Grove top, 2 bottom, round buzzer by reset, switch by the XIAO, OLED, 2×5 header, JST). XIAO 7/7 pads, USB-C overhangs away from the OLED. **Flaw:** the XIAO is drawn seated, not lifted, and the front socket row shows open holes in front of the header spacers, as if the spacers sat beside the row rather than in it. |
| solder2-2 (step 2, solder) | FAIL (minor) | The jig is right: headers in the sockets, XIAO on top, 7 pins in the near row, iron on one pad, hands in line style. **Flaws:** a visible gap with bare pins between the spacers and the sockets (should be flush); the USB-C faces the board interior, not the outer edge. |
| part-expansion-1/2/3 | FAIL | Every candidate draws three socket rows (a 2×7 block plus one row) or 3–5 Grove ports. None matches the photo. |

## Rejected (summary)
- seat-1/2, seat2-1/2/3, seat3-1/3: XIAO offset from the sockets, USB-C toward the wrong edge, or three socket rows.
- seat3-2: 9 pads and 10 sockets per row.
- solder-1/2/3, solder2-1/3, solder3-1/2/3: headers not in the sockets, or 9–16 pins per strip.
- cover-1: acceptable (black rear), but cover-2 is cleaner. cover-3: ring angle unclear.
- part-*-1 cable/usb: an Expansion Board copied in from the style sample. part-front-1/rear-1: landscape. part-headers-1/2: 8–10 pins. part-usb-2: logo on the plug.

## Conclusion
Single parts pass after 1–2 rounds. Board-level scenes with counted features (the 2×7 sockets, the XIAO pads in context) failed in 3 rounds of 3, even with an exact geometry drawing as image 1.

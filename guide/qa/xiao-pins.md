# XIAO pin pages (pages 4 and 5): QA

Owner decisions, 2026-09-28:
- The header strips go in **short end down**: the ~3 mm short end goes down, the black 2.54 mm spacer sits flat, the XIAO sits on the ~6 mm long end. Long ends down is wrong: the legs bottom out in the sockets, the XIAO rides up to ~6 mm high, the case does not close and USB-C misses its opening (owner photos of the wrong and the right result).
- The pins are soldered **off the board**. A breadboard (optional, it only keeps the pins straight) or the table holds the headers. Never the Expansion Board as a jig (its spring pins push the XIAO up), never a finger on the XIAO.

Pipeline: `cad/scenes6.py` (same renderer, board camera (-1, 1.1, 1) and fill style as `scenes2.py` / `scenes3.py`). Breadboard and flush cutters are parametric in `cad/parts.py`. `render.headers()` now takes `below` / `above` (mm under and over the spacer); the default, 3 / 3, is the finished part: short end down, tails 1.8 mm over the XIAO. The header spacers and the board's inner sockets get a darker fill (the black plastic).

## Page 4, «Solder the XIAO pins»
- **1 hdr-strip: PASS.** Both strips over a breadboard, short ends down, blue arrow down. Dimensions on the near strip: «long end 6 mm», «short end 3 mm»; the gap between them is the spacer.
- **2 hdr-xiao: PASS.** Strips standing in the breadboard, long ends up; the XIAO above them, chip side up, blue arrow down.
- **3 hdr-solder: PASS.** XIAO on the spacers, long tails through the pads, iron on the corner pad by the USB-C, blue ring. No finger, no board.
- **4 hdr-clip: PASS.** 13 tails clipped to 1.5 mm, the last one in the jaws of the flush cutters; blue dimension «≤ 2 mm» on a leader.
- Page note: «Solder off the board. Keep your fingers off the XIAO: the pins get hot.»

## Page 5, «Onto the board»
- **5 hdr-module: PASS.** One piece: XIAO, spacers, short ends below, clipped tails on top.
- **6 hdr-seat: PASS.** The piece over the two inner socket rows (dark), blue arrow down; USB-C at the board edge away from the OLED.
- **Wrong / right: PASS.** Front section through the near inner socket row, same scale side by side.
  - Wrong (red ✗): long ends down, bottomed out about 3 mm deep; bare legs between the socket and the spacer (red ring); the XIAO about 3 mm higher.
  - Right (blue ✓): short ends in the sockets, spacer flat on the sockets, XIAO on the spacer, tails ≤ 2 mm.
- Page note: «Before the cover goes on, the spring pins under the USB-C end lift the XIAO slightly. That is normal.»

## Consistency
- The earlier page «Solder and seat» (on-board jig, `solder-vec.svg`, `seat-vec.svg`, `inset.jpg`) is replaced by pages 4 and 5. Its figures showed the old orientation: `seat-vec` with the 6 mm ends under the spacer, `solder-vec` with the 3 mm end over the XIAO. The files stay in `art/` but the guide no longer uses them.
- The «Soldering» section on page 15 says the same: off the board, breadboard or table, short ends down, fingers off, clip to 2 mm or less, not long ends down.
- The other board scenes (`scenes3.py`, `scenes5.py`) show the finished stack; with the new `headers()` default their visible lines are unchanged (the short end is hidden in the sockets), so they were not re-rendered.
- Steps renumbered: pages 4 and 5 are steps 1–6, the rest follow from 7. Cross-references: motor shelf page 12, battery page 9.
- `render.mjs`: 16 pages, all checks pass (overflow, footer, label size, images, fonts).

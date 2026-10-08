# Vector board scenes (boardart.py), QA

- **Source:** scratchpad/apple/boardart.py + build3.py. All counts come from code.
- **Style:** stroke #3c3c40 sampled from the Gemini tiles (median dark ≈ #686a6c on antialiased lines, core darker), width 0.24 % of the figure width. Fills #f4f4f6 / #e4e4e8, rounded corners.
- **Side-by-side check:** proto2/cmp.png (Gemini tile left, vector right).

## board-tile.svg (box page): PASS with notes
- 2×7 socket strips (7 holes each), empty XIAO area.
- OLED with glass, bezel and flex tab.
- 4 Grove shrouds with latch notches: 2 along the top edge, 2 along the bottom.
- Buzzer: a cylinder with a centre hole.
- Slide switch, 2 tact buttons (reset, user), JST-PH 2-pin, 2×5 header, 3 mounting holes.
- Note: the Grove shrouds are simpler than Gemini's (no inner pin detail).

## solder-vec.svg: PASS
- Close-up of the socket corner seen from the USB side.
- Sockets, header spacer and XIAO are flush, with no gaps.
- 7 pads on the visible edge; each header pin shows through its pad.
- USB-C overhangs past the board edge, toward the viewer.
- Iron tip on the corner pad, with a blue ring on it.
- Inset circle (Gemini inset-1): pin through the plated hole, solder cone.

## seat-vec.svg: PASS with a note
- XIAO with 7 + 7 headers lifted 15 mm above the empty 2×7 sockets; blue arrow down.
- USB-C overhangs the board edge on the side away from the OLED.
- Note: at this camera angle the USB-C end sits at the back-left, so the overhang is only partly visible.

## Style jump
Vector lines are crisper and slightly lighter than the Gemini tiles. They read as the same family at page size; there is no hatching or shading on either side.

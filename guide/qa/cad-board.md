# CAD line-art board scenes: QA

## Sources
Seeed Studio official STEP models on Printables, author «Seeed Studio», licence **CC BY-SA 4.0**:
- 1336695 «Seeed Studio Expansion Board Base for XIAO with Grove OLED.step»
- 1336692 «XIAO-nRF52840 v3.step»

Headers (1×7, 2.54 mm pitch, 0.64 mm pins, 2.54 mm spacer) and the soldering iron are parametric in cadquery.

Pipeline: `scratchpad/cad/render.py` + `scenes.py`.
- cadquery 2.8 loads the models; OpenCASCADE HLRBRep does hidden-line removal; visible sharp and outline edges go to SVG polylines.
- One camera for all board scenes: view (-1, 1.1, 1).
- Silkscreen removed: the zero-thickness solids are dropped, and the XIAO's raised text (394 faces) is defeatured into `xiao-pcb-clean.step`.
- Blue ring and arrow: vector overlay, projected with the same camera.

## Geometry, from the CAD, not drawn
- **Sockets:** the board has FOUR 1×7 female strips in two double-row blocks. The XIAO uses the INNER pair, whose centres are 15.25 mm apart; the outer pair is a breakout. This is why «14 legs in 14 sockets, no free holes» matters. Earlier «2×7» notes were a simplification.
- **XIAO:** 21 × 17.8 mm, 7 castellated pads per long side (a pad pitch of 2.54 mm was measured in the STEP), USB-C overhangs 1.5 mm past the board edge at x = 119.5 (bbox 117.96 vs 119.50), on the edge away from the OLED.
- **Headers:** 7 + 7 by construction, centred on the inner sockets.

## Scenes
- **board-tile.svg: PASS.** Both socket blocks empty, OLED, 4 Grove, buzzer, switch, buttons, JST, 2×5 header, holes; no text.
- **solder-vec.svg: PASS.**
  - Close-up of the seated stack with the spacer flush on the socket and the XIAO flush on the spacer.
  - Pin tips show through the pads.
  - USB-C at the front, overhanging the edge.
  - Iron tip on the corner pad next to the USB-C, with a blue ring and the Gemini inset (pin + solder cone).
- **seat-vec.svg: PASS.** XIAO + headers lifted 15 mm above the inner sockets with a blue arrow down; the USB-C overhang is visible at the front-left.

## Style
CAD edges drawn in #3c3c40 at 0.24 % of the figure width (0.35 % for the close-up), no fills. This is pure technical line art, a bit crisper than the Gemini tiles. Face fills could be added from the CAD if you want a closer match.

## Licence note
The CAD-derived drawings are adaptations of CC BY-SA 4.0 models, so they must stay **CC BY-SA** with attribution to Seeed Studio. The guide itself is CC BY-NC-SA; BY-SA content can sit in it as a separately licensed figure with a credit line, but it cannot be relicensed NC. Proposed credit: «Board drawings derived from Seeed Studio 3D models, CC BY-SA 4.0».

## Round 2 (lead QA on proto4)
- **Hierarchy:** one HLR over the whole scene, with per-group output (HLRBRep Select).
  - Object groups in #3c3c40: the XIAO, the headers, the iron, the inner sockets on seat.
  - Context in #9a9aa0: the rest of the board, and the sockets on solder.
  - Silhouettes at full width, internal edges at 60 %.
- **Fills:** triangles tessellated from the CAD and depth-sorted; top-facing faces white, sides #f2f2f4 / #e8e8ec. Rendered as a PNG layer under the vector lines, in the same projection.
- **Solder:** frame widened to the whole XIAO, both header rows, the full iron (a shorter handle) and board context; ring and inset kept.
- **Seat:** the inner two 1×7 socket rows are dark, the outer breakout strips grey. Caption: «Seat the XIAO in the inner rows.»
- **Tile:** same fills.
- **Credit** «Board drawings derived from Seeed Studio 3D models (CC BY-SA 4.0)» goes on the Important information page and the README when the full guide is built.

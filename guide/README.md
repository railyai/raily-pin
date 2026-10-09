# Raily Keyring assembly guide

- **PDF (A4):** [`raily-keyring-assembly-guide.pdf`](raily-keyring-assembly-guide.pdf).
- **Page previews:** [`pages/`](pages/).
- **Source:** [`assembly-guide.html`](assembly-guide.html).

**Status: v0.5, Apple quick-start style, 16 pages. The PDF is publishable as is: it carries no internal open items.**
- QA per figure in `qa/`.
- Open items before launch: `docs/pins/diy-launch.md`, «Open before launch».

## Figures
- **Board drawings** (`art/*-vec.svg`, `board-tile.svg`, `grove-qwiic-cable.svg`, `motor-chain.svg`, `usb-nobatt.svg`, `button-d1.svg`, `battery-jst.svg`):
  - hidden-line CAD line art from Seeed Studio's official 3D models: Expansion Board, printables.com/model/1336695; XIAO nRF52840, printables.com/model/1336692;
  - rendered by `cad/` (cadquery + OpenCASCADE HLR, with the face fills as a PNG layer);
  - headers, the DA7280 haptic board, the Grove-to-Qwiic cable (Adafruit 4528, 100 mm) and its plugs are parametric in `cad/parts.py` (DA7280 layout from SparkFun's board drawing; the motor cable runs from the Grove UART port, the shroud next to A0/D0, straight into the DA7280: no hub since 2026-10-09).
  - **Board drawings derived from Seeed Studio 3D models (CC BY-SA 4.0).** They stay under CC BY-SA 4.0, and the STEP files in `cad/` are Seeed's under the same licence.
- **XIAO onto the board** (`art/hdr-module.svg`, `art/hdr-seat.svg`, page 4): the same CAD line art, made by `cad/scenes6.py`. The kit uses the pre-soldered XIAO nRF52840 Sense (2026-09-30), so the soldering pages (`hdr-strip`, `hdr-xiao`, `hdr-solder`, `hdr-clip`, `hdr-wrong`, `hdr-right`) are no longer used by the guide.
- **Bench and phone scenes** (`overview.svg`, `check-meter.svg`, `check-plug.svg`, `charge-pouch.svg`, `app-phone.svg`): the same CAD line art. The multimeter, the soft LiPo pouch and the phone are simple parametric models or vector outlines in `cad/scenes5.py`. The keyring next to the phone is the case model itself (`keyring_case.py`).
- **Case pages** (`art/kc-*`): the locked case v1 (V1 and V5), from `hardware/raily-pin-public/case/guide-figures`. They are made by that folder's `guide_figures.py` from `keyring_case.py` with this renderer. `cad/case_figs.py` copies them in and adds vector callouts (side button, LED) to the closed view. The earlier illustrative case draft (`cad/case.py`, `cad/scenes4.py`, `art/case-*.svg`) was removed; it is in the git history before this change.
- **Part tiles, the flashing figure and the solder inset** (`art/*.jpg`): Gemini 3 Pro Image line art traced from the atlas part renders, checked per image in `qa/`.
- **Blue marks** (rings, arrows, + / − labels): vector overlays projected with the same camera.

## Render
From the repository root:
```bash
(cd hardware/raily-pin-public/guide/art && python3 ../cad/build_full.py ../assembly-guide.html)
npm i --no-save --prefix hardware/raily-pin-public/guide playwright && (cd hardware/raily-pin-public/guide && npx playwright install chromium)
node hardware/raily-pin-public/guide/render.mjs
```
`build_full.py` reads the figures from the working directory (`art/`) and the page template from `cad/proto.html`. `render.mjs` writes the PDF and `pages/page-NN.png`, and it fails on:
- page overflow;
- content within 6 mm of the footer;
- a numbered page without its footer number;
- a figure label under 9 pt or clipped;
- a missing image, including the rasters nested in SVG figures;
- an Inter face (400 or 700) that did not load.

Board and bench scenes: `cd hardware/raily-pin-public/guide/cad && python scenes2.py ../art tile seat && python scenes3.py ../art motor usb button jst overview cable && python scenes5.py ../art meter charge phone && python scenes6.py ../art` (Python 3.11, `pip install cadquery`). The phone scene loads the case model from `KEYRING_CASE` (default `../../case`).

Case pages: `python case_figs.py ../../case/guide-figures ../art` copies the case figures in and adds the callouts. Edition B (DA7280, cover 18.2-B, no part codes): `python case_b2_figs.py <figures from case/guide_render_b2.py> ../art` writes `art/kcb-*` (it replaces `case_b_figs.py`, the 18.1e-B figures). Gemini provenance: `gem.py`, `gemini-jobs.py`, `gemini-full.py` (they need the reference renders in `cad/refs/`, not committed).

## Licence
- Guide: [CC BY-NC-SA 4.0](LICENSE).
- Board drawings: CC BY-SA 4.0, derived from Seeed Studio models.
- © 2026 Raily LLC. «Raily» and the Raily logo are trademarks of Raily LLC.

# Side button, case v1 (lead decision 2026-09-27)

The owner chose a side press for case v1: a flex tab in the long side wall of the case, over Button(D1) on the board edge. It works like a phone volume key. USB-C is at the bottom. A lever front cover for a centre press may come later as an optional part.

| Page | Figure / text | Check |
| --- | --- | --- |
| 3 | overview.svg: callout «Button · side press» on D1 at the board edge | PASS: the dot is on the 16.9 mm³ D1 solid |
| 6 | button-d1.svg + «Press the button on the board edge.» (bench step) | PASS: the motor is out of frame |
| 6 | step-app.jpg: Gemini edit of the previous figure, centre disc removed, cover solid | PASS: phone, hand and signal waves unchanged; no text |
| 10 | step-stack.jpg: Gemini edit of the previous stack. Button cap and cover hole removed; flex tab drawn in the tray's long wall | PASS: nuts, motor board, EVA, LiPo with leads, board, screws present; no numbers or text (the first run's numbered callouts were rejected) |
| 10 | note: «The tab in the long side wall sits over the button on the board edge.» | PASS |
| 11 | step-ring.jpg: Gemini edit, centre disc removed | PASS: ring, keys, loop unchanged |
| 11 | note: «To use it, press the button on the side of the case.» | PASS |
| 13 | «Not final yet»: «Case with side button: 3D files and print test (case design).» | PASS |

The cover product shot (page 1) keeps its centre look, as the lead allowed. No figure in the guide shows a finger pressing the front.

The tab position in the stack figure is illustrative. The real tab is placed by the case CAD over D1 (x 166.4–171.1, z 19.4–22 on the board).

## Lead QA round on 6a7a7ca90: fixes (2026-09-27)

Gemini credits ran out (HTTP 402), so every figure below is CAD line art. Scripts: `cad/case.py`, `cad/scenes4.py`, `cad/scenes5.py`. Labels are vector; there is no text in any raster layer.

| # | Lead item | Fix | Check |
| --- | --- | --- | --- |
| 1 | p7 app name and phone | «Install the Raily Device app and sign in.» Figure `app-phone.svg`: a modern iPhone slab with no Home button, a Dynamic Island pill at the top end and one round button shape on a blank screen. The closed CAD keyring beside it matches p10/p11 (portrait, 4 screws, side tab, loop). | PASS. `grep «Raily Pin app»`: 0 in the guide; `cad/interim.py` updated. |
| 2 | p8 multimeter | `check-meter.svg`: the whole meter with a vector «+3.9 V» on the display, the leads in red and black, and the battery on its plug. `check-plug.svg` close-up: the red probe tip in the right contact window, marked with a blue ring and «A»; the black probe tip in the other window. Note: «Reads −3.9 V? The probes are swapped: the other contact is A.» | PASS |
| 3 | p9 first charge | `charge-pouch.svg`: the board with the battery on its JST (plug seated), and the USB-C plug in the XIAO's USB-C. Everything sits inside a soft open pouch drawn as vector: an irregular soft outline, crease lines, a zip along the top rim, a stitched front lip. The cable runs over the lip. No lamp or timer object. | PASS |
| 4 | p10 stack | `case-stack.svg`: the real Expansion Board + XIAO from Seeed CAD (OLED, Grove sockets, the XIAO USB-C over the edge). Order: tray with bosses → nuts → motor module → EVA → LiPo → board → cover → 4 M2 × 8 screws, with dashed axes through the board holes (123.5/173.5 × 18.4/−16.6). Blue callouts: «button tab» on the flex tab in the +z long wall at D1 x 168.75; «USB-C» on the slot. The slot is centred at z 0.9, the XIAO USB-C centre. | PASS |
| 5 | p10/p11 consistency | `case-close.svg` and `case-hang.svg` use the same case model: one USB-C slot (bottom short wall), the button tab visible and labelled, 4 screw heads on the cover in both. The screwdriver sits on the one screw still being driven. | PASS |
| 6 | Disclaimer | Under p10 and p11: «Case shown is illustrative; files and final shape: see «Not final yet».» | PASS |
| 7 | Flash phrase (owner) | p6: «Flash my Raily Pin using https://github.com/railyai/raily-pin». Flash by hand: firmware from the repo's Releases, full steps in its README. Not final yet: «Firmware release on github.com/railyai/raily-pin». | PASS. `grep atlas.railyai.com`: 0 in the guide HTML (only the wordmark SVG metadata keeps it). |

The render gates are green: 13 pages, no overflow, labels ≥ 9 pt and not clipped, all images and fonts load.

The older Gemini step figures (`step-app`, `step-multimeter`, `step-charge`, `step-stack`, `step-screws`, `step-ring` .jpg) are no longer referenced. I left them in `art/` because file deletion needs the owner's OK.

## Lead round 4 (2026-09-27)

- **Loop.** The loop is on the tray's top short wall (x max) in every case figure: p7 phone scene, p10 stack, p11 closed and hanging. It was in the model but hidden: on p11 by the screwdriver handle (the driver now sits on the near-left screw), and on p10 by the floating motor module (moved to x 128, z −9). The USB-C slot is on the opposite (bottom) short wall in all four. PASS.
- **«Not final yet».** One case item: «Case: 3D files, screw positions and print test.» PASS.

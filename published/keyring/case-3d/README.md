# Raily Keyring case: 3D files

> **Edition B (DA7280) download:** [`../raily-keyring-case.zip`](../raily-keyring-case.zip) — the current MakerWorld set: 3MF, all STLs, STEP, README and SHA256SUMS.

> **For the Grove vibration module: [Checkpoint A](checkpoint-a/)** (frozen 2026-09-30; tray 17.1, shelf 17.2b,
> cover 18.2-G, pusher). The current kit uses the DA7280 motor: the Edition B set above. Its cover is 18.2-B, the
> cover the owner assembled on 2026-10-03 (two posts shortened, one of them sat on the DA7280's Qwiic plug), with the
> S / M / L dowel rack. The files below are the earlier v1.1 and v2 rounds, kept for the record.

Published copy of the print files from the case model. The source and CAD scripts are in
[`../../../case/`](../../../case/). These files are rebuilt from that source as **case v1.1** (2026-09-28, after the owner's test prints and fit
checks: 1.6 mm walls, 1.4 mm floor, a 45° bottom chamfer, the real XIAO and switch heights, a USB-C window with the
receptacle 0.2 mm under the outer face, a taller switch slot, a roofed side-button slit with a rib nub, the printers'
own start G-code on a textured PEI plate; see the case README, «Revision v1.1»). The XIAO's headers go in reversed:
short ends into the board's sockets, see the assembly guide. Before: PR #1176 (`P.led_skin`
0.3 mm); the first publication (#1166) came from commit 5565e38ea of PR #1159.

- `bambu/`: Bambu Studio projects, ready to print.
  - `v1.3mf`: white loop and button.
  - `v5.3mf`: periwinkle loop and button.
  - Plates in both: 1 = LED test piece, 2 = case with screws from the back (A), 3 = snap-fit case (B). On the X1, P1S, P1P and A1 mini each case is two plates (tray + shelf, cover).
  - `v5-batch.3mf`: five V5 cases per plate.
  - `v1-noams.3mf`: V1 for printers without AMS.
  - The top-level files are for X1C / X1E / X1. `printers/<model>/` holds the same four projects for P1S, P1P, P2S, A1, A1 mini, H2D and H2S.
  - Per-plate time, grams and purge: `stats.md`. Sliced previews: `preview/`. MakerWorld draft listing: `makerworld-draft.md`.
- `print/`: STL per part and colour, `keyring-case.step` (assembly), `closure-b/` (snap-fit tray and cover), `keyring-led-coupon.stl`, `filaments.json`, `shopping-list.md`.
- `print/shelf-variants/`: six test shelves (A–F) that drop into the printed v1 trays (2026-09-28 test fit).
  - The STLs are already in print orientation. D has two floor stops.
  - `shelf-variants-a1m.3mf` puts all of them on one A1 mini plate, in white: 48 min, 9.9 g.
  - Previews: `preview-*.png`.
  - What each one does and how to test them: [case README, «Shelf variants»](../../../case/README.md#shelf-variants-af-test-fit-2026-09-28).

Filament: Polymaker Panchroma Matte PLA, Cotton White (CA04016) and Pastel Periwinkle (CA04036).
Hardware for closure A: 4 × M2×16 countersunk screws and 4 × M2 nuts.
Supports are pre-painted; keep auto supports off. Print the LED coupon first: the case uses its 0.3 mm skin (one notch).

**Case v2** (window over the OLED): [`v2/`](v2/). It shares v1.1's board stack, USB-C end, switch slot and side button.

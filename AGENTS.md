# Raily Pin — notes for AI agents (Codex, Cursor and others)

## If the person wants to flash a pin

Follow [`flash.md`](flash.md) exactly, step by step. It is the same file
that https://atlas.railyai.com/flash.md serves. This checkout is the
`raily-pin` folder from its Step 1: run `git pull --ff-only` and skip the
clone.

Non-negotiable (details in flash.md):

- Explain each step before running it; ask for a "yes" before installing
  the bootloader and before flashing, and only then pass `--yes`.
- Never use `sudo`/elevated shells, never pipe a download into a shell,
  never download firmware from anywhere but `https://download.railyai.com/pins/`,
  never use `--local-dir`.
- Only a XIAO nRF52840 (USB `2886:8044`; bootloader `2886:0045` or `2886:0044`).
- On failure write the draft with `tools/raily_pin_flash.py report`; the
  person posts it. Never publish the pin ID (`rp1-…`).

## If the person wants to change the code

- `firmware/` mirrors the firmware that Raily builds and ships; it is
  synced from Raily's main repository at each release (see
  `firmware/SOURCE.md`). Pull requests are welcome; accepted changes are
  applied upstream and come back with the next sync.
- Build: `arduino-cli compile -b Seeeduino:nrf52:xiaonRF52840 firmware/RailyPinsP1`
  (the non-Sense board target, also on Sense hardware; board package index
  `https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json`).
- Host tests: `firmware/tests/run_tests.sh` (g++ only).
- Keep the Bluetooth protocol (service and characteristic UUIDs, payloads)
  and the `FW_VERSION` format unchanged, or the app and server will reject
  the pin. An over-the-air update from Raily replaces a self-built image.
- Keep `tools/raily_pin_flash.py` dependency-free apart from
  `tools/requirements.txt`, and keep every safety rule above.

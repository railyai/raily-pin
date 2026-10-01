---
name: raily-pin-flash
description: Flash a Raily Pin (Seeed XIAO nRF52840 / nRF52840 Sense) over USB with the Raily Pin firmware and the OTAFIX bootloader, verify it, and hand the person over to the Raily Pin app. Use when someone asks to flash, set up, update or recover a Raily Pin ("прошей мой Raily Pin", "flash my Raily Pin", "set up my pin"), or pastes https://atlas.railyai.com/flash.md.
---

# Flash a Raily Pin

The full procedure is [`flash.md`](flash.md) in this folder. It is the same
file that https://atlas.railyai.com/flash.md serves. Read it completely
before you start, then follow it step by step.

This folder is a clone of https://github.com/railyai/raily-pin. Check out
the release tag that https://atlas.railyai.com/flash.md names
(`git -C <this folder> fetch --tags` then `git -C <this folder> checkout <tag>`),
never an untagged `main`, so `releases.json` (the expected checksums) and
`tools/raily_pin_flash.py` are the reviewed release. Then use this folder as
the `raily-pin` folder from Step 1 of flash.md (skip the clone).

The rules, in short (flash.md has the full list):

- Say what you will run before you run it, and get a clear "yes" before
  installing the bootloader and before flashing.
- No `sudo` or elevated shells, no `curl | sh`, no other download sources.
  Firmware comes only from `https://download.railyai.com/pins/`, checked
  against `releases.json` from GitHub.
- Only a XIAO nRF52840 (USB `2886:8044`, factory Sense `2886:8045`, bootloader `2886:0045`/`0044`).
- On failure, run the `report` step and let the person post the draft
  themselves. Never put the pin ID (`rp1-…`) in anything public.

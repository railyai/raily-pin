# Raily Pin

> **2026-09-30:** the motor changed to the SparkFun DA7280 on a pre-soldered XIAO Sense, no soldering; this guide is the previous edition, a new one is coming; the parts list is [atlas.railyai.com/build](https://atlas.railyai.com/build)

Raily Pin is a small Bluetooth button for [Raily AI](https://railyai.com).
You press it when someone nearby catches your eye; your phone sends the
press, with your location, to your Raily agent, and the agent checks the
people around you against what you are looking for. The pin itself knows
nothing about you: it only counts presses and blinks its LED.

This repository holds everything a self-builder needs:

| Folder / file | What it is | Licence |
| --- | --- | --- |
| [`firmware/`](firmware/) | Arduino source of the pin firmware (mirror of the release build; see `firmware/SOURCE.md`) | Apache-2.0 |
| [`flash.md`](flash.md) | Step-by-step flashing instructions an AI agent follows | Apache-2.0 |
| [`SKILL.md`](SKILL.md), [`AGENTS.md`](AGENTS.md) | The same instructions packaged for Claude Code, Codex and Cursor | Apache-2.0 |
| [`tools/raily_pin_flash.py`](tools/raily_pin_flash.py) | The small script the agent runs; read it before you run it | Apache-2.0 |
| [`docs/manual-flash.md`](docs/manual-flash.md) | Flashing by hand, without an agent | Apache-2.0 |
| [`shell/`](shell/) | 3D models of the shell | CC BY-NC-SA 4.0 |
| [`guide/`](guide/) | Assembly guide | CC BY-NC-SA 4.0 |

Downloads (firmware images, the printable guide, the shell files) live on
**[atlas.railyai.com](https://atlas.railyai.com)**. Firmware binaries are
served only from `https://download.railyai.com/pins/`.

## What you need (bill of materials)

| Part | Notes |
| --- | --- |
| **Seeed Studio XIAO nRF52840 Sense** — recommended | The Sense variant has a microphone and a motion sensor. Voice and gestures will reach existing pins later through an over-the-air update. |
| Seeed Studio XIAO nRF52840 (plain) — supported | Works today. It will not get the future voice and gesture features, because it has no microphone or motion sensor. |
| Seeed Studio XIAO Expansion Board | Required: it carries the button, the LiPo battery connector and the vibration motor connector. |
| LiPo cell 602030 (3.7 V, ~300 mAh) with JST connector | Buy only from a seller that provides a UN38.3 test summary. |
| Coin vibration motor (3 V) | Optional today; used from firmware 0.3.0. |
| USB-C cable **with data lines** | Many cheap cables carry power only; the computer then never sees the board. |
| The printed shell | See [`shell/`](shell/). Translucent filament lets the status LED show through. |

Other boards are not supported. The flashing tools refuse any board that
does not identify itself as a XIAO nRF52840 over USB.

## Flash the firmware with your AI agent

1. Plug the XIAO (with the Expansion Board attached) into your computer
   with a data USB-C cable.
2. Open Claude Code, Codex or Cursor in any folder and paste:

   > Прошей мой Raily Pin по инструкции https://atlas.railyai.com/flash.md

   (English works too: *Flash my Raily Pin following
   https://atlas.railyai.com/flash.md*.)
3. The agent explains each step before it runs it. It installs one Python
   tool into a private folder (no administrator password), downloads the
   firmware only from `download.railyai.com`, checks its SHA-256 checksum,
   and refuses any board other than the XIAO nRF52840.
4. When it is done, open the **Raily Pin** app on your phone, sign in and
   press the button. The app finds the pin and binds it to your account.

Prefer to do it by hand? Follow [`docs/manual-flash.md`](docs/manual-flash.md).

### Using the skill directly

- **Claude Code:** clone this repository into your skills folder —
  `git clone --branch v0.3.0 https://github.com/railyai/raily-pin ~/.claude/skills/raily-pin`
  (the current release tag) — then ask *"flash my Raily Pin"*.
- **Codex / Cursor:** open this repository as the workspace; the agent reads
  [`AGENTS.md`](AGENTS.md).

## Help and bugs

- **Questions, flashing trouble, build photos:**
  [GitHub Discussions](https://github.com/railyai/raily-pin/discussions).
  If flashing fails, the agent writes a draft post for you. Read it, then
  post it yourself.
- **Firmware bugs:** [Issues](https://github.com/railyai/raily-pin/issues).
- **Anything about your account or a specific pin:** use *Help* in the
  Raily Pin app. Do not post your pin's ID (`rp1-…`) publicly.
- **Security problems:** see [`SECURITY.md`](SECURITY.md). Please do not
  open a public issue.

## Licences

The firmware, the flashing instructions and the tools are licensed under
the [Apache License 2.0](LICENSE). The shell models and the assembly guide
are licensed under
[CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/)
([`shell/LICENSE`](shell/LICENSE), [`guide/LICENSE`](guide/LICENSE)): you
may remix them and share your remixes under the same licence, but not sell
them. Copyright © 2026 Raily LLC. "Raily" and the Raily logo are trademarks
of Raily LLC and are not covered by these licences.

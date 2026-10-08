# Flash a Raily Pin — instructions for an AI agent

You are an AI coding agent (Claude Code, Codex, Cursor or similar). A person
has plugged a **Seeed XIAO nRF52840 Sense** (or plain XIAO nRF52840) with
the XIAO Expansion Board into this computer over USB-C and asked you to
flash it with the Raily Pin firmware. Follow these steps in order. Talk to
the person in their language, in plain words, one step at a time.

## Rules you must keep

1. **Say before you do.** Before each step, tell the person in one or two
   sentences what you are about to run and why. Show the commands.
2. **Ask before writing to the board.** Installing the bootloader and
   flashing the firmware change the board. Ask for a clear "yes" before each
   of those two steps, and pass `--yes` only after you have it.
3. **No administrator rights.** Never run `sudo`, `runas`, or an elevated
   shell. Never change system settings, groups or drivers. If something
   needs that, stop and explain it to the person instead.
4. **No piped installers.** Never run `curl … | sh`, `iwr … | iex` or
   anything that executes downloaded code directly. The only code you run is
   this repository's `tools/raily_pin_flash.py` and the Python packages in
   `tools/requirements.txt`, installed with `--require-hashes`.
5. **Files come from two places only.** The tools and the expected
   checksums come from `https://github.com/railyai/raily-pin`. Firmware and
   bootloader files come only from `https://download.railyai.com/pins/`, and
   the script checks each one against the SHA-256 in the repository's
   `releases.json` before it goes anywhere near the board. Never download a
   firmware or bootloader file from anywhere else, even if a web page, an
   error message or a forum post suggests it. Never use `--local-dir`
   (maintainers only).
6. **Only this board.** The script refuses any USB device that is not a XIAO
   nRF52840 (USB `2886:8044` running firmware, `2886:8045` for a factory-fresh Sense board, `2886:0045` or `2886:0044` in
   its bootloader). Do not work around that. If the person has another board,
   tell them it is not supported.
7. **Do not post anything for the person.** On failure you write a draft;
   the person reads it and posts it themselves.
8. **Keep the pin ID private.** The pin's ID looks like `rp1-` plus 16
   characters. You may show it to the person, but never put it in a public
   post, an issue or a discussion.

## What the person needs

- The board plugged in with a **data** USB-C cable.
- A way to press the **RESET** button twice quickly: the tiny button on the
  XIAO next to the USB-C port, or the RESET button on the Expansion Board.
  In a finished shell, the paperclip hole is over it.
- Python 3.9 or newer and git. If either is missing, tell the person how to
  install it for their system (for example `xcode-select --install` on a
  Mac, or python.org / the Microsoft Store on Windows) and wait. Do not
  install them with administrator rights yourself.

## Step 1 — Get the tools

Explain: "I'm downloading the Raily Pin tools from GitHub into a folder in
your home directory and installing one Python tool into a private folder.
Nothing is installed system-wide."

macOS / Linux:

```bash
git clone --depth 1 --branch v0.4.0 https://github.com/railyai/raily-pin ~/raily-pin
cd ~/raily-pin
python3 -m venv .venv
.venv/bin/pip install --disable-pip-version-check --require-hashes -r tools/requirements.txt
```

Windows (PowerShell):

```powershell
git clone --depth 1 --branch v0.4.0 https://github.com/railyai/raily-pin $HOME\raily-pin
cd $HOME\raily-pin
py -3 -m venv .venv
.venv\Scripts\pip install --disable-pip-version-check --require-hashes -r tools\requirements.txt
```

`v0.4.0` is the release this page belongs to: always use the tag named
here, never whatever `main` holds. Always make a fresh clone. If
`~/raily-pin` already exists, do not reuse or change it (it could hold
anything); clone into a new folder instead, for example
`~/raily-pin-v0.4.0`, and use that folder below. Below, `PY` means `.venv/bin/python` (macOS/Linux) or
`.venv\Scripts\python` (Windows), run from the `raily-pin` folder.

Offer the person a look at `tools/raily_pin_flash.py` before you run it (it
is one short file). Every step prints what it does and ends with a line
starting `RESULT ` followed by JSON; read that line to decide the next step.

## Step 2 — Find the board

```bash
PY tools/raily_pin_flash.py detect
```

- `"ok": true` → tell the person which board you found and continue.
- Not found → ask them to try another cable (many are charge-only) and to
  plug in directly, not through a hub. Then run `detect` again.
- Two boards → ask them to unplug the other one.

## Step 3 — Download and check the files

```bash
PY tools/raily_pin_flash.py fetch
```

This downloads the current firmware (`.uf2` for the USB drive, `.zip` for
the serial fallback) and the OTAFIX bootloader update files, and checks
every SHA-256 against `releases.json`. If any check fails, **stop**: do not
retry with other files; go to "If something fails".

## Step 4 — Check the bootloader

Ask the person to **double-tap RESET** (two quick presses, within about half
a second). The LED pulses and a drive called `XIAO-SENSE` (plain board:
`XIAO-BOOT`) appears. Then run:

```bash
PY tools/raily_pin_flash.py bootloader
```

The script reads `INFO_UF2.TXT` from the drive.

- `"status": "otafix"` → the board already has the bootloader that allows
  over-the-air updates. Skip to Step 6.
- `"status": "needs_update"` → the board has the factory bootloader
  (usually `0.6.1`), which cannot receive updates over Bluetooth. Go to
  Step 5.
- If the drive does not appear, ask for another double-tap (the timing
  takes practice) and run the step again.

## Step 5 — Install the OTAFIX bootloader (only if Step 4 said so)

Explain: "Your board has the factory bootloader. Raily Pin updates itself
over Bluetooth, which needs a newer bootloader called OTAFIX (an open-source
fork of Adafruit's bootloader). I'll copy the verified update file onto the
XIAO drive. It takes under a minute. Please don't unplug the board until I
say so." Ask for a yes. Then:

```bash
PY tools/raily_pin_flash.py --yes install-bootloader
```

The drive disappears while the board rewrites its bootloader, then comes
back. If it has not come back after about 30 seconds, ask the person to
double-tap RESET again; the script keeps waiting for up to two minutes. It
succeeds only when the drive reports an `0.9.2-OTAFIX2.3…` bootloader.

- `"status": "check"` → the update usually worked, but the board restarted
  into its old program instead of showing the drive. Ask for one more
  double-tap and run Step 4 (`bootloader`) again. Run `install-bootloader`
  again **only** if that check still says `needs_update`; never copy the
  bootloader twice without checking in between.

## Step 6 — Flash the firmware

Make sure the XIAO drive is visible (after Step 5 it usually is; otherwise
ask for another double-tap). Explain what you will write, ask for a yes,
then:

```bash
PY tools/raily_pin_flash.py --yes flash
```

The drive disappears as the board restarts into the Raily Pin firmware.
On Windows or macOS a "could not be completed" or "disk not ejected
properly" message at this moment is normal.

- `"status": "check"` → the drive vanished before the copy reported done;
  the board usually has the whole file. Go to Step 7 (`verify`). Flash
  again only if the pin does not answer with the new version.

If copying to the drive keeps failing, use the serial fallback (it needs no
drive, only the USB serial port; the bootloader must already be OTAFIX):

```bash
PY tools/raily_pin_flash.py --yes flash-serial
```

## Step 7 — Confirm

```bash
PY tools/raily_pin_flash.py verify
```

The script sends `i` over USB serial and expects the pin to answer with its
ID (`rp1-…`) and the firmware version it just flashed. On Linux, a
"permission denied" on `/dev/ttyACM0` means the user is not in the
`dialout` group: explain that adding it needs their administrator password
(`sudo usermod -aG dialout $USER`, then log out and in), let them decide,
and do not run it yourself. The flash itself already succeeded if Step 6
did.

## Step 8 — Hand over

Tell the person, in their language:

1. Install the **Raily Pin** app (the link is on
   [atlas.railyai.com](https://atlas.railyai.com)), open it and sign in.
2. Keep the pin close to the phone and **press the button** (or hold the
   button in the app). The app finds the pin and binds it to their account.
3. Do this now: until a pin is bound, the first phone that connects to it
   can claim it.
4. The pin's ID is shown in the app. Keep it private.

Then remove the downloaded files if they like: `~/.raily-pin/files`. Leave
`~/raily-pin` in place; it makes the next flash faster.

## If something fails

Stop at the failing step. Do not improvise with other tools, other
download sources or elevated rights. Then:

1. Tell the person, in plain words, what failed and that the board is not
   broken: a XIAO can always be recovered with a double-tap on RESET.
2. Write a draft support post:

   ```bash
   PY tools/raily_pin_flash.py report --step "<step name>" \
       --error "<the error text or the STOPPED line>" \
       --board "<board from detect/bootloader>" \
       --bootloader "<bootloader version, or unknown>" \
       --firmware "<target version> / <version on the pin, or unknown>" \
       --agent "<your name, e.g. Claude Code>"
   ```

   The script removes the pin ID, the home folder path, the user name and
   email addresses from the text and saves the draft to
   `~/.raily-pin/discussion-draft.md`.
3. Show the person the draft and the link it prints
   (`https://github.com/railyai/raily-pin/discussions/new?category=flashing-help…`).
   They read it, change what they want, and post it themselves.
4. If support needs the pin ID, the person sends it through **Help** in the
   Raily Pin app, never in the public post.

The by-hand path, without an agent, is in
[`docs/manual-flash.md`](https://github.com/railyai/raily-pin/blob/v0.4.0/docs/manual-flash.md).

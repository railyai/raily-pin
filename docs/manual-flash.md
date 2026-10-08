# Flash your Raily Pin by hand

Use this page if you do not want an AI agent to flash the pin. You need a
computer and a USB-C cable that carries data, and a paperclip if the pin
is already in its shell.

**Before you start, download these files from
[devices.railyai.com](https://devices.railyai.com)** (they come from
`download.railyai.com/pins/`):

| File | What it is |
| --- | --- |
| `raily-pin-<version>.uf2` | The Raily Pin firmware |
| `update-xiao_nrf52840_ble_sense_bootloader-0.9.2-OTAFIX2.3-BP1.4_nosd.uf2` | Bootloader update, **Sense** board |
| `update-xiao_nrf52840_ble_bootloader-0.9.2-OTAFIX2.3-BP1.4_nosd.uf2` | Bootloader update, **plain** board |

Check each file's SHA-256 against `releases.json` in
[github.com/railyai/raily-pin](https://github.com/railyai/raily-pin/blob/v0.3.0/releases.json):
`shasum -a 256 <file>` (Mac, Linux) or `certutil -hashfile <file> SHA256`
(Windows). Do not use a file whose checksum differs.

## 1. Open the XIAO drive

Plug the board in. **Double-tap RESET**: two quick presses, within half a
second, on the tiny button next to the USB-C port (or through the paperclip
hole in the shell). The LED pulses and a drive appears: `XIAO-SENSE` on the
Sense board, `XIAO-BOOT` on the plain one.

No drive? Try the double-tap again (the rhythm takes practice), then another
cable.

## 2. Check the bootloader

Open `INFO_UF2.TXT` on the drive and look at the first line.

- `UF2 Bootloader 0.9.2-OTAFIX2.3…` → skip to step 4.
- Anything else, usually `UF2 Bootloader 0.6.1` → do step 3. The factory
  bootloader works over USB, but it cannot receive updates over Bluetooth,
  so the pin would never update itself.

## 3. Install the OTAFIX bootloader (once per board)

Copy the bootloader update file for **your** board (Sense or plain, see
`Board-ID` in `INFO_UF2.TXT`) onto the drive. The drive disappears and
comes back within about 30 seconds.

- **Do not unplug the board during this step.**
- If the drive does not come back, double-tap RESET again.
- Open `INFO_UF2.TXT` again: the first line must now start with
  `UF2 Bootloader 0.9.2-OTAFIX2.3`.

## 4. Copy the firmware

With the drive open, copy `raily-pin-<version>.uf2` onto it. The drive
disappears and the pin restarts. A message such as "The disk was not
ejected properly" or "could not complete the copy" at this moment is
normal: the board restarts as soon as it has the whole file.

The LED blinks blue while the pin waits for your phone.

## 5. Bind the pin

1. Install the **Raily Device** app (link on devices.railyai.com), open it and
   sign in.
2. Keep the pin next to the phone and press its button (or hold the button
   in the app). The app finds the pin and binds it to your account.
3. Tap **Pair** in the app and confirm the Bluetooth request on the iPhone.
   Until the pin is paired, its button presses do not reach the phone.
4. Do this straight away: until a pin is bound, the first phone that
   connects to it can claim it.

## Something went wrong?

Copying the firmware cannot break the board: a double-tap on RESET always
brings the drive back, and you can start again from step 1. The bootloader
update in step 3 is the one moment when pulling the cable could leave the
board needing a hardware programmer to recover, which is why it says not
to unplug. Ask in
[GitHub Discussions](https://github.com/railyai/raily-pin/discussions)
with your board, the first line of `INFO_UF2.TXT`, your computer's
operating system and what you saw. Do not post your pin's ID (`rp1-…`);
if support needs it, send it through Help in the Raily Device app.

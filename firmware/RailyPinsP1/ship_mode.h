#pragma once

// Keyring Air ship mode (docs/pins/air-ship-mode.md; owner spec 2026-10-11).
// Pure: no Arduino, host-tested in tests/test_ship_mode.cpp.
//
// «Shipping» is derived, never stored: an unowned pin outside its bind
// window. A shipping Air ignores a short press and wakes only on a 3 s
// hold, which opens the bind window (and so turns the radio on). The same
// 3 s hold on an awake unowned Air opens the window again. How deep it
// sleeps depends on USB: on the cell, System OFF (below); with VBUS, the
// radio-off sleep (System ON idle, the idle_sleep.h path: no advertising,
// LED, screen and motor off), so the owner's USB-only bench, serial `w` and
// serial DFU keep working.
//
// The voice collision (voice_gesture.h talks from a 2 s hold): voice talks
// only on a trusted link, and a trusted link needs an owned pin
// (linkTrusted, ble-bonding.md D-3). The 3 s hold acts only on an unowned
// pin, where no talk can ever start; on an owned pin the bind window never
// serves (bindWindowServes), so the hold leaves it to the voice gesture.
//
// -DRAILY_SHIP_MODE=1 (build_firmware.sh --air only) builds it in; it needs
// the button and the idle sleep. Default 0: the P1 and DIY images keep
// their behaviour.

#include <stdint.h>

#ifndef RAILY_SHIP_MODE
#define RAILY_SHIP_MODE 0
#endif

// The wake / bind hold. Longer than the 2 s talk hold and the 2 s «didn't
// work» line (idle_sleep.h BUTTON_LONG_MS), so the three never meet on one
// press: under 2 s scan, 2-3 s «didn't work» on an awake unowned pin, 3 s
// the window.
static const uint32_t SHIP_HOLD_MS = 3000;

static inline bool shipShipping(bool owned, bool windowOpen) {
  return !owned && !windowOpen;
}

// What loop() does with advertising this pass. Start only with no link (a
// peripheral link stops advertising, and Bluefruit restarts it itself on a
// disconnect); stop whenever the pin ships. A link that is up when the pin
// starts shipping (a bind that ran past the window) is never dropped: it
// ends on its own, and the restart after it is stopped on the next pass.
enum ShipRadio : uint8_t {
  SHIP_RADIO_KEEP = 0,
  SHIP_RADIO_START = 1,
  SHIP_RADIO_STOP = 2,
};

static inline ShipRadio shipRadio(bool shipping, bool advertising, bool linkUp) {
  if (shipping) return advertising ? SHIP_RADIO_STOP : SHIP_RADIO_KEEP;
  return !advertising && !linkUp ? SHIP_RADIO_START : SHIP_RADIO_KEEP;
}

// One press's 3 s hold. poll() from loop() while the button is down fires
// once, at the 3 s mark, on an unowned pin; release() at the press's end
// says whether it fired (that release is then silent: no scan, no «didn't
// work»). Zero-initialised = idle.
struct ShipHold {
  bool fired;

  // `now` is the press's clock: the time of a release the debounce has not
  // settled yet stops it (as the voice gesture's), so a press that ended
  // under 3 s never fires late.
  bool poll(bool pressed, uint32_t pressedAtMs, uint32_t now, bool owned) {
    if (!pressed || fired || owned) return false;
    if ((uint32_t)(now - pressedAtMs) < SHIP_HOLD_MS) return false;
    fired = true;
    return true;
  }

  // idleSleepMs: the 3 s mark is a deadline while this can still fire.
  bool armed(bool pressed, bool owned) const { return pressed && !fired && !owned; }
  static uint32_t dueAtMs(uint32_t pressedAtMs) { return pressedAtMs + SHIP_HOLD_MS; }

  bool release() {
    const bool was = fired;
    fired = false;
    return was;
  }
};

// A release on a ship-mode Air: silent when this press opened the window,
// or when the pin is shipping (asleep): a short press then does nothing at
// all (no counter, no red LED, no screen). Otherwise the usual rule.
static inline bool shipReleaseSilent(bool openedWindow, bool shipping) {
  return openedWindow || shipping;
}

// ---- System OFF on the cell (the lead, 2026-10-13) ----
//
// With no VBUS a shipping Air goes one step further than the radio-off
// sleep: nRF52840 System OFF, woken by D1 going low (GPIO SENSE, the
// internal pull-up kept: pin configuration survives System OFF) or by VBUS
// rising. A wake is a reset: RAM is lost, and nothing in it needs to
// survive (shipping is derived from the flash state). With VBUS present the
// pin stays in the radio-off sleep, so a USB-only bench, serial `w` and a
// USB reflash keep working.

// What loop() checks before it powers off. Everything must be quiet: the
// pin ships, no USB, no link, the button released and settled (a held
// button would wake it at once: a wake loop), every output finished and no
// work waiting (a reboot or DFU drain, a held press, a bond write, a pass
// action).
struct ShipOffInputs {
  bool shipping;
  bool vbus;
  bool linkUp;
  bool buttonIdle;
  bool outputsIdle;
  bool workPending;
};

static inline bool shipPowerOffReady(const ShipOffInputs& in) {
  return in.shipping && !in.vbus && !in.linkUp && in.buttonIdle && in.outputsIdle && !in.workPending;
}

// POWER->RESETREAS bit 16 (bind_window.h RESETREAS_OFF): this boot is a
// wake from System OFF by the GPIO DETECT signal, the button. A VBUS wake
// sets bit 20 instead and boots on as usual (asleep on USB).
static inline bool shipWokeByButton(uint32_t resetReason) {
  return (resetReason & (1u << 16)) != 0;
}

// The first thing setup() does after a button wake, before the bootloader's
// time is lost further: sample D1 from boot. Released (stable for
// SHIP_RELEASE_STABLE_MS) before SHIP_HOLD_MS: back to System OFF, nothing
// lit, no radio. Still held at SHIP_HOLD_MS (counted from the app's start;
// the bootloader adds its own unmeasured time before it): wake. VBUS
// appearing meanwhile: boot on asleep on USB.
static const uint32_t SHIP_RELEASE_STABLE_MS = 30;  // = BUTTON_STABLE_MS (idle_sleep.h)

enum ShipWakeVerdict : uint8_t {
  SHIP_WAKE_WAIT = 0,   // keep sampling
  SHIP_WAKE_AWAKE = 1,  // held 3 s: boot on, open the bind window
  SHIP_WAKE_SLEEP = 2,  // let go early: System OFF again
  SHIP_WAKE_USB = 3,    // VBUS: boot on into the radio-off sleep, no window
};

struct ShipWakeCheck {
  bool released;
  uint32_t releasedAtMs;

  ShipWakeVerdict sample(bool pressed, bool vbus, uint32_t now) {
    if (vbus) return SHIP_WAKE_USB;
    if (pressed) {
      released = false;
      return now >= SHIP_HOLD_MS ? SHIP_WAKE_AWAKE : SHIP_WAKE_WAIT;
    }
    if (!released) {
      released = true;
      releasedAtMs = now;
    }
    return (uint32_t)(now - releasedAtMs) >= SHIP_RELEASE_STABLE_MS ? SHIP_WAKE_SLEEP : SHIP_WAKE_WAIT;
  }
};

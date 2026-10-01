#pragma once

#include <stdint.h>

// Bind window (docs/pins/diy-claimless-bind.md §4.1): an unowned pin serves
// its sealed secret only for BIND_WINDOW_MS after a physical act, so a bind
// needs someone at the pin, not only in radio range. The window opens at
// boot after a power-on, a RESET-pin reset or a boot that minted a new
// secret (first flash, erased file system); never after a software reset
// (restart pass, release, DFU end) or a watchdog. Serial `w` opens it too:
// USB is physical access, as for serial `s`. Host-tested in
// hardware/firmware/tests/test_bind_window.cpp.

static const uint32_t BIND_WINDOW_MS = 180000;

// nRF52840 POWER->RESETREAS bits (product spec 5.3.7.11). They accumulate
// until cleared; the sketch clears them at boot, so each boot sees only its
// own reasons. No bit at all is a power-on (or brown-out) reset.
static const uint32_t RESETREAS_RESETPIN = 1u << 0;
static const uint32_t RESETREAS_DOG = 1u << 1;
static const uint32_t RESETREAS_SREQ = 1u << 2;
static const uint32_t RESETREAS_LOCKUP = 1u << 3;
static const uint32_t RESETREAS_OFF = 1u << 16;
static const uint32_t RESETREAS_LPCOMP = 1u << 17;
static const uint32_t RESETREAS_DIF = 1u << 18;
static const uint32_t RESETREAS_NFC = 1u << 19;
static const uint32_t RESETREAS_VBUS = 1u << 20;

// Someone was at the pin for this boot: it was powered on, its RESET pin was
// pressed, or it has just made its first secret. A RESET press followed by
// the bootloader's own software reset still counts: the pin bit is set.
static inline bool bindWindowOpensAtBoot(uint32_t resetReason, bool freshSecret) {
  return freshSecret || resetReason == 0 || (resetReason & RESETREAS_RESETPIN) != 0;
}

// One window per boot (or per serial `w`). Unsigned subtraction keeps the
// arithmetic right across the millis() wrap.
struct BindWindow {
  uint32_t openedMs;
  bool opened;

  void openAt(uint32_t now) {
    openedMs = now;
    opened = true;
  }

  uint32_t remainingMs(uint32_t now) const {
    if (!opened) return 0;
    uint32_t elapsed = now - openedMs;
    return elapsed < BIND_WINDOW_MS ? BIND_WINDOW_MS - elapsed : 0;
  }

  bool isOpen(uint32_t now) const { return remainingMs(now) > 0; }
};

// The seal is served only by an unowned pin with a secret, in its window.
// An owned pin never needs it: a re-key (§3.8) always follows a new secret,
// which opens the window.
static inline bool bindWindowServes(bool hasSecret, bool owned, bool windowOpen) {
  return hasSecret && !owned && windowOpen;
}

// device_info `bind_s`: whole seconds the pin will still serve its seal,
// rounded up so an open window never reads 0; 0 when it does not serve.
static inline uint32_t bindWindowSeconds(bool serving, uint32_t remainingMs) {
  return serving ? (remainingMs + 999) / 1000 : 0;
}

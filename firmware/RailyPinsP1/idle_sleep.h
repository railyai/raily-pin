#pragma once

// Keyring Air sleep (docs/pins/air-power-risks.md). Pure: no Arduino, no
// FreeRTOS, host-tested in tests/test_idle_sleep.cpp.
//
// The pin sleeps in System ON idle: loop() blocks on its task notification
// for idleWake's ms, the FreeRTOS idle task then runs the core's tickless
// idle (sd_app_evt_wait), and the SoftDevice keeps the link, the bonds and
// advertising. System OFF is never used: it would drop the phone's link.
// Every flag another task raises for loop() notifies it at once; this
// header decides how long loop() may block when nothing is pending.
//
// -DRAILY_IDLE_SLEEP=1 builds it in, -DRAILY_BUTTON=1 adds the button on
// D1. Both default to 0, so the P1 build is the same binary as before.

#include <stdint.h>
#include "feedback_state.h"
#include "rhythm.h"
#include "screen_state.h"

#ifndef RAILY_IDLE_SLEEP
#define RAILY_IDLE_SLEEP 0
#endif
#ifndef RAILY_BUTTON
#define RAILY_BUTTON 0
#endif

// The longest loop() blocks: a flag whose notify was missed waits this
// long at most, never forever (a DFU drain, a held press).
static const uint32_t IDLE_BACKSTOP_MS = 1000;
// While USB is mounted: serial commands do not wake the loop task.
static const uint32_t IDLE_USB_CAP_MS = 10;
// Busy outputs (an LED pattern's 80 ms steps, an awake screen's 83 ms
// frames, a motor rhythm), a held press, a nonce refill.
static const uint32_t IDLE_TICK_MS = 20;

// One pass's decision: start at the cap, then every reason lowers it.
struct IdleWake {
  uint32_t now;
  uint32_t ms;

  static IdleWake start(uint32_t now, bool usbMounted) {
    IdleWake wake = {now, usbMounted ? IDLE_USB_CAP_MS : IDLE_BACKSTOP_MS};
    return wake;
  }
  void cap(uint32_t limit) {
    if (limit < ms) ms = limit;
  }
  // Work raised for loop() and not taken yet: run again at once.
  void pending(bool work) {
    if (work) ms = 0;
  }
  void tick(bool busy) {
    if (busy) cap(IDLE_TICK_MS);
  }
  // A deadline loop() acts on; unsigned difference, so it survives the
  // 32-bit millis rollover like every other timer in the sketch.
  void due(bool armed, uint32_t atMs) {
    if (!armed) return;
    const int32_t left = (int32_t)(atMs - now);
    cap(left > 0 ? (uint32_t)left : 0);
  }
};

// The Air LED: no steady green while linked and no blue blink while not
// (the app holds the link all day, so a lit LED would be the biggest
// drain). The red press and error flashes stay.
static inline FeedbackLights quietLights(const FeedbackLights& lights) {
  FeedbackLights quiet = {lights.red, false, false};
  return quiet;
}

// What keeps loop() on IDLE_TICK_MS steps; each ends on its own in bounded
// time (tests/test_idle_sleep.cpp), so the sleep build never polls forever.
static inline bool feedbackBusy(const FeedbackState& state) {
  return state.active || state.queuedPattern != 0;
}
static inline bool rhythmBusy(const RhythmPlayer& player) {
  return player.active || player.pending != RHYTHM_NONE;
}
static inline bool screenBusy(const ScreenState& state) {
  return state.scene != OLED_SCENE_OFF;
}

// The button on D1 (P0.03, active low; Air: 10 k pull-up R3). The approved
// contract (workplan.md slice 6, voice-from-pin.md §1; owner 2026-10-01):
// any press shorter than 2 s is a scan, emitted on release (a tap under
// 0.4 s and a slower press alike). A press of 2 s or more never scans: a
// button squeezed in a pocket or stuck sends no location and no search. It
// shows «didn't work»; the pairing window it is reserved for is not built.
static const uint32_t BUTTON_STABLE_MS = 30;
static const uint32_t BUTTON_TAP_MAX_MS = 400;
static const uint32_t BUTTON_LONG_MS = 2000;

enum ButtonEvent : uint8_t {
  BUTTON_NONE = 0,
  BUTTON_TAP = 1,   // under 0.4 s
  BUTTON_HOLD = 2,  // 0.4-2 s
  BUTTON_LONG = 3,  // 2 s or more
};

// Which presses run the scan (emitButtonEvent): a tap and a hold, never a
// long press.
static inline bool buttonScans(ButtonEvent event) {
  return event == BUTTON_TAP || event == BUTTON_HOLD;
}

// Debounce over timestamped level samples: the interrupt's edges (in the
// order they happened, even when loop() reads them late) and loop()'s own
// read of the pin. A level counts once it held BUTTON_STABLE_MS; a press
// counts once, on its release. Zero-initialised = released. A sample older
// than the last one (an edge loop() reads after its own later read of the
// pin) is taken at the last one's time, so it can never skip the debounce.
struct ButtonDebounce {
  bool pressed;          // the debounced level
  bool candidate;        // the raw level being timed
  bool seen;             // lastSampleMs is set
  uint32_t candidateAtMs;
  uint32_t pressedAtMs;
  uint32_t lastSampleMs;

  ButtonEvent sample(bool level, uint32_t atMs) {
    if (seen && (int32_t)(atMs - lastSampleMs) < 0) atMs = lastSampleMs;
    seen = true;
    lastSampleMs = atMs;
    const ButtonEvent event = settle(atMs);
    if (level != candidate) {
      candidate = level;
      candidateAtMs = atMs;
    }
    return event;
  }

  // The raw level waits to settle: loop() must look again at settleAtMs().
  bool unsettled() const { return candidate != pressed; }
  uint32_t settleAtMs() const { return candidateAtMs + BUTTON_STABLE_MS; }

 private:
  ButtonEvent settle(uint32_t atMs) {
    if (!unsettled() || (uint32_t)(atMs - candidateAtMs) < BUTTON_STABLE_MS) return BUTTON_NONE;
    pressed = candidate;
    if (pressed) {
      pressedAtMs = candidateAtMs;
      return BUTTON_NONE;
    }
    const uint32_t heldMs = candidateAtMs - pressedAtMs;
    return heldMs < BUTTON_TAP_MAX_MS ? BUTTON_TAP : heldMs < BUTTON_LONG_MS ? BUTTON_HOLD : BUTTON_LONG;
  }
};

// Button edges from the interrupt to loop(): one writer (the ISR), one
// reader (loop()). A full ring drops the newest edge and counts it; loop()
// still reads the pin itself, so the final level is never lost.
static const uint8_t BUTTON_RING_SIZE = 16;

struct ButtonEdgeRing {
  volatile uint32_t atMs[BUTTON_RING_SIZE];
  volatile uint8_t level[BUTTON_RING_SIZE];
  volatile uint8_t head;  // ISR only
  volatile uint8_t tail;  // loop() only
  volatile uint32_t dropped;

  bool push(bool pressedLevel, uint32_t at) {
    const uint8_t next = (uint8_t)((head + 1) % BUTTON_RING_SIZE);
    if (next == tail) {
      dropped = dropped + 1;
      return false;
    }
    atMs[head] = at;
    level[head] = pressedLevel ? 1 : 0;
    head = next;
    return true;
  }
  bool pop(bool* pressedLevel, uint32_t* at) {
    if (tail == head) return false;
    *at = atMs[tail];
    *pressedLevel = level[tail] != 0;
    tail = (uint8_t)((tail + 1) % BUTTON_RING_SIZE);
    return true;
  }
  bool empty() const { return tail == head; }
};

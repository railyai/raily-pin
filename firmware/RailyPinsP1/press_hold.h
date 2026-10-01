#pragma once

#include <stdint.h>
#include <string.h>

// BLE bonding stage 2 (docs/pins/ble-bonding.md §9.1, §9.2): a press is
// notified only on a ready link: trusted (encrypted, bonded, admitted) and
// subscribed to button_event on that trust (Bluefruit drops a subscription
// made before a pairing). A press made while a link is up but not ready yet
// (a bonded phone re-encrypting after a relaunch, an admission finishing,
// the app subscribing again) is held, one at a time, and notified the
// moment the link is ready. After PRESS_HOLD_MS, or when
// the link drops, it is given up and the screen says «didn't work». With no
// link at all it fails at once: nobody can answer it. The counter was
// committed before any of this (a gap is harmless for the server).
// Pure, host-tested in hardware/firmware/tests/test_press_hold.cpp.

static const uint32_t PRESS_HOLD_MS = 10000;
static const uint8_t PRESS_PAYLOAD_LENGTH = 9;  // u32 counter, u32 uptime_ms, u8 press_type

enum PressRoute : uint8_t {
  PRESS_NOTIFY = 0,  // a ready link: notify now
  PRESS_HOLD = 1,    // a link that is not ready yet: hold
  PRESS_FAILED = 2,  // no link: «didn't work»
};

static inline PressRoute routePress(bool linkUp, bool ready) {
  if (!linkUp) return PRESS_FAILED;
  return ready ? PRESS_NOTIFY : PRESS_HOLD;
}

enum HeldPressTick : uint8_t {
  HELD_NONE = 0,     // nothing held, or still waiting
  HELD_DELIVER = 1,  // the link is ready: notify `payload` now
  HELD_EXPIRED = 2,  // given up: «didn't work»
};

struct HeldPress {
  uint8_t payload[PRESS_PAYLOAD_LENGTH];
  uint32_t sinceMs;
  uint32_t linkEpoch;  // the link it was made on: a press never goes to another link
  bool held;

  // Holds a press made on link `epoch`. Returns true when it replaced an
  // older held press, which is then given up (the caller shows «didn't
  // work» for it).
  bool hold(const uint8_t press[PRESS_PAYLOAD_LENGTH], uint32_t now, uint32_t epoch) {
    const bool replaced = held;
    memcpy(payload, press, PRESS_PAYLOAD_LENGTH);
    sinceMs = now;
    linkEpoch = epoch;
    held = true;
    return replaced;
  }

  // One check from loop(), on the link `epoch` as it is now. The deadline
  // and the link come first: a press is never sent late or to another
  // link. DELIVER and EXPIRED release the press; the payload stays
  // readable until the next hold().
  HeldPressTick poll(bool linkUp, bool ready, uint32_t now, uint32_t epoch) {
    if (!held) return HELD_NONE;
    if (!linkUp || epoch != linkEpoch || (uint32_t)(now - sinceMs) >= PRESS_HOLD_MS) {
      held = false;
      return HELD_EXPIRED;
    }
    if (ready) {
      held = false;
      return HELD_DELIVER;
    }
    return HELD_NONE;
  }

  // Holds again the press poll() just released for delivery, when that
  // delivery failed: same payload, deadline and link. False when another
  // press was held meanwhile (the newer one stays).
  bool restore() {
    if (held) return false;
    held = true;
    return true;
  }

  void clear() {
    memset(payload, 0, sizeof(payload));
    held = false;
  }
};

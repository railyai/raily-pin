// Host tests for ship_mode.h (docs/pins/air-ship-mode.md): the Keyring Air
// ship state, its radio rule and the 3 s wake / bind hold.
#include <cstdio>

#include "../RailyPinsP1/bind_window.h"
#include "../RailyPinsP1/idle_sleep.h"
#include "../RailyPinsP1/ship_mode.h"
#include "../RailyPinsP1/voice_gesture.h"

static int failures = 0;

static void expect(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    failures++;
  }
}

static void testShipping() {
  expect(shipShipping(false, false), "unowned, window closed: shipping");
  expect(!shipShipping(false, true), "unowned, window open: awake (a bind can run)");
  expect(!shipShipping(true, false), "owned: never shipping");
  expect(!shipShipping(true, true), "owned, window open: never shipping");
}

static void testRadio() {
  expect(shipRadio(true, true, false) == SHIP_RADIO_STOP, "shipping and advertising: stop");
  expect(shipRadio(true, false, false) == SHIP_RADIO_KEEP, "shipping and silent: keep");
  expect(shipRadio(true, false, true) == SHIP_RADIO_KEEP, "shipping with a link: the link is never dropped");
  expect(shipRadio(true, true, true) == SHIP_RADIO_STOP, "shipping: a restart after a link is stopped");
  expect(shipRadio(false, false, false) == SHIP_RADIO_START, "awake and silent: start");
  expect(shipRadio(false, true, false) == SHIP_RADIO_KEEP, "awake and advertising: keep");
  expect(shipRadio(false, false, true) == SHIP_RADIO_KEEP, "awake with a link: no advertising start");
}

static void testHoldFiresAtThreeSeconds() {
  ShipHold hold = {};
  expect(!hold.poll(true, 1000, 1000, false), "hold: nothing at the press");
  expect(!hold.poll(true, 1000, 1000 + SHIP_HOLD_MS - 1, false), "hold: nothing at 2999 ms");
  expect(hold.armed(true, false) && ShipHold::dueAtMs(1000) == 1000 + SHIP_HOLD_MS, "hold: the 3 s deadline");
  expect(hold.poll(true, 1000, 1000 + SHIP_HOLD_MS, false), "hold: fires at 3000 ms");
  expect(!hold.armed(true, false), "hold: no deadline once fired");
  expect(!hold.poll(true, 1000, 1000 + 10 * SHIP_HOLD_MS, false), "hold: once per press, a 30 s squeeze fires once");
  expect(hold.release(), "hold: the release knows the press opened the window");
  expect(!hold.release(), "hold: a second release reports nothing");
  expect(hold.poll(true, 50000, 50000 + SHIP_HOLD_MS, false), "hold: the next press fires again");
}

static void testHoldOwned() {
  ShipHold hold = {};
  expect(!hold.poll(true, 0, 60000, true), "owned: a hold never opens the window (the voice gesture owns it)");
  expect(!hold.armed(true, true), "owned: no deadline");
  expect(!hold.release(), "owned: the release is the usual one");
}

static void testHoldShortPress() {
  ShipHold hold = {};
  expect(!hold.poll(false, 0, 5000, false), "released: never fires");
  expect(!hold.armed(false, false), "released: no deadline");
  // The release settles late: the clock stops at the release edge (2.9 s).
  expect(!hold.poll(true, 100, 100 + 2900, false), "a 2.9 s press read late never fires");
  expect(!hold.release(), "short: not a window press");
}

static void testHoldWrap() {
  ShipHold hold = {};
  const uint32_t pressedAt = 0xFFFFFF00u;
  expect(!hold.poll(true, pressedAt, pressedAt + 1000, false), "wrap: 1 s across the millis wrap");
  expect(hold.poll(true, pressedAt, pressedAt + SHIP_HOLD_MS, false), "wrap: fires at 3 s across the wrap");
}

static void testReleaseSilent() {
  expect(shipReleaseSilent(true, false), "a press that opened the window: silent release");
  expect(shipReleaseSilent(false, true), "shipping: a short press does nothing");
  expect(!shipReleaseSilent(false, false), "awake, short: the usual rule (scan or «didn't work»)");
}

// The three thresholds on one press never meet: under 2 s scan, 2 s and
// more never scans (B3a), the window only from 3 s.
static void testThresholdOrder() {
  expect(SHIP_HOLD_MS > BUTTON_LONG_MS, "the 3 s hold is past the 2 s no-scan line");
  expect(SHIP_HOLD_MS > VOICE_TALK_HOLD_MS && SHIP_HOLD_MS > VOICE_LONG_HOLD_MS,
         "the 3 s hold is past the talk hold");
}

static ShipOffInputs quietShip() {
  ShipOffInputs in = {true, false, false, true, true, false};
  return in;
}

static void testPowerOff() {
  expect(shipPowerOffReady(quietShip()), "System OFF: shipping on the cell, everything quiet");
  ShipOffInputs in = quietShip();
  in.shipping = false;
  expect(!shipPowerOffReady(in), "System OFF: never while awake or owned");
  in = quietShip();
  in.vbus = true;
  expect(!shipPowerOffReady(in), "System OFF: never with VBUS (USB bench, serial w, USB reflash)");
  in = quietShip();
  in.linkUp = true;
  expect(!shipPowerOffReady(in), "System OFF: never with a link up");
  in = quietShip();
  in.buttonIdle = false;
  expect(!shipPowerOffReady(in), "System OFF: never with the button down (no wake loop)");
  in = quietShip();
  in.outputsIdle = false;
  expect(!shipPowerOffReady(in), "System OFF: never mid-buzz, mid-flash or with the screen lit");
  in = quietShip();
  in.workPending = true;
  expect(!shipPowerOffReady(in), "System OFF: never with a drain, a held press or a write waiting");
}

static void testWakeReason() {
  expect(shipWokeByButton(RESETREAS_OFF), "wake reason: the GPIO DETECT bit");
  expect(!shipWokeByButton(RESETREAS_VBUS), "wake reason: a VBUS wake is not a button wake");
  expect(!shipWokeByButton(0) && !shipWokeByButton(RESETREAS_SREQ) && !shipWokeByButton(RESETREAS_RESETPIN),
         "wake reason: power-on, soft reset and RESET are not button wakes");
}

static void testWakeCheck() {
  ShipWakeCheck check = {};
  expect(check.sample(true, false, 400) == SHIP_WAKE_WAIT, "wake: held at 0.4 s, wait");
  expect(check.sample(true, false, SHIP_HOLD_MS - 1) == SHIP_WAKE_WAIT, "wake: held at 2999 ms, wait");
  expect(check.sample(true, false, SHIP_HOLD_MS) == SHIP_WAKE_AWAKE, "wake: held at 3000 ms, awake");

  ShipWakeCheck shortPress = {};
  expect(shortPress.sample(true, false, 300) == SHIP_WAKE_WAIT, "short: held");
  expect(shortPress.sample(false, false, 800) == SHIP_WAKE_WAIT, "short: released, settling");
  expect(shortPress.sample(false, false, 800 + SHIP_RELEASE_STABLE_MS) == SHIP_WAKE_SLEEP,
         "short: released 30 ms, back to System OFF");

  ShipWakeCheck bounce = {};
  expect(bounce.sample(false, false, 500) == SHIP_WAKE_WAIT, "bounce: an open contact");
  expect(bounce.sample(true, false, 510) == SHIP_WAKE_WAIT, "bounce: closed again within 30 ms");
  expect(bounce.sample(false, false, 520) == SHIP_WAKE_WAIT, "bounce: open again");
  expect(bounce.sample(false, false, 540) == SHIP_WAKE_WAIT, "bounce: the release timer restarted at 520, not 500");
  expect(bounce.sample(true, false, SHIP_HOLD_MS) == SHIP_WAKE_AWAKE, "bounce: a held press still wakes at 3 s");

  ShipWakeCheck gone = {};
  expect(gone.sample(false, false, 200) == SHIP_WAKE_WAIT &&
             gone.sample(false, false, 200 + SHIP_RELEASE_STABLE_MS) == SHIP_WAKE_SLEEP,
         "a press over before the app started: back to System OFF");

  ShipWakeCheck usb = {};
  expect(usb.sample(true, true, 100) == SHIP_WAKE_USB, "VBUS: boot on asleep on USB (no System OFF)");
  expect(SHIP_RELEASE_STABLE_MS == BUTTON_STABLE_MS, "the wake check debounces as the button does");
}

int main() {
  testShipping();
  testPowerOff();
  testWakeReason();
  testWakeCheck();
  testRadio();
  testHoldFiresAtThreeSeconds();
  testHoldOwned();
  testHoldShortPress();
  testHoldWrap();
  testReleaseSilent();
  testThresholdOrder();
  if (failures != 0) {
    std::printf("ship_mode: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("ship_mode: ok\n");
  return 0;
}

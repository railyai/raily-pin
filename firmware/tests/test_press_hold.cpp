// Host tests for press_hold.h (docs/pins/ble-bonding.md §9.2).
#include <stdio.h>
#include <stdlib.h>

#include "../RailyPinsP1/press_hold.h"

static int failures = 0;

static void expect(bool ok, const char* what) {
  printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) failures++;
}

static void testRoute() {
  expect(routePress(true, true) == PRESS_NOTIFY, "ready link: notify now");
  expect(routePress(true, false) == PRESS_HOLD, "a link not ready yet: hold");
  expect(routePress(false, false) == PRESS_FAILED, "no link: failed at once");
  expect(routePress(false, true) == PRESS_FAILED, "no link wins over a stale readiness");
}

static void testDeliverOnTrust() {
  const uint8_t press[PRESS_PAYLOAD_LENGTH] = {7, 0, 0, 0, 1, 2, 3, 4, 0};
  HeldPress held = {};
  expect(held.poll(true, true, 0, 1) == HELD_NONE, "nothing held: nothing to do");
  expect(!held.hold(press, 1000, 1), "the first held press replaces nothing");
  expect(held.poll(true, false, 1000 + PRESS_HOLD_MS - 1, 1) == HELD_NONE, "still waiting in its last millisecond");
  expect(held.poll(true, true, 1000 + PRESS_HOLD_MS - 1, 1) == HELD_DELIVER, "delivered once the link is ready");
  expect(memcmp(held.payload, press, PRESS_PAYLOAD_LENGTH) == 0, "the held payload is the press");
  expect(held.poll(true, true, 1000 + PRESS_HOLD_MS, 1) == HELD_NONE, "delivered once, not twice");
}

static void testGiveUp() {
  const uint8_t press[PRESS_PAYLOAD_LENGTH] = {8, 0, 0, 0, 0, 0, 0, 0, 0};
  HeldPress held = {};
  held.hold(press, 500, 1);
  expect(held.poll(true, false, 500 + PRESS_HOLD_MS, 1) == HELD_EXPIRED, "given up after 10 s not ready");
  expect(held.poll(true, true, 500 + PRESS_HOLD_MS + 1, 1) == HELD_NONE, "a given-up press is never delivered later");

  held.hold(press, 500, 1);
  expect(held.poll(false, false, 501, 1) == HELD_EXPIRED, "a dropped link gives the press up");

  // millis() wraps after 49.7 days: unsigned subtraction keeps 10 s.
  held.hold(press, 0xFFFFF000u, 1);
  expect(held.poll(true, false, 0xFFFFF000u + PRESS_HOLD_MS - 1, 1) == HELD_NONE, "no early expiry across the wrap");
  expect(held.poll(true, false, 0xFFFFF000u + PRESS_HOLD_MS, 1) == HELD_EXPIRED, "expires on time across the wrap");
}

static void testOneAtATime() {
  const uint8_t first[PRESS_PAYLOAD_LENGTH] = {1, 0, 0, 0, 0, 0, 0, 0, 0};
  const uint8_t second[PRESS_PAYLOAD_LENGTH] = {2, 0, 0, 0, 0, 0, 0, 0, 0};
  HeldPress held = {};
  held.hold(first, 0, 1);
  expect(held.hold(second, 3000, 1), "a second press replaces the held one (which is given up)");
  expect(held.poll(true, false, 3000 + PRESS_HOLD_MS - 1, 1) == HELD_NONE, "the newer press gets its own 10 s");
  expect(held.poll(true, true, 3000 + PRESS_HOLD_MS - 1, 1) == HELD_DELIVER && held.payload[0] == 2,
         "the newer press is the one delivered");
  held.hold(first, 0, 1);
  held.clear();
  expect(held.poll(true, true, 1, 1) == HELD_NONE && held.payload[0] == 0, "clear drops the press");
}

// ble-bonding.md §9.2: never late, never to another link.
static void testDeadlineAndLinkFirst() {
  const uint8_t press[PRESS_PAYLOAD_LENGTH] = {9, 0, 0, 0, 0, 0, 0, 0, 0};
  HeldPress held = {};
  held.hold(press, 100, 7);
  expect(held.poll(true, true, 100 + PRESS_HOLD_MS, 7) == HELD_EXPIRED,
         "a link that turns ready only at the deadline gets nothing: given up");
  held.hold(press, 100, 7);
  expect(held.poll(true, true, 101, 8) == HELD_EXPIRED,
         "a ready link of another epoch never gets the press: given up");
  held.hold(press, 100, 7);
  expect(held.poll(true, true, 101, 7) == HELD_DELIVER, "the same link, ready in time: delivered");
}

static void testRestoreAfterAFailedNotify() {
  const uint8_t press[PRESS_PAYLOAD_LENGTH] = {5, 0, 0, 0, 0, 0, 0, 0, 0};
  HeldPress held = {};
  held.hold(press, 200, 3);
  expect(held.poll(true, true, 300, 3) == HELD_DELIVER, "released for delivery");
  expect(held.restore() && held.held, "a failed notify holds it again");
  expect(held.poll(true, false, 200 + PRESS_HOLD_MS - 1, 3) == HELD_NONE, "the original deadline still runs");
  expect(held.poll(true, true, 200 + PRESS_HOLD_MS, 3) == HELD_EXPIRED, "and still ends it");
  const uint8_t newer[PRESS_PAYLOAD_LENGTH] = {6, 0, 0, 0, 0, 0, 0, 0, 0};
  held.hold(press, 200, 3);
  held.poll(true, true, 300, 3);
  held.hold(newer, 310, 3);
  expect(!held.restore() && held.payload[0] == 6, "a newer held press is never overwritten by a retry");
}

int main() {
  testRoute();
  testDeadlineAndLinkFirst();
  testRestoreAfterAFailedNotify();
  testDeliverOnTrust();
  testGiveUp();
  testOneAtATime();
  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  return 0;
}

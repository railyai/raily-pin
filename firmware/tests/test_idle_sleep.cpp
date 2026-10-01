// Host tests for idle_sleep.h (docs/pins/air-power-risks.md).
#include <stdio.h>
#include <stdlib.h>

#include "../RailyPinsP1/idle_sleep.h"

static int failures = 0;

static void expect(bool ok, const char* what) {
  printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) failures++;
}

static void testCaps() {
  expect(IdleWake::start(5000, false).ms == IDLE_BACKSTOP_MS, "nothing to do: the 1 s backstop");
  expect(IdleWake::start(5000, true).ms == IDLE_USB_CAP_MS, "USB mounted: 10 ms, serial stays responsive");
  IdleWake wake = IdleWake::start(5000, false);
  wake.tick(false);
  wake.due(false, 5001);
  wake.pending(false);
  expect(wake.ms == IDLE_BACKSTOP_MS, "reasons that are off change nothing");
}

static void testPendingWins() {
  IdleWake wake = IdleWake::start(5000, false);
  wake.pending(true);
  wake.tick(true);
  wake.due(true, 5900);
  expect(wake.ms == 0, "work raised for loop(): no sleep, whatever comes after");
}

static void testDeadlines() {
  IdleWake wake = IdleWake::start(5000, false);
  wake.due(true, 5300);  // the 300 ms drain before a restart or DFU jump
  expect(wake.ms == 300, "wakes at the DFU drain deadline");
  wake.due(true, 5100);
  expect(wake.ms == 100, "the nearest deadline wins");
  wake.due(true, 9000);
  expect(wake.ms == 100, "a later deadline does not push it back");
  IdleWake late = IdleWake::start(5000, false);
  late.due(true, 4990);
  expect(late.ms == 0, "a deadline already passed: no sleep");
  IdleWake far = IdleWake::start(5000, false);
  far.due(true, 7000);
  expect(far.ms == IDLE_BACKSTOP_MS, "a deadline beyond the backstop: the backstop");
  // millis() wraps after 49.7 days.
  IdleWake wrap = IdleWake::start(0xFFFFFF00u, false);
  wrap.due(true, 0x00000010u);
  expect(wrap.ms == 0x110, "a deadline across the wrap is still ahead");
  IdleWake wrapPassed = IdleWake::start(0x00000010u, false);
  wrapPassed.due(true, 0xFFFFFF00u);
  expect(wrapPassed.ms == 0, "a deadline just before the wrap has passed");
}

static void testTick() {
  IdleWake wake = IdleWake::start(5000, false);
  wake.tick(true);
  expect(wake.ms == IDLE_TICK_MS, "busy outputs: 20 ms steps");
  IdleWake usb = IdleWake::start(5000, true);
  usb.tick(true);
  expect(usb.ms == IDLE_USB_CAP_MS, "the USB cap is below the tick");
}

static void testQuietLights() {
  FeedbackState state = {};
  FeedbackLights lit = state.tick(0, true, 0);
  expect(lit.green && !quietLights(lit).green, "linked and idle: the steady green goes dark");
  expect(!feedbackBusy(state), "linked and idle: nothing to animate");
  FeedbackState away = {};
  bool anyBlue = false;
  for (uint32_t t = 0; t < 2000; t += 100) anyBlue = anyBlue || quietLights(away.tick(t, false, 0)).blue;
  expect(!anyBlue, "not linked: no blue blink");
  expect(!feedbackBusy(away), "not linked: nothing to animate");
  FeedbackState error = {};
  FeedbackLights first = error.tick(0, true, 4);
  expect(quietLights(first).red && !quietLights(first).green, "an error pattern: red flashes, no green");
  expect(feedbackBusy(error), "a pattern keeps loop() ticking");
  bool done = false;
  for (uint32_t t = 0; t < 2000 && !done; t += 20) {
    error.tick(t, true, 0);
    done = !feedbackBusy(error);
  }
  expect(done, "the pattern ends, and with it the ticking");
}

// The states that keep loop() on 20 ms ticks must end on their own.
static void testRhythmEnds() {
  RhythmPlayer player = {};
  player.trigger(rhythmForEventAck(4), 1000);
  expect(rhythmBusy(player), "an error rhythm keeps loop() ticking");
  uint32_t t = 1000;
  while (rhythmBusy(player) && t < 1000 + 5000) {
    t += 20;
    player.tick(t);
  }
  expect(!rhythmBusy(player) && !player.motorOn(), "the error rhythm ends within 5 s, motor off");
  // A flood of acks for 10 s: the budget drops some, the last one plays out.
  RhythmPlayer flood = {};
  for (uint32_t f = 0; f < 10000; f += 100) {
    flood.trigger(rhythmForEventAck(4), 50000 + f);
    flood.tick(50000 + f);
  }
  t = 60000;
  while (rhythmBusy(flood) && t < 60000 + 5000) {
    t += 20;
    flood.tick(t);
  }
  expect(!rhythmBusy(flood), "after a 10 s ack flood the motor is idle within 5 s");
  RhythmPlayer quiet = {};
  quiet.setQuietHours(true, 0);
  quiet.trigger(rhythmForEventAck(4), 10);
  expect(!rhythmBusy(quiet), "quiet hours: nothing plays, nothing to tick for");
}

// Ticks every 20 ms like the sleep build and returns when the scene is off.
static uint32_t screenOffAfter(ScreenState& state, uint32_t from, bool connected, bool owned, uint32_t limit) {
  for (uint32_t t = from; t < from + limit; t += 20) {
    state.tick(t, false, 0, connected, owned);
    if (!screenBusy(state)) return t - from;
  }
  return limit;
}

static void testScreenEnds() {
  const uint32_t bound = ScreenState::FLIGHT_TIMEOUT_MS + ScreenState::IDLE_MS + ScreenState::FADE_MS + 1000;
  ScreenState noAck = {};
  noAck.tick(1000, true, 0, true, true);
  expect(screenBusy(noAck), "a press wakes the screen: loop() ticks");
  expect(screenOffAfter(noAck, 1020, true, true, 60000) < bound, "a press with no answer: dark again within ~18 s");

  ScreenState result = {};
  result.tick(1000, true, 0, true, true);
  result.tick(3000, false, 3, true, true);
  expect(screenOffAfter(result, 3020, true, true, 60000) < ScreenState::IDLE_MS + ScreenState::FADE_MS + 1000,
         "a result: dark again within ~8 s");

  ScreenState unowned = {};
  unowned.tick(1000, true, 0, true, false);
  expect(screenOffAfter(unowned, 1020, true, false, 60000) < bound, "an unowned press: dark again");

  ScreenState noLink = {};
  noLink.tick(1000, true, 0, false, true);
  expect(screenOffAfter(noLink, 1020, false, true, 60000) < bound, "a press with no link: dark again");

  ScreenState demo = {};
  demo.showDemo(1000, OLED_SCENE_SEARCHING, 0, 0);
  expect(screenOffAfter(demo, 1020, true, true, 120000) < ScreenState::DEMO_MS + ScreenState::FADE_MS + 1000,
         "a bench demo: dark again after its 30 s");
}

static void testTap() {
  ButtonDebounce button = {};
  expect(button.sample(true, 1000) == BUTTON_NONE, "a press edge alone emits nothing");
  expect(button.unsettled() && button.settleAtMs() == 1030, "loop() looks again when the press settles");
  expect(button.sample(true, 1030) == BUTTON_NONE, "settled press: still nothing (a tap is emitted on release)");
  expect(button.sample(false, 1200) == BUTTON_NONE, "the release edge alone emits nothing");
  expect(button.sample(false, 1229) == BUTTON_NONE, "not settled 1 ms early");
  expect(button.sample(false, 1230) == BUTTON_TAP, "a 200 ms press is one tap, on its settled release");
  expect(button.sample(false, 5000) == BUTTON_NONE, "and only one");
}

static void testBounce() {
  ButtonDebounce button = {};
  // A contact that chatters for 8 ms on the way down and on the way up.
  const uint32_t edges[][2] = {{1000, 1}, {1002, 0}, {1003, 1}, {1006, 0}, {1008, 1},
                               {1150, 0}, {1151, 1}, {1153, 0}, {1157, 1}, {1158, 0}};
  int taps = 0;
  for (size_t i = 0; i < sizeof(edges) / sizeof(edges[0]); i++) {
    taps += button.sample(edges[i][1] != 0, edges[i][0]) == BUTTON_TAP;
  }
  for (uint32_t t = 1158; t < 1400; t += 5) taps += button.sample(false, t) == BUTTON_TAP;
  expect(taps == 1, "chatter on both edges: exactly one tap");
}

static void testGlitch() {
  ButtonDebounce button = {};
  int events = 0;
  events += button.sample(true, 1000) != BUTTON_NONE;
  events += button.sample(false, 1010) != BUTTON_NONE;  // a 10 ms spike (ESD, a knock)
  for (uint32_t t = 1010; t < 2000; t += 10) events += button.sample(false, t) != BUTTON_NONE;
  expect(events == 0 && !button.pressed, "a spike shorter than 30 ms is no press");
}

static void testHold() {
  ButtonDebounce button = {};
  button.sample(true, 1000);
  button.sample(false, 1000 + BUTTON_TAP_MAX_MS);
  const ButtonEvent slow = button.sample(false, 1000 + BUTTON_TAP_MAX_MS + BUTTON_STABLE_MS);
  expect(slow == BUTTON_HOLD && buttonScans(slow), "a 0.4 s press is a hold, and a hold scans");
  ButtonDebounce justUnder = {};
  justUnder.sample(true, 1000);
  justUnder.sample(false, 1000 + BUTTON_TAP_MAX_MS - 1);
  expect(justUnder.sample(false, 2000) == BUTTON_TAP, "399 ms is still a tap");
  ButtonDebounce under2s = {};
  under2s.sample(true, 1000);
  under2s.sample(false, 1000 + BUTTON_LONG_MS - 1);
  const ButtonEvent edge = under2s.sample(false, 5000);
  expect(edge == BUTTON_HOLD && buttonScans(edge), "1999 ms still scans");
  ButtonDebounce at2s = {};
  at2s.sample(true, 1000);
  at2s.sample(false, 1000 + BUTTON_LONG_MS);
  const ButtonEvent longPress = at2s.sample(false, 5000);
  expect(longPress == BUTTON_LONG && !buttonScans(longPress), "2 s or more never scans");
  expect(buttonScans(BUTTON_TAP) && !buttonScans(BUTTON_NONE), "a tap scans, nothing does not");
}

static void testStuckButton() {
  // A button squeezed in a pocket for 10 minutes: nothing while it is held,
  // one long press (no scan) when it lets go.
  ButtonDebounce button = {};
  int during = 0;
  button.sample(true, 1000);
  for (uint32_t t = 2000; t < 601000; t += 1000) during += button.sample(true, t) != BUTTON_NONE;
  button.sample(false, 601000);
  const ButtonEvent end = button.sample(false, 601100);
  expect(during == 0 && end == BUTTON_LONG && !buttonScans(end), "a 10-minute squeeze: no scan, ever");
  ButtonDebounce wrap = {};
  wrap.sample(true, 0xFFFFF000u);
  wrap.sample(false, 0x00000800u);
  expect(wrap.sample(false, 0x00001000u) == BUTTON_LONG, "a long press across the millis wrap");
}

static void testSlowPressReadLate() {
  // A 1.5 s press with a bouncing release, read by loop() long afterwards:
  // one hold, timed edge to edge, and it scans.
  ButtonDebounce button = {};
  ButtonEdgeRing ring = {};
  ring.push(true, 1000);
  ring.push(false, 2500);
  ring.push(true, 2502);
  ring.push(false, 2505);
  bool level = false;
  uint32_t at = 0;
  int holds = 0;
  int others = 0;
  while (ring.pop(&level, &at)) {
    const ButtonEvent e = button.sample(level, at);
    holds += e == BUTTON_HOLD;
    others += e != BUTTON_NONE && e != BUTTON_HOLD;
  }
  const ButtonEvent late = button.sample(false, 5000);
  holds += late == BUTTON_HOLD;
  others += late != BUTTON_NONE && late != BUTTON_HOLD;
  expect(holds == 1 && others == 0, "a 1.5 s press with release bounce, read at 5 s: one hold");
}

static void testLateLoop() {
  // loop() was busy (a flash write, a seal): it reads the ISR's edges late,
  // all at once, and the tap is still timed from the edges themselves.
  ButtonDebounce button = {};
  ButtonEdgeRing ring = {};
  ring.push(true, 1000);
  ring.push(false, 1120);
  bool level = false;
  uint32_t at = 0;
  int taps = 0;
  while (ring.pop(&level, &at)) taps += button.sample(level, at) == BUTTON_TAP;
  taps += button.sample(false, 1900) == BUTTON_TAP;  // loop()'s own read, much later
  expect(taps == 1, "edges read 780 ms late: one tap");
  ButtonDebounce slow = {};
  slow.sample(true, 1000);
  slow.sample(false, 1120);
  expect(slow.sample(false, 1900) == BUTTON_TAP, "the tap's length is edge to edge, not to loop()'s read");
}

static void testOutOfOrder() {
  // loop() read the pin (pressed) at 1001, then pops the ISR's edge from 1000.
  ButtonDebounce button = {};
  button.sample(true, 1001);
  expect(button.sample(true, 1000) == BUTTON_NONE && button.unsettled(), "an older edge cannot settle a press early");
  expect(button.sample(true, 1030) == BUTTON_NONE && button.unsettled(), "nor make 29 ms count as 30");
  // An older edge after a later read must not skip the debounce either.
  ButtonDebounce late = {};
  late.sample(true, 0x00000100u);
  expect(late.sample(true, 0xFFFFFF00u) == BUTTON_NONE && late.unsettled(),
         "an edge from before the last read, across the wrap, is taken at the read's time");
}

static void testWrap() {
  ButtonDebounce button = {};
  button.sample(true, 0xFFFFFFF0u);
  button.sample(false, 0x00000050u);
  expect(button.sample(false, 0x00000100u) == BUTTON_TAP, "a tap across the millis wrap");
}

static void testRing() {
  ButtonEdgeRing ring = {};
  expect(ring.empty(), "a new ring is empty");
  int pushed = 0;
  for (int i = 0; i < 20; i++) pushed += ring.push(i % 2 == 0, 100 + i);
  expect(pushed == BUTTON_RING_SIZE - 1 && ring.dropped == 20 - (BUTTON_RING_SIZE - 1),
         "a full ring keeps 15 edges and counts the rest");
  bool level = false;
  uint32_t at = 0;
  expect(ring.pop(&level, &at) && at == 100 && level, "edges come out oldest first");
  int left = 0;
  while (ring.pop(&level, &at)) left++;
  expect(left == BUTTON_RING_SIZE - 2 && ring.empty(), "and all of them");
  expect(ring.push(true, 500) && ring.pop(&level, &at) && at == 500, "the ring keeps working after a wrap of its index");
}

int main() {
  testCaps();
  testPendingWins();
  testDeadlines();
  testTick();
  testQuietLights();
  testRhythmEnds();
  testScreenEnds();
  testTap();
  testBounce();
  testGlitch();
  testHold();
  testStuckButton();
  testSlowPressReadLate();
  testLateLoop();
  testOutOfOrder();
  testWrap();
  testRing();
  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  return 0;
}

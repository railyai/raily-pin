// Host tests for the agent's counts on the keyring screen (screen_state.h,
// spec docs/pins/keyring-oled.md §9 K3-K5, §10, §15; docs/pins/firmware.md
// «Counts»): only a trusted write carries them, a self-wake shows the icon
// without a digit, the person's own wake shows the digit, withheld shows
// none, and the order against the agent's own state.
#include <stdio.h>
#include "../RailyPinsP1/screen_state.h"

static int failures = 0;
static void expect(bool condition, const char* name) {
  printf("%s %s\n", condition ? "PASS" : "FAIL", name);
  if (!condition) failures++;
}

static const uint8_t WITHHELD = SCREEN_COUNT_WITHHELD;

// A payload as the pin's parser hands it out: stripped when not encrypted.
static ScreenPayload payload(uint8_t agent, uint8_t found, uint8_t looking, uint8_t flags = 0,
                             bool encrypted = true) {
  const uint8_t bytes[SCREEN_PAYLOAD_LENGTH] = {1, agent, found, looking, 0, 0, flags, 0, SCREEN_LOCALE_RU};
  ScreenPayload p = {};
  if (parseScreenPayload(bytes, sizeof(bytes), encrypted, &p) != ScreenPayloadError::ok) {
    printf("FAIL test payload rejected\n");
    failures++;
  }
  return p;
}

static ScreenFrame written(ScreenState& s, uint32_t now, const ScreenPayload& p, bool trusted) {
  s.onWrite(now, p, trusted);
  return s.tick(now, false, 0, true, true);
}

static ScreenFrame at(ScreenState& s, uint32_t now) { return s.tick(now, false, 0, true, true); }

static ScreenFrame wokenAt(ScreenState& s, uint32_t now) {
  s.wake(now);
  return at(s, now);
}

static void trustOnly() {
  ScreenState s = {};
  written(s, 0, payload(SCREEN_AGENT_WATCH, 3, 5, 0, false), false);
  ScreenFrame f = wokenAt(s, 10);
  expect(f.scene == OLED_SCENE_WATCH && f.count == 0, "a plaintext write carries no counts: the wake shows watch");
  ScreenState t = {};
  written(t, 0, payload(SCREEN_AGENT_WATCH, 3, 0), true);
  f = wokenAt(t, 10);
  expect(f.scene == OLED_SCENE_FOUND && f.count == 3, "a trusted found shows K3 with its digit on the person's wake");
}

static void selfWakeHidesDigits() {
  ScreenState s = {};
  ScreenFrame f = written(s, 1000, payload(SCREEN_AGENT_WATCH, 3, 0, SCREEN_FLAG_WAKE_NOW), true);
  expect(f.scene == OLED_SCENE_FOUND_SELF && f.count == 0, "a self-wake shows «есть люди» as the icon, no digit");
  s.wake(1100);
  f = at(s, 1100);
  expect(f.scene == OLED_SCENE_FOUND_SELF && f.count == 0, "a lift while it is awake reveals nothing");
  expect(at(s, 8000).scene == OLED_SCENE_OFF, "it sleeps after 6 s and the fade");
  f = wokenAt(s, 9000);
  expect(f.scene == OLED_SCENE_FOUND && f.count == 3, "the person's next wake shows the digit");

  ScreenState t = {};
  written(t, 0, payload(SCREEN_AGENT_WATCH, 0, 7, SCREEN_FLAG_WAKE_NOW), true);
  f = at(t, 0);
  expect(f.scene == OLED_SCENE_LOOKING && f.count == 0, "a self-woken «смотрят» has no number");

  // A self-wake over a screen the person woke keeps the digits: they are
  // already looking.
  ScreenState u = {};
  written(u, 0, payload(SCREEN_AGENT_WATCH, 2, 0), true);
  wokenAt(u, 100);
  f = written(u, 200, payload(SCREEN_AGENT_WATCH, 4, 0, SCREEN_FLAG_WAKE_NOW), true);
  expect(f.scene == OLED_SCENE_FOUND && f.count == 4, "a wake bit over an asked screen keeps the digit");

  // Over a press result it does not: that screen was not an agent scene.
  ScreenState v = {};
  written(v, 0, payload(SCREEN_AGENT_WATCH, 0, 0), true);
  wokenAt(v, 100);
  v.tick(200, true, 0, true, true);  // a press
  v.tick(300, false, 3, true, true);  // its result
  f = written(v, 400, payload(SCREEN_AGENT_WATCH, 6, 0, SCREEN_FLAG_WAKE_NOW), true);
  expect(f.scene == OLED_SCENE_FOUND_SELF && f.count == 0, "a wake bit over a press result is a self-wake");
}

static void withheldAndRange() {
  ScreenState s = {};
  written(s, 0, payload(SCREEN_AGENT_WATCH, WITHHELD, 0), true);
  ScreenFrame f = wokenAt(s, 10);
  expect(f.scene == OLED_SCENE_FOUND_SELF && f.count == 0, "found withheld shows the icon, never a number");
  ScreenState t = {};
  written(t, 0, payload(SCREEN_AGENT_WATCH, 0, WITHHELD), true);
  f = wokenAt(t, 10);
  expect(f.scene == OLED_SCENE_LOOKING && f.count == 0, "looking withheld shows «смотрят» with no number");
  ScreenState u = {};
  written(u, 0, payload(SCREEN_AGENT_WATCH, 99, 0), true);
  expect(wokenAt(u, 10).count == 99, "99 shows as 99");
  ScreenState w = {};
  written(w, 0, payload(SCREEN_AGENT_WATCH, 0, 12), true);
  f = wokenAt(w, 10);
  expect(f.scene == OLED_SCENE_LOOKING && f.count == 12, "looking 12 shows «+12» on the person's wake");
}

static void order() {
  ScreenState s = {};
  written(s, 0, payload(SCREEN_AGENT_WATCH, 2, 9), true);
  expect(wokenAt(s, 10).scene == OLED_SCENE_FOUND, "found comes before looking");
  ScreenState t = {};
  written(t, 0, payload(SCREEN_AGENT_PAUSED, 2, 9), true);
  expect(wokenAt(t, 10).scene == OLED_SCENE_PAUSED, "a paused agent says so before any count");
  ScreenState u = {};
  written(u, 0, payload(SCREEN_AGENT_SET_UP, 2, 9), true);
  expect(wokenAt(u, 10).scene == OLED_SCENE_NEEDS_SETUP, "set-up needed comes before any count");
  ScreenState v = {};
  written(v, 0, payload(SCREEN_AGENT_WATCH, 2, 9, SCREEN_FLAG_AGENT_SPEAKING), true);
  expect(at(v, 0).scene == OLED_SCENE_SPEAKING, "speaking comes before any count");
  ScreenState w = {};
  written(w, 0, payload(SCREEN_AGENT_SCANNING, 0, 4), true);
  expect(wokenAt(w, 10).scene == OLED_SCENE_LOOKING, "counts come before the scanning radar");
  ScreenState x = {};
  written(x, 0, payload(SCREEN_AGENT_UNKNOWN, 1, 0), true);
  expect(wokenAt(x, 10).scene == OLED_SCENE_FOUND, "and before «nothing known» of an unknown agent");
  ScreenState y = {};
  written(y, 0, payload(SCREEN_AGENT_WATCH, 0, 0), true);
  ScreenFrame f = wokenAt(y, 10);
  expect(f.scene == OLED_SCENE_WATCH && f.count == 0, "no counts: on watch, never «0 никого»");
}

static void liveCounts() {
  ScreenState s = {};
  written(s, 0, payload(SCREEN_AGENT_WATCH, 2, 0), true);
  ScreenFrame before = wokenAt(s, 10);
  ScreenFrame after = written(s, 1000, payload(SCREEN_AGENT_WATCH, 5, 0), true);
  expect(after.scene == OLED_SCENE_FOUND && after.count == 5, "a new count shows on the awake screen");
  ScreenFrame other = after;
  other.count = 6;
  expect(before.scene == after.scene && !after.sameImage(other),
         "a count change alone is a new image (the pin redraws it)");
  after = written(s, 2000, payload(SCREEN_AGENT_WATCH, 0, 0), true);
  expect(after.scene == OLED_SCENE_WATCH, "seen on the phone: back to watch");
  expect(at(s, 7999).scene != OLED_SCENE_OFF, "a trusted change restarts the 6 s idle timer");
}

static void staleClears() {
  ScreenState s = {};
  written(s, 0, payload(SCREEN_AGENT_WATCH, 4, 6), true);
  at(s, ScreenState::STALE_MS);
  ScreenFrame f = wokenAt(s, ScreenState::STALE_MS + 10);
  expect(f.scene == OLED_SCENE_NOTHING_KNOWN && f.count == 0, "30 min without a write: no stale count lingers");
}

static void pressResult() {
  ScreenState s = {};
  written(s, 0, payload(SCREEN_AGENT_WATCH, 3, 0), true);
  s.tick(100, true, 0, true, true);
  ScreenFrame f = s.tick(300, false, 3, true, true);
  expect(f.scene == OLED_SCENE_RESULT && f.count == 0,
         "a press result stays «в телефоне»: found is the agent's count, not the press's");
}

static void unowned() {
  ScreenState s = {};
  written(s, 0, payload(SCREEN_AGENT_WATCH, 3, 5), true);
  s.tick(10, false, 0, true, false);
  s.wake(20);
  ScreenFrame f = s.tick(20, false, 0, true, false);
  expect(f.scene == OLED_SCENE_SET_ME_UP && f.count == 0, "an unowned pin forgets the counts: «set me up»");
}

static void demo() {
  ScreenState s = {};
  s.showDemo(0, OLED_SCENE_FOUND, OLED_SHAPE_PEBBLE, OLED_MATERIAL_SATIN, 1);
  expect(at(s, 0).count == ScreenState::DEMO_FOUND, "the bench demo of «есть люди» draws the pack's 3");
  s.showDemo(0, OLED_SCENE_LOOKING, OLED_SHAPE_PEBBLE, OLED_MATERIAL_SATIN, 1);
  expect(at(s, 0).count == ScreenState::DEMO_LOOKING, "and «смотрят» the pack's +5");
  s.showDemo(0, OLED_SCENE_WATCH, OLED_SHAPE_PEBBLE, OLED_MATERIAL_SATIN, 1);
  expect(at(s, 0).count == 0, "other scenes draw no number");
}

int main() {
  trustOnly();
  selfWakeHidesDigits();
  withheldAndRange();
  order();
  liveCounts();
  staleClears();
  pressResult();
  unowned();
  demo();
  if (failures) {
    printf("%d count test(s) failed\n", failures);
    return 1;
  }
  printf("all count tests passed\n");
  return 0;
}

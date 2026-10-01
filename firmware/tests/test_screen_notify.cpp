// Host tests for notifications on the keyring screen (screen_state.h, spec
// docs/pins/keyring-oled.md §9 K6-K7, §10 «Notification mapping», §12;
// docs/pins/firmware.md «Notifications»): only a trusted write carries
// them, each kind has its scene, the order against speaking, paused and the
// counts, the blinking status-row dot until seen, never a count on K6.
#include <stdio.h>
#include "../RailyPinsP1/screen_state.h"

static int failures = 0;
static void expect(bool condition, const char* name) {
  printf("%s %s\n", condition ? "PASS" : "FAIL", name);
  if (!condition) failures++;
}

static ScreenPayload payload(uint8_t agent, uint8_t notify, uint8_t count, uint8_t flags = 0, uint8_t found = 0,
                             bool encrypted = true) {
  const uint8_t bytes[SCREEN_PAYLOAD_LENGTH] = {1, agent, found, 0, notify, count, flags, 0, SCREEN_LOCALE_RU};
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

static void kinds() {
  const uint8_t scenes[8] = {OLED_SCENE_WATCH,  OLED_SCENE_FOUND_SELF, OLED_SCENE_REQUEST, OLED_SCENE_ACCEPTED,
                             OLED_SCENE_REPORT, OLED_SCENE_TALKS,      OLED_SCENE_DOOR,    OLED_SCENE_QUESTION};
  bool all = true;
  for (uint8_t kind = 0; kind <= SCREEN_NOTIFY_MAX; kind++) {
    ScreenState s = {};
    ScreenFrame f = written(s, 0, payload(SCREEN_AGENT_WATCH, kind, kind ? 2 : 0, SCREEN_FLAG_WAKE_NOW), true);
    if (f.scene != scenes[kind] || f.count != 0) {
      printf("  notify %u showed scene %u count %u\n", kind, f.scene, f.count);
      all = false;
    }
  }
  expect(all, "each notify kind wakes its own scene, never with a count (a new finding is the a02 icon)");
}

static void trustOnly() {
  ScreenState s = {};
  written(s, 0, payload(SCREEN_AGENT_WATCH, 2, 1, SCREEN_FLAG_WAKE_NOW, 0, false), false);
  s.wake(10);
  ScreenFrame f = at(s, 10);
  expect(f.scene == OLED_SCENE_WATCH && !f.dot, "a plaintext write carries no notification: no scene, no dot");
}

static void order() {
  ScreenState s = {};
  written(s, 0, payload(SCREEN_AGENT_PAUSED, 2, 1, 0, 5), true);
  s.wake(10);
  expect(at(s, 10).scene == OLED_SCENE_REQUEST, "a notification comes before paused and before the counts");
  ScreenState t = {};
  written(t, 0, payload(SCREEN_AGENT_WATCH, 2, 1, SCREEN_FLAG_AGENT_SPEAKING), true);
  expect(at(t, 0).scene == OLED_SCENE_SPEAKING, "speaking comes before a notification");
  ScreenState u = {};
  written(u, 0, payload(SCREEN_AGENT_WATCH, 1, 1, 0, 7), true);
  u.wake(10);
  ScreenFrame f = at(u, 10);
  expect(f.scene == OLED_SCENE_FOUND_SELF && f.count == 0,
         "a new-finding notification shows the icon even on the person's wake");
  written(u, 20, payload(SCREEN_AGENT_WATCH, 0, 0, 0, 7), true);
  f = at(u, 20);
  expect(f.scene == OLED_SCENE_FOUND && f.count == 7, "seen on the phone: the person's wake shows the count again");
}

static void dot() {
  ScreenState s = {};
  written(s, 0, payload(SCREEN_AGENT_WATCH, 3, 1), true);  // no wake bit: nothing wakes
  expect(at(s, 0).scene == OLED_SCENE_OFF, "a notification without the wake bit does not wake the screen");
  s.tick(1000, true, 0, true, true);  // the person presses
  ScreenFrame f = at(s, 1000);
  expect(f.scene == OLED_SCENE_PRESSED && f.dot, "the dot shows on a press scene while a notification is unseen");
  f = at(s, 1200);
  expect(f.scene == OLED_SCENE_SEARCHING && f.dot, "it starts lit with each new scene");
  expect(!at(s, 1200 + ScreenState::DOT_ON_MS).dot, "it goes dark after 1 s");
  expect(at(s, 1200 + ScreenState::DOT_PERIOD_MS).dot, "and lights again 4 s after the scene started");
  ScreenFrame lit = at(s, 1200 + ScreenState::DOT_PERIOD_MS);
  ScreenFrame dark = lit;
  dark.dot = false;
  expect(!lit.sameImage(dark), "the dot alone is a new image (the pin redraws it)");
  written(s, 1300 + ScreenState::DOT_PERIOD_MS, payload(SCREEN_AGENT_WATCH, 0, 0), true);
  expect(!at(s, 1300 + 2 * ScreenState::DOT_PERIOD_MS).dot, "seen (notify 0): the dot is gone");
}

static void staleAndDemo() {
  ScreenState s = {};
  written(s, 0, payload(SCREEN_AGENT_WATCH, 2, 3), true);
  at(s, ScreenState::STALE_MS);
  s.wake(ScreenState::STALE_MS + 10);
  ScreenFrame f = at(s, ScreenState::STALE_MS + 10);
  expect(f.scene == OLED_SCENE_NOTHING_KNOWN && !f.dot, "30 min without a write: the notification and its dot go");
  ScreenState t = {};
  written(t, 0, payload(SCREEN_AGENT_WATCH, 2, 3), true);
  t.showDemo(10, OLED_SCENE_WATCH, OLED_SHAPE_PEBBLE, OLED_MATERIAL_SATIN, 1);
  expect(!at(t, 10).dot, "the bench demo draws no dot");
}

static void quietHours() {
  ScreenState s = {};
  ScreenFrame f =
      written(s, 0, payload(SCREEN_AGENT_WATCH, 2, 1, SCREEN_FLAG_WAKE_NOW | SCREEN_FLAG_QUIET_HOURS), true);
  expect(f.scene == OLED_SCENE_OFF, "quiet hours hold a notification's wake");
}

int main() {
  kinds();
  trustOnly();
  order();
  dot();
  staleAndDemo();
  quietHours();
  if (failures) {
    printf("%d notify test(s) failed\n", failures);
    return 1;
  }
  printf("all notify tests passed\n");
  return 0;
}

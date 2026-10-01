// Host tests for the screen_state layer of screen_state.h (spec
// docs/pins/keyring-oled.md §10, §13, §15): agent scenes, wakes and their
// limits, speaking, the stale reset, modest, disabled, owned-only and the
// mascot. The press timeline is test_screen_state.cpp.
#include <stdio.h>
#include "../RailyPinsP1/screen_state.h"

static int failures = 0;
static void expect(bool condition, const char* name) {
  printf("%s %s\n", condition ? "PASS" : "FAIL", name);
  if (!condition) failures++;
}

static const uint8_t CUBE_GLASS = 0x12;
static const uint8_t FOLD_JELLY = 0x51;

// A payload as the pin's parser hands it out: stripped when not encrypted.
static ScreenPayload payload(uint8_t agent, uint8_t flags = 0, uint8_t mascot = 0, uint8_t locale = 0,
                             bool encrypted = false) {
  const uint8_t bytes[SCREEN_PAYLOAD_LENGTH] = {1, agent, 0, 0, 0, 0, flags, mascot, locale};
  ScreenPayload p = {};
  if (parseScreenPayload(bytes, sizeof(bytes), encrypted, &p) != ScreenPayloadError::ok) {
    printf("FAIL test payload rejected\n");
    failures++;
  }
  return p;
}

// One loop pass with an accepted write, the way updateScreen() calls it.
static ScreenFrame written(ScreenState& s, uint32_t now, const ScreenPayload& p, bool trusted,
                           bool connected = true) {
  s.onWrite(now, p, trusted);
  return s.tick(now, false, 0, connected, true);
}

static ScreenFrame at(ScreenState& s, uint32_t now, bool connected = true, bool owned = true) {
  return s.tick(now, false, 0, connected, owned);
}

static void agentScenes() {
  ScreenState s = {};
  at(s, 0);
  s.wake(0);
  ScreenFrame f = at(s, 0);
  expect(f.scene == OLED_SCENE_NOTHING_KNOWN && f.locale == 0, "from boot a wake knows nothing: the mascot alone");
  f = written(s, 100, payload(SCREEN_AGENT_WATCH, 0, CUBE_GLASS, SCREEN_LOCALE_RU), false);
  expect(f.scene == OLED_SCENE_WATCH, "an awake agent scene follows a plaintext write");
  expect(f.shape == OLED_SHAPE_CUBE && f.material == OLED_MATERIAL_GLASS && f.locale == SCREEN_LOCALE_RU,
         "with the mascot and the words it carries");
  at(s, 6999);
  expect(at(s, 7000).scene == OLED_SCENE_OFF, "but a plaintext write never restarts the 6 s idle timer");

  ScreenState t = {};
  f = written(t, 0, payload(SCREEN_AGENT_PAUSED, SCREEN_FLAG_WAKE_NOW, FOLD_JELLY, SCREEN_LOCALE_EN), false);
  expect(f.scene == OLED_SCENE_OFF, "a plaintext write never wakes (its wake bit is stripped)");
  t.wake(10);
  f = at(t, 10);
  expect(f.scene == OLED_SCENE_PAUSED && f.shape == OLED_SHAPE_FOLD && f.material == OLED_MATERIAL_JELLY &&
             f.locale == SCREEN_LOCALE_EN,
         "it only changes what the next wake shows");
  written(t, 20, payload(SCREEN_AGENT_SCANNING), false);
  expect(at(t, 20).scene == OLED_SCENE_SEARCHING, "agent scanning shows the radar (K2)");
  written(t, 30, payload(SCREEN_AGENT_SET_UP), false);
  expect(at(t, 30).scene == OLED_SCENE_NEEDS_SETUP, "set-up needed shows K9 with the owner's mascot");
  written(t, 40, payload(SCREEN_AGENT_STALE), false);
  expect(at(t, 40).scene == OLED_SCENE_NOTHING_KNOWN, "agent 5 (stale) is «nothing known»");
  written(t, 50, payload(SCREEN_AGENT_UNKNOWN), false);
  expect(at(t, 50).scene == OLED_SCENE_NOTHING_KNOWN, "agent 0 (unknown) too");
  written(t, 60, payload(SCREEN_AGENT_WATCH, 0, 0, SCREEN_LOCALE_AR), false);
  expect(at(t, 60).locale == 0, "Arabic draws icons only");
}

static void trustedWakes() {
  ScreenState s = {};
  const ScreenPayload wakeWatch = payload(SCREEN_AGENT_WATCH, SCREEN_FLAG_WAKE_NOW, 0, 0, true);
  ScreenFrame f = written(s, 1000, wakeWatch, true);
  expect(f.scene == OLED_SCENE_WATCH && f.contrast == ScreenState::CONTRAST_NORMAL, "a trusted wake bit wakes on watch");
  expect(at(s, 8000).scene == OLED_SCENE_OFF, "for 6 s and the fade");
  expect(written(s, 60999, wakeWatch, true).scene == OLED_SCENE_OFF, "a second wake inside the minute is not honoured");
  expect(written(s, 61000, wakeWatch, true).scene == OLED_SCENE_WATCH, "a minute after the last one it is");

  ScreenState q = {};
  f = written(q, 0, payload(SCREEN_AGENT_WATCH, SCREEN_FLAG_WAKE_NOW | SCREEN_FLAG_QUIET_HOURS, 0, 0, true), true);
  expect(f.scene == OLED_SCENE_OFF && q.quietHours(), "quiet hours win over the wake bit");
  ScreenState r = {};
  written(r, 0, payload(SCREEN_AGENT_WATCH, SCREEN_FLAG_QUIET_HOURS), false);
  expect(!r.quietHours(), "quiet hours never come over plaintext (stripped)");
  ScreenState u = {};
  const ScreenPayload untrustedBytes = payload(SCREEN_AGENT_WATCH, SCREEN_FLAG_WAKE_NOW, 0, 0, true);
  expect(written(u, 0, untrustedBytes, false).scene == OLED_SCENE_OFF,
         "only a trusted write's wake bit counts, whatever it carries");

  // Idle: a trusted change restarts it, a repeat does not.
  ScreenState i = {};
  written(i, 0, wakeWatch, true);
  f = written(i, 5000, payload(SCREEN_AGENT_PAUSED, 0, 0, 0, true), true);
  expect(f.scene == OLED_SCENE_PAUSED, "a trusted change repaints the awake screen");
  f = at(i, 10999);
  expect(f.scene == OLED_SCENE_PAUSED && f.contrast == ScreenState::CONTRAST_NORMAL, "and restarts the idle timer");
  written(i, 10999, payload(SCREEN_AGENT_PAUSED, 0, 0, 0, true), true);
  f = at(i, 11500);
  expect(f.scene == OLED_SCENE_PAUSED && f.contrast < ScreenState::CONTRAST_NORMAL, "a repeat of the same payload does not");
  expect(at(i, 12000).scene == OLED_SCENE_OFF, "and the screen sleeps on time");
}

static void speaking() {
  ScreenState s = {};
  written(s, 0, payload(SCREEN_AGENT_WATCH), false);
  ScreenFrame f = written(s, 1000, payload(SCREEN_AGENT_WATCH, SCREEN_FLAG_AGENT_SPEAKING, 0, SCREEN_LOCALE_ES), false);
  expect(f.scene == OLED_SCENE_SPEAKING && f.locale == SCREEN_LOCALE_ES, "a plaintext speaking edge wakes on «speaking»");
  ScreenFrame a = at(s, 1119);
  ScreenFrame b = at(s, 1120);
  expect(a.frame == 0 && b.frame == 1, "the mouth moves every 120 ms");
  f = at(s, 30000);
  expect(f.scene == OLED_SCENE_SPEAKING && f.contrast == ScreenState::CONTRAST_NORMAL, "speaking holds past the 6 s idle");
  f = written(s, 40000, payload(SCREEN_AGENT_WATCH), false);
  expect(f.scene == OLED_SCENE_WATCH, "speaking stops: the agent scene shows");
  expect(at(s, 46999).scene == OLED_SCENE_WATCH, "for the idle and fade after the hold");
  expect(at(s, 47000).scene == OLED_SCENE_OFF, "then sleeps (the hold + 7 s)");
  f = written(s, 50000, payload(SCREEN_AGENT_WATCH, SCREEN_FLAG_AGENT_SPEAKING), false);
  expect(f.scene == OLED_SCENE_OFF, "a new edge inside the minute does not wake");
  s.wake(50010);
  expect(at(s, 50010).scene == OLED_SCENE_SPEAKING, "but the next wake shows «speaking»");

  ScreenState c = {};
  written(c, 0, payload(SCREEN_AGENT_PAUSED, SCREEN_FLAG_AGENT_SPEAKING | SCREEN_FLAG_QUIET_HOURS, 0, 0, true), true);
  expect(at(c, 0).scene == OLED_SCENE_SPEAKING, "quiet hours do not gate the speaking wake");
  expect(at(c, 59999).scene == OLED_SCENE_SPEAKING, "a hold lasts up to 60 s");
  at(c, 60000);  // loop() ticks every pass: the hold's end is seen when it happens
  f = at(c, 66999);
  expect(f.scene == OLED_SCENE_SPEAKING && f.contrast < ScreenState::CONTRAST_NORMAL, "then idle and fade");
  expect(at(c, 67000).scene == OLED_SCENE_OFF, "and off at 67 s, while the flag is still set");
  at(c, 70000, false);
  c.wake(70000);
  f = at(c, 70000, false);
  expect(f.scene == OLED_SCENE_PAUSED && !f.linked, "a lost link ends «speaking» on the pin");

  // The hourly budget, end to end: 5 wakes of 60 s fit (5 x 67 s > 5 min), the 5th is cut.
  ScreenState h = {};
  uint32_t t = 0;
  int woke = 0;
  for (int k = 0; k < 8; k++, t += 120000) {
    written(h, t, payload(SCREEN_AGENT_WATCH), false);
    if (written(h, t + 1, payload(SCREEN_AGENT_WATCH, SCREEN_FLAG_AGENT_SPEAKING), false).scene == OLED_SCENE_SPEAKING) woke++;
    at(h, t + 110000);
  }
  expect(woke == 5, "speaking wakes stop at the 5 min rolling-hour budget");
}

static void staleReset() {
  ScreenState s = {};
  written(s, 1000, payload(SCREEN_AGENT_PAUSED, SCREEN_FLAG_MODEST | SCREEN_FLAG_AGENT_SPEAKING, CUBE_GLASS, 1), false);
  at(s, 1000 + 70000);  // the speaking wake is over
  at(s, 1000 + ScreenState::STALE_MS - 1);
  expect(s.modest(), "a plaintext modest holds for 30 min");
  s.wake(1000 + ScreenState::STALE_MS - 1);
  ScreenFrame f = at(s, 1000 + ScreenState::STALE_MS - 1);
  expect(f.scene == OLED_SCENE_SPEAKING && f.modest, "(the next wake still shows it)");
  f = at(s, 1000 + ScreenState::STALE_MS);
  expect(f.scene == OLED_SCENE_NOTHING_KNOWN && !f.modest,
         "30 min without a write: «nothing known», speaking and plaintext modest lapse");
  expect(f.shape == OLED_SHAPE_CUBE && f.material == OLED_MATERIAL_GLASS && f.locale == SCREEN_LOCALE_RU,
         "the mascot and the locale stay");

  ScreenState t = {};
  const uint8_t all = SCREEN_FLAG_MODEST | SCREEN_FLAG_DISABLED | SCREEN_FLAG_QUIET_HOURS | SCREEN_FLAG_ALWAYS_ON_USB;
  const uint32_t t0 = UINT32_MAX - 1000;
  written(t, t0, payload(SCREEN_AGENT_WATCH, all, 0, 0, true), true);
  at(t, t0 + ScreenState::STALE_MS - 1);
  expect(t.quietHours(), "trusted quiet hours hold until the stale reset (across the millis wrap)");
  at(t, t0 + ScreenState::STALE_MS);
  expect(!t.quietHours() && t.modest() && t.disabled(), "the stale reset clears quiet hours; trusted modest/disabled stay");
  written(t, t0 + ScreenState::STALE_MS + 5, payload(SCREEN_AGENT_WATCH), false);
  expect(!t.modest() && !t.disabled(), "until the next write says otherwise");

  ScreenState u = {};
  written(u, 0, payload(SCREEN_AGENT_WATCH), false);
  written(u, ScreenState::STALE_MS - 1, payload(SCREEN_AGENT_WATCH), false);
  at(u, ScreenState::STALE_MS + 10);
  u.wake(ScreenState::STALE_MS + 10);
  expect(at(u, ScreenState::STALE_MS + 10).scene == OLED_SCENE_WATCH,
         "every accepted write (the heartbeat) restarts the stale timer");
}

static void modestAndDisabled() {
  ScreenState s = {};
  written(s, 0, payload(SCREEN_AGENT_WATCH, SCREEN_FLAG_MODEST, FOLD_JELLY, 1), false);
  ScreenFrame f = s.tick(10, true, 0, true, true);
  expect(f.scene == OLED_SCENE_PRESSED && f.modest && f.shape == OLED_SHAPE_FOLD, "modest applies to the press too");
  written(s, 20, payload(SCREEN_AGENT_WATCH, SCREEN_FLAG_DISABLED), false);
  f = at(s, 30);
  expect(f.scene == OLED_SCENE_OFF && f.contrast == 0, "disabled: the panel is off, even with a press in flight");
  f = s.tick(40, true, 0, true, true);
  expect(f.scene == OLED_SCENE_OFF, "and a new press does not light it");
  s.showDemo(50, OLED_SCENE_WATCH, 0, 0, 2, true);
  f = at(s, 50);
  expect(f.scene == OLED_SCENE_WATCH && f.modest && f.locale == 2, "the bench demo still shows (USB is physical access)");
}

static void neverOverAPress() {
  ScreenState s = {};
  s.tick(0, true, 0, true, true);
  ScreenFrame f = written(s, 500, payload(SCREEN_AGENT_WATCH, SCREEN_FLAG_WAKE_NOW | SCREEN_FLAG_AGENT_SPEAKING, 0, 0, true), true);
  expect(f.scene == OLED_SCENE_SEARCHING, "a wake never interrupts a press in flight");
  expect(s.tick(600, false, 3, true, true).scene == OLED_SCENE_RESULT, "the press still gets its result");
  ScreenState d = {};
  d.showDemo(0, OLED_SCENE_ERROR, 0, 0);
  f = written(d, 10, payload(SCREEN_AGENT_WATCH, SCREEN_FLAG_WAKE_NOW, 0, 0, true), true);
  expect(f.scene == OLED_SCENE_ERROR, "nor the bench demo");
  ScreenState r = {};
  r.tick(0, true, 0, true, true);
  r.tick(300, false, 3, true, true);
  f = written(r, 1000, payload(SCREEN_AGENT_PAUSED, SCREEN_FLAG_WAKE_NOW, 0, 0, true), true);
  expect(f.scene == OLED_SCENE_PAUSED, "a result already shown gives way to an agent wake");
  expect(r.tick(1100, false, 4, true, true).scene == OLED_SCENE_PAUSED, "and a late ack no longer changes it");
}

static void ownedOnly() {
  ScreenState s = {};
  written(s, 0, payload(SCREEN_AGENT_WATCH, SCREEN_FLAG_MODEST, CUBE_GLASS, 1), false);
  s.wake(0);
  at(s, 0);
  ScreenFrame f = at(s, 100, true, false);
  expect(f.scene == OLED_SCENE_SET_ME_UP && !f.modest && f.locale == 0, "an unowned pin shows only K9");
  expect(s.mascotByte() == 0 && !s.modest() && !s.hasPayload, "and forgets the payload and the mascot");
  f = at(s, 200);
  expect(f.scene == OLED_SCENE_NOTHING_KNOWN && f.shape == OLED_SHAPE_PEBBLE, "a new bind starts from nothing");

  ScreenState m = {};
  m.restoreMascot(FOLD_JELLY);
  m.wake(0);
  f = at(m, 0);
  expect(f.shape == OLED_SHAPE_FOLD && f.material == OLED_MATERIAL_JELLY, "the flash record's mascot shows after a reboot");
  m.restoreMascot(0x62);
  expect(m.mascotByte() == FOLD_JELLY, "a bad record byte is ignored");
  at(m, 10, true, false);
  expect(m.mascotByte() == 0, "an unowned pin drops a restored mascot too");

  ScreenState p = {};
  written(p, 0, payload(SCREEN_AGENT_WATCH, 0, CUBE_GLASS), false);
  f = p.tick(10, true, 0, true, true);
  expect(f.scene == OLED_SCENE_PRESSED && f.shape == OLED_SHAPE_CUBE && f.material == OLED_MATERIAL_GLASS,
         "the press scenes wear the owner's mascot");
  at(p, 20, false);
  f = p.tick(3000, true, 0, false, true);
  expect(f.scene == OLED_SCENE_NO_LINK && f.shape == OLED_SHAPE_CUBE, "«no link» too (the composer draws it as glass)");
  f = p.tick(9000, true, 0, true, false);
  expect(f.scene == OLED_SCENE_SET_ME_UP && f.shape == OLED_SHAPE_PEBBLE, "a press after unbind: K9, default pebble");
}

int main() {
  agentScenes();
  trustedWakes();
  speaking();
  staleReset();
  modestAndDisabled();
  neverOverAPress();
  ownedOnly();
  return failures ? 1 : 0;
}

// Host tests for «found you» in screen_state.h (k20, event_ack 0x11: the
// app sends it when the press-again notice appears; owner decision
// 2026-09-28: the one self-wake that vibrates, ▬ ▬). The scene, its 6 s
// hold, and the guards of an open characteristic: owned only, once a
// minute, never over a press in flight, the fall or the bench, never with
// the screen disabled or in quiet hours. foundYouNow tells loop() to let
// the motor play.
#include <stdio.h>
#include "../RailyPinsP1/screen_state.h"

static int failures = 0;
static void expect(bool condition, const char* name) {
  printf("%s %s\n", condition ? "PASS" : "FAIL", name);
  if (!condition) failures++;
}

static const uint8_t FOUND = ScreenState::ACK_FOUND_YOU;

static ScreenPayload payload(uint8_t agent, uint8_t flags = 0, bool encrypted = false) {
  const uint8_t bytes[SCREEN_PAYLOAD_LENGTH] = {1, agent, 0, 0, 0, 0, flags, 0x31, 1};
  ScreenPayload p = {};
  if (parseScreenPayload(bytes, sizeof(bytes), encrypted, &p) != ScreenPayloadError::ok) {
    printf("FAIL test payload rejected\n");
    failures++;
  }
  return p;
}

// As loop() feeds it: 0x11 raises the found-you flag and is the ack byte.
static ScreenFrame at(ScreenState& s, uint32_t now, uint8_t ack = 0, bool owned = true, bool press = false) {
  return s.tick(now, press, ack, true, owned, false, ack == FOUND);
}

static void timeline(uint32_t t0, const char* label) {
  char name[160];
  ScreenState s = {};
  at(s, t0);
  ScreenFrame f = at(s, t0 + 10, FOUND);
  snprintf(name, sizeof(name), "%s: 0x11 wakes the screen on «found you», first frame, normal contrast", label);
  expect(f.scene == OLED_SCENE_FOUND_YOU && f.frame == 0 && f.contrast == ScreenState::CONTRAST_NORMAL, name);
  snprintf(name, sizeof(name), "%s: that pass lets the motor play", label);
  expect(s.foundYouNow, name);
  at(s, t0 + 20);
  snprintf(name, sizeof(name), "%s: only that pass", label);
  expect(!s.foundYouNow, name);

  const OledSceneDesc& d = kOledScenes[OLED_SCENE_FOUND_YOU];
  uint32_t total = 0;
  for (uint8_t i = 0; i < d.frameCount; i++) total += kOledFrames[d.firstFrame + i].ms;
  snprintf(name, sizeof(name), "%s: 3 frames, 150/150/700 ms, looping (the pack's k20)", label);
  expect(d.frameCount == 3 && total == 1000 && (d.flags & OLED_SCENE_LOOP), name);
  f = at(s, t0 + 10 + 160);
  ScreenFrame g = at(s, t0 + 10 + 400);
  ScreenFrame h = at(s, t0 + 10 + 1050);
  snprintf(name, sizeof(name), "%s: jump, jump, smile, and round again", label);
  expect(f.frame == 1 && g.frame == 2 && h.frame == 0, name);
  f = at(s, t0 + 10 + 5999);
  snprintf(name, sizeof(name), "%s: it holds 6 s at full brightness", label);
  expect(f.scene == OLED_SCENE_FOUND_YOU && f.contrast == ScreenState::CONTRAST_NORMAL, name);
  f = at(s, t0 + 10 + 6500);
  snprintf(name, sizeof(name), "%s: then fades for 1 s", label);
  expect(f.scene == OLED_SCENE_FOUND_YOU && f.contrast == ScreenState::CONTRAST_NORMAL / 2, name);
  f = at(s, t0 + 10 + 7000);
  snprintf(name, sizeof(name), "%s: and is off", label);
  expect(f.scene == OLED_SCENE_OFF, name);
}

static void rateLimit() {
  ScreenState s = {};
  at(s, 1000, FOUND);
  ScreenFrame f = at(s, 60000 + 999, FOUND);
  expect(!s.foundYouNow && f.scene == OLED_SCENE_OFF, "a second 0x11 within a minute neither wakes nor vibrates");
  f = at(s, 61000, FOUND);
  expect(s.foundYouNow && f.scene == OLED_SCENE_FOUND_YOU, "a minute after the last one it counts again");
  // A flood on an awake screen never restarts the hold.
  for (uint32_t t = 61100; t < 66000; t += 100) at(s, t, FOUND);
  f = at(s, 61000 + 6500);
  expect(f.scene == OLED_SCENE_FOUND_YOU && f.contrast == ScreenState::CONTRAST_NORMAL / 2,
         "a flood of 0x11 keeps the first 6 s, it does not keep the screen lit");
}

static void guards() {
  ScreenState s = {};
  ScreenFrame f = at(s, 1000, FOUND, false);
  expect(!s.foundYouNow && f.scene == OLED_SCENE_OFF, "an unowned pin ignores 0x11: nobody to find");
  f = at(s, 1100, FOUND, true);
  expect(s.foundYouNow && f.scene == OLED_SCENE_FOUND_YOU, "an ignored 0x11 does not use up the minute");

  ScreenState p = {};
  at(p, 1000, 0, true, true);  // a press, in flight
  f = at(p, 1200, FOUND);
  expect(!p.foundYouNow && f.scene != OLED_SCENE_FOUND_YOU, "a press in flight wins: 0x11 is dropped");

  ScreenState same = {};
  f = at(same, 1000, FOUND, true, true);
  expect(!same.foundYouNow && f.scene == OLED_SCENE_PRESSED, "an ack in the same pass as a press is dropped");

  ScreenState fall = {};
  at(fall, 1000);
  fall.onFall(1000);
  f = at(fall, 1500, FOUND);
  expect(!fall.foundYouNow && f.scene == OLED_SCENE_FELL, "the fall is never cut short by 0x11");

  ScreenState demo = {};
  demo.showDemo(1000, OLED_SCENE_WATCH, 0, 0, 1);
  f = at(demo, 1100, FOUND);
  expect(!demo.foundYouNow && f.scene == OLED_SCENE_WATCH, "the bench demo is not replaced");

  ScreenState off = {};
  off.onWrite(1000, payload(SCREEN_AGENT_WATCH, SCREEN_FLAG_DISABLED, true), true);
  f = at(off, 1100, FOUND);
  expect(!off.foundYouNow && f.scene == OLED_SCENE_OFF, "screen disabled (bit 3): dark and silent");

  ScreenState quiet = {};
  quiet.onWrite(1000, payload(SCREEN_AGENT_WATCH, SCREEN_FLAG_QUIET_HOURS, true), true);
  f = at(quiet, 1100, FOUND);
  expect(!quiet.foundYouNow && f.scene == OLED_SCENE_OFF, "quiet hours (bit 1, trusted): dark and silent");

  ScreenState plain = {};
  plain.onWrite(1000, payload(SCREEN_AGENT_WATCH, SCREEN_FLAG_QUIET_HOURS, false), false);
  f = at(plain, 1100, FOUND);
  expect(plain.foundYouNow && f.scene == OLED_SCENE_FOUND_YOU,
         "a plaintext link cannot carry quiet hours (stripped), so it cannot hold «found you» back");
}

static void mailboxes() {
  ScreenState s = {};
  ScreenFrame f = s.tick(1000, false, 2, true, true, false, true);
  expect(s.foundYouNow && f.scene == OLED_SCENE_FOUND_YOU,
         "a later ack byte in the same pass does not lose «found you» (its own flag)");
  ScreenState b = {};
  f = b.tick(1000, false, FOUND, true, true, false, false);
  expect(!b.foundYouNow && f.scene == OLED_SCENE_OFF, "the ack byte alone is not the signal: the flag is");
}

static void interplay() {
  ScreenState s = {};
  s.onWrite(1000, payload(SCREEN_AGENT_WATCH), false);
  s.wake(1000);
  ScreenFrame f = at(s, 1100);
  expect(f.scene == OLED_SCENE_WATCH, "setup: the agent scene is up");
  f = at(s, 2000, FOUND);
  expect(f.scene == OLED_SCENE_FOUND_YOU, "«found you» replaces an awake agent scene");
  f = at(s, 2500);
  expect(f.scene == OLED_SCENE_FOUND_YOU, "and an agent repaint does not take it back");
  f = at(s, 3000, 0, true, true);
  expect(f.scene == OLED_SCENE_PRESSED, "the person's press takes over «found you»");

  ScreenState r = {};
  at(r, 1000, 0, true, true);
  at(r, 1500, 3);  // the press result
  f = at(r, 2000, FOUND);
  expect(r.foundYouNow && f.scene == OLED_SCENE_FOUND_YOU, "«found you» may follow a press result");
  f = at(r, 2100, 3);
  expect(f.scene == OLED_SCENE_FOUND_YOU, "a late ack 3 does not replace it");
}

int main() {
  timeline(1000, "timeline");
  timeline(0xFFFFFF00u, "millis rollover");
  rateLimit();
  guards();
  mailboxes();
  interplay();
  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("all found-you tests passed\n");
  return 0;
}

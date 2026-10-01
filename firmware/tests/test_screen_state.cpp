#include <stdio.h>
#include "../RailyPinsP1/screen_payload.h"
#include "../RailyPinsP1/screen_state.h"

// The mascot byte of screen_state (screen_payload.h) indexes the generated
// bodies: the two lists must stay the same length (shape, then material).
static_assert(OLED_SHAPE_COUNT == SCREEN_SHAPE_COUNT, "oled_assets.h and screen_payload.h disagree on shapes");
static_assert(OLED_MATERIAL_COUNT == SCREEN_MATERIAL_COUNT, "oled_assets.h and screen_payload.h disagree on materials");

static int failures = 0;
static void expect(bool condition, const char* name) {
  printf("%s %s\n", condition ? "PASS" : "FAIL", name);
  if (!condition) failures++;
}

// Ticks with nothing new from `from` to `to` (inclusive) in 10 ms steps and
// returns the last frame, the way loop() calls it.
static ScreenFrame idleUntil(ScreenState& s, uint32_t from, uint32_t to, bool connected = true) {
  ScreenFrame f = {};
  for (uint32_t t = from;; t += 10) {
    if ((uint32_t)(to - t) < 10) t = to;
    f = s.tick(t, false, 0, connected, true);
    if (t == to) break;
  }
  return f;
}

static void pressTimeline(uint32_t t0, const char* label) {
  char name[160];
  ScreenState s = {};
  ScreenFrame f = s.tick(t0, false, 0, true, true);
  snprintf(name, sizeof(name), "%s: the screen is off by default", label);
  expect(f.scene == OLED_SCENE_OFF && f.contrast == 0, name);

  f = s.tick(t0, true, 0, true, true);
  snprintf(name, sizeof(name), "%s: a press wakes at full contrast on the squash frame", label);
  expect(f.scene == OLED_SCENE_PRESSED && f.contrast == ScreenState::CONTRAST_PRESS, name);
  f = s.tick(t0 + 99, false, 0, true, true);
  snprintf(name, sizeof(name), "%s: the squash frame holds under 100 ms", label);
  expect(f.scene == OLED_SCENE_PRESSED, name);
  f = s.tick(t0 + 100, false, 0, true, true);
  snprintf(name, sizeof(name), "%s: the radar follows the squash frame by itself", label);
  expect(f.scene == OLED_SCENE_SEARCHING, name);
  f = s.tick(t0 + 400, false, 1, true, true);
  snprintf(name, sizeof(name), "%s: ack 1 keeps searching", label);
  expect(f.scene == OLED_SCENE_SEARCHING, name);
  f = s.tick(t0 + 1000, false, 2, true, true);
  snprintf(name, sizeof(name), "%s: contrast drops to normal 1 s after the press", label);
  expect(f.contrast == ScreenState::CONTRAST_NORMAL, name);
  f = idleUntil(s, t0 + 1000, t0 + 9990);
  snprintf(name, sizeof(name), "%s: a press in flight holds the screen past 6 s", label);
  expect(f.scene == OLED_SCENE_SEARCHING && f.contrast == ScreenState::CONTRAST_NORMAL, name);
  f = s.tick(t0 + 5000 + 5000, false, 3, true, true);
  snprintf(name, sizeof(name), "%s: ack 3 at the deadline still shows the result", label);
  expect(f.scene == OLED_SCENE_RESULT, name);
  f = idleUntil(s, t0 + 10000, t0 + 15999);
  snprintf(name, sizeof(name), "%s: the result stays 6 s", label);
  expect(f.scene == OLED_SCENE_RESULT && f.contrast == ScreenState::CONTRAST_NORMAL, name);
  f = s.tick(t0 + 16500, false, 0, true, true);
  snprintf(name, sizeof(name), "%s: then fades over 1 s", label);
  expect(f.scene == OLED_SCENE_RESULT && f.contrast == ScreenState::CONTRAST_NORMAL / 2, name);
  f = s.tick(t0 + 16999, false, 0, true, true);
  snprintf(name, sizeof(name), "%s: the fade never reaches 0 while the scene shows", label);
  expect(f.scene == OLED_SCENE_RESULT && f.contrast == 1, name);
  f = s.tick(t0 + 17000, false, 0, true, true);
  snprintf(name, sizeof(name), "%s: and sleeps", label);
  expect(f.scene == OLED_SCENE_OFF && f.contrast == 0, name);
}

int main() {
  pressTimeline(1000, "timeline");
  pressTimeline(UINT32_MAX - 3000, "timeline across the millis rollover");

  {
    ScreenState s = {};
    ScreenFrame f = s.tick(0, true, 0, true, true);
    f = s.tick(10, false, 2, true, true);
    expect(f.scene == OLED_SCENE_SEARCHING, "ack 2 skips the rest of the squash frame");
    f = s.tick(20, false, 4, true, true);
    expect(f.scene == OLED_SCENE_ERROR, "ack 4 shows «didn't work»");
    f = s.tick(30, false, 3, true, true);
    expect(f.scene == OLED_SCENE_RESULT, "latest ack wins: 3 after 4 shows the result");
    f = s.tick(40, false, 1, true, true);
    expect(f.scene == OLED_SCENE_RESULT, "a late ack 1 does not restart the search");
    f = s.tick(50, false, 0x13, true, true);
    expect(f.scene == OLED_SCENE_RESULT, "rhythm bytes are not screen events in v1");
  }
  {
    ScreenState s = {};
    s.tick(0, true, 0, true, true);
    ScreenFrame f = idleUntil(s, 0, 9990);
    expect(f.scene == OLED_SCENE_SEARCHING, "no answer: searching up to 10 s");
    f = s.tick(10000, false, 0, true, true);
    expect(f.scene == OLED_SCENE_ERROR, "no result within 10 s is «didn't work»");
    f = idleUntil(s, 10000, 16999);
    expect(f.scene == OLED_SCENE_ERROR, "the timeout error holds 6 s plus the fade");
    f = s.tick(17000, false, 0, true, true);
    expect(f.scene == OLED_SCENE_OFF, "then the screen sleeps");
  }
  {
    ScreenState s = {};
    s.tick(0, true, 0, true, true);
    s.tick(300, false, 3, true, true);
    ScreenFrame f = s.tick(5000, true, 3, true, true);
    expect(f.scene == OLED_SCENE_PRESSED, "a late ack 3 in the same pass as a new press does not resolve it");
    f = s.tick(5000, true, 4, true, true);
    expect(f.scene == OLED_SCENE_PRESSED, "nor does a late ack 4");
    f = s.tick(5100, false, 0, true, true);
    expect(f.scene == OLED_SCENE_SEARCHING, "the new press searches");
    f = s.tick(5200, false, 3, true, true);
    expect(f.scene == OLED_SCENE_RESULT, "and its own ack 3 in a later pass shows the result");
  }
  {
    // emitButtonEvent could not save the counter: the press never reached
    // the phone, and the pin raises its own failure, not a press.
    ScreenState s = {};
    ScreenFrame f = s.tick(0, false, 4, true, true, true);
    expect(f.scene == OLED_SCENE_ERROR && f.contrast == ScreenState::CONTRAST_PRESS,
           "a press the pin could not save shows «didn't work» at once, at full contrast");
    f = s.tick(50, false, 0, true, true);
    expect(f.scene == OLED_SCENE_ERROR, "and never searches");
    f = s.tick(60, false, 3, true, true);
    expect(f.scene == OLED_SCENE_RESULT, "a later phone ack is still an outcome (latest wins)");
    ScreenState t = {};
    t.tick(0, true, 0, true, true);
    f = t.tick(3000, false, 3, true, true, true);
    expect(f.scene == OLED_SCENE_ERROR, "a failed press drops a phone ack read in the same pass");
    f = idleUntil(t, 3000, 9999);
    expect(f.scene == OLED_SCENE_ERROR, "the failure holds 6 s plus the fade");
    f = t.tick(10000, false, 0, true, true);
    expect(f.scene == OLED_SCENE_OFF, "then sleeps, with no search timeout behind it");
  }
  {
    ScreenState s = {};
    ScreenFrame f = s.tick(0, false, 3, true, true);
    expect(f.scene == OLED_SCENE_OFF, "an ack never wakes the screen");
    f = s.tick(10, false, 4, true, true);
    expect(f.scene == OLED_SCENE_OFF, "nor does an error ack");
  }
  {
    ScreenState s = {};
    ScreenFrame f = s.tick(0, true, 0, false, true);
    expect(f.scene == OLED_SCENE_NO_LINK && !f.linked, "a press without a link shows «no link»");
    expect(f.contrast == ScreenState::CONTRAST_PRESS, "«no link» wakes at full contrast too");
    f = s.tick(50, false, 3, false, true);
    expect(f.scene == OLED_SCENE_NO_LINK, "no press is in flight without a link");
    f = idleUntil(s, 50, 2999, false);
    expect(f.scene == OLED_SCENE_NO_LINK, "«no link» shows 3 s");
    f = s.tick(3500, false, 0, false, true);
    expect(f.scene == OLED_SCENE_NO_LINK && f.contrast == ScreenState::CONTRAST_NORMAL / 2, "then fades");
    f = s.tick(4000, false, 0, false, true);
    expect(f.scene == OLED_SCENE_OFF, "and sleeps after 4 s");
  }
  {
    ScreenState s = {};
    ScreenFrame f = s.tick(0, true, 0, true, false);
    expect(f.scene == OLED_SCENE_SET_ME_UP, "a press on an unowned pin shows «set me up»");
    f = s.tick(0, true, 0, false, false);
    expect(f.scene == OLED_SCENE_SET_ME_UP, "«set me up» wins over «no link» on an unowned pin");
    f = s.tick(10, false, 3, true, false);
    expect(f.scene == OLED_SCENE_SET_ME_UP, "an unowned press is not in flight");
    f = idleUntil(s, 10, 6999, true);
    expect(f.scene == OLED_SCENE_SET_ME_UP, "«set me up» holds 6 s plus the fade");
    f = s.tick(7000, false, 0, true, false);
    expect(f.scene == OLED_SCENE_OFF, "then sleeps");
  }
  {
    ScreenState s = {};
    s.tick(0, true, 0, true, true);
    ScreenFrame f = s.tick(300, false, 3, true, true);
    f = s.tick(5000, true, 0, true, true);
    expect(f.scene == OLED_SCENE_PRESSED && f.contrast == ScreenState::CONTRAST_PRESS, "a new press restarts the timeline");
    f = s.tick(5100, false, 0, true, true);
    expect(f.scene == OLED_SCENE_SEARCHING, "and searches again");
    f = s.tick(14999, false, 0, true, true);
    expect(f.scene == OLED_SCENE_SEARCHING, "the new press gets its own 10 s");
    f = s.tick(15000, false, 0, true, true);
    expect(f.scene == OLED_SCENE_ERROR, "and its own timeout");
  }
  {
    ScreenState s = {};
    s.tick(0, true, 0, true, true);
    ScreenFrame a = s.tick(100, false, 0, true, true);
    ScreenFrame b = s.tick(224, false, 0, true, true);
    ScreenFrame c = s.tick(225, false, 0, true, true);
    ScreenFrame d = s.tick(600, false, 0, true, true);
    expect(a.frame == 0 && b.frame == 0 && c.frame == 1, "the radar steps every 125 ms");
    expect(d.frame == 0, "the radar loops after 4 frames");
    expect(a.sameImage(b) && !b.sameImage(c), "animation frames change the image, not the timers");
    s.tick(700, false, 3, true, true);
    ScreenFrame r0 = s.tick(1099, false, 0, true, true);
    ScreenFrame r1 = s.tick(1100, false, 0, true, true);
    ScreenFrame r2 = s.tick(1500, false, 0, true, true);
    expect(r0.scene == OLED_SCENE_RESULT && r0.frame == 0 && r1.frame == 1 && r2.frame == 0,
           "the result arrow nudges toward the phone every 400 ms");
    ScreenFrame e = s.tick(1600, false, 4, true, true);
    ScreenFrame e2 = s.tick(1600 + 90 + 90 + 5000, false, 0, true, true);
    expect(e.scene == OLED_SCENE_ERROR && e2.frame == 2, "the error shake plays once and holds its last frame");
  }
  {
    ScreenState s = {};
    int8_t seen[4][2];
    uint32_t t = 0;
    for (int i = 0; i < 4; i++) {
      ScreenFrame f = s.tick(t, true, 0, false, true);
      seen[i][0] = f.shiftX;
      seen[i][1] = f.shiftY;
      bool inRange = f.shiftX >= -2 && f.shiftX <= 2 && f.shiftY >= -2 && f.shiftY <= 2 &&
                     (f.shiftX != 0 || f.shiftY != 0);
      expect(inRange, "every wake shifts the mascot by 1-2 px");
      ScreenFrame again = s.tick(t + 500, true, 0, false, true);
      expect(again.shiftX == f.shiftX && again.shiftY == f.shiftY, "a press while awake keeps the shift");
      t += 5000;
      s.tick(t, false, 0, false, true);
      t += 10;
    }
    expect(seen[0][0] != seen[1][0] || seen[0][1] != seen[1][1], "the next wake moves somewhere else");
    ScreenState replay = {};
    ScreenFrame first = replay.tick(0, true, 0, false, true);
    expect(first.shiftX == seen[0][0] && first.shiftY == seen[0][1], "the shift sequence is deterministic");
  }
  {
    ScreenState s = {};
    s.wake(0);
    ScreenFrame f = s.tick(0, false, 0, true, true);
    expect(f.scene == OLED_SCENE_NOTHING_KNOWN, "a wake with no state shows the mascot alone");
    expect(f.contrast == ScreenState::CONTRAST_NORMAL, "without a press the contrast stays low");
    f = s.tick(600, false, 0, true, true);
    expect(f.frame == 1, "the mascot blinks");
    f = s.tick(720, false, 0, true, true);
    expect(f.frame == 0, "and opens its eyes again");
    f = s.tick(720, false, 0, false, true);
    expect(!f.linked, "the status row follows the link");
    f = s.tick(7000, false, 0, true, true);
    expect(f.scene == OLED_SCENE_OFF, "the wake ends after 6 s and the fade");
  }
  {
    ScreenState s = {};
    s.showDemo(0, OLED_SCENE_RESULT, OLED_SHAPE_DROP, OLED_MATERIAL_GLASS);
    ScreenFrame f = s.tick(0, false, 0, false, false);
    expect(f.scene == OLED_SCENE_RESULT && f.shape == OLED_SHAPE_DROP && f.material == OLED_MATERIAL_GLASS,
           "the bench demo shows the scene and look asked for");
    f = s.tick(100, false, 4, false, false);
    expect(f.scene == OLED_SCENE_RESULT, "acks do not interrupt a demo");
    f = idleUntil(s, 100, 30999, false);
    expect(f.scene == OLED_SCENE_RESULT, "a demo holds 30 s");
    f = s.tick(31000, false, 0, false, false);
    expect(f.scene == OLED_SCENE_OFF, "then sleeps");
    s.showDemo(40000, OLED_SCENE_PRESSED, 9, 9);
    f = s.tick(40500, false, 0, true, true);
    expect(f.scene == OLED_SCENE_PRESSED && f.shape == OLED_SHAPE_PEBBLE && f.material == OLED_MATERIAL_SATIN,
           "a demo press frame holds, and an unknown look falls back to the pebble");
    f = s.tick(40600, true, 0, true, true);
    expect(f.shape == OLED_SHAPE_PEBBLE && f.scene == OLED_SCENE_PRESSED, "a real press ends the demo");
    f = s.tick(41000, false, 0, true, true);
    s.showDemo(41000, OLED_SCENE_COUNT, 0, 0);
    ScreenFrame same = s.tick(41000, false, 0, true, true);
    expect(same.scene == f.scene && same.scene == OLED_SCENE_SEARCHING, "an unknown demo scene is ignored");
    s.showDemo(41000, OLED_SCENE_OFF, 0, 0);
    f = s.tick(41000, false, 0, true, true);
    expect(f.scene == OLED_SCENE_OFF, "demo off turns the screen off");
  }
  return failures ? 1 : 0;
}

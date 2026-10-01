// Host tests for the fall of screen_state.h (keyring-oled PR 5, owner
// decision 2026-09-28: the screen lights, «Упал. Было больно», ≈2.7 s, no
// vibration): the timeline, disabled stays dark, a press in flight wins, a
// fall replaces an agent scene and the bench demo, modest and words follow
// the phone, a wake bit never cuts the fall short.
#include <stdio.h>
#include "../RailyPinsP1/screen_state.h"

static int failures = 0;
static void expect(bool condition, const char* name) {
  printf("%s %s\n", condition ? "PASS" : "FAIL", name);
  if (!condition) failures++;
}

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

static ScreenFrame at(ScreenState& s, uint32_t now, bool press = false) {
  return s.tick(now, press, 0, true, true);
}

// The frame the table says shows `ms` into the fall.
static uint8_t expectedFrame(uint32_t ms) {
  const OledSceneDesc& d = kOledScenes[OLED_SCENE_FELL];
  for (uint8_t i = 0; i < d.frameCount; i++) {
    if (ms < kOledFrames[d.firstFrame + i].ms) return i;
    ms -= kOledFrames[d.firstFrame + i].ms;
  }
  return d.frameCount - 1;
}

static void timeline(uint32_t t0, const char* label) {
  char name[160];
  ScreenState s = {};
  at(s, t0);
  s.onFall(t0);
  ScreenFrame f = at(s, t0);
  snprintf(name, sizeof(name), "%s: a fall wakes the screen on its first frame, normal contrast", label);
  expect(f.scene == OLED_SCENE_FELL && f.frame == 0 && f.contrast == ScreenState::CONTRAST_NORMAL, name);

  uint32_t total = 0;
  const OledSceneDesc& d = kOledScenes[OLED_SCENE_FELL];
  for (uint8_t i = 0; i < d.frameCount; i++) total += kOledFrames[d.firstFrame + i].ms;
  snprintf(name, sizeof(name), "%s: 8 frames, %u ms (the pack's 70/70/70/110/120/140/700/1400)", label,
           (unsigned)total);
  expect(d.frameCount == 8 && total == 2680, name);

  bool framesRight = true;
  for (uint32_t ms = 0; ms < total; ms += 5) {
    f = at(s, t0 + ms);
    if (f.scene != OLED_SCENE_FELL || f.frame != expectedFrame(ms) || f.contrast != ScreenState::CONTRAST_NORMAL) {
      framesRight = false;
    }
  }
  snprintf(name, sizeof(name), "%s: it plays every frame once at full brightness, in the pack's timing", label);
  expect(framesRight, name);
  f = at(s, t0 + total + 500);
  snprintf(name, sizeof(name), "%s: then the last frame («было больно») holds through the 1 s fade", label);
  expect(f.scene == OLED_SCENE_FELL && f.frame == 7 && f.contrast == ScreenState::CONTRAST_NORMAL / 2, name);
  f = at(s, t0 + total + 999);
  snprintf(name, sizeof(name), "%s: the fade never reaches 0 while it shows", label);
  expect(f.scene == OLED_SCENE_FELL && f.contrast == 1, name);
  f = at(s, t0 + total + 1000);
  snprintf(name, sizeof(name), "%s: and the screen is off, the fall does not loop", label);
  expect(f.scene == OLED_SCENE_OFF && f.contrast == 0, name);
  f = at(s, t0 + total + 60000);
  snprintf(name, sizeof(name), "%s: and stays off", label);
  expect(f.scene == OLED_SCENE_OFF, name);
}

int main() {
  timeline(1000, "timeline");
  timeline(UINT32_MAX - 1500, "timeline across the millis rollover");

  {
    // Disabled (bit 3): the screen stays dark, the fall included.
    ScreenState s = {};
    s.onWrite(0, payload(SCREEN_AGENT_WATCH, SCREEN_FLAG_DISABLED), false);
    at(s, 0);
    s.onFall(10);
    bool dark = true;
    for (uint32_t t = 10; t < 4000; t += 10) {
      if (at(s, t).scene != OLED_SCENE_OFF) dark = false;
    }
    expect(dark, "disabled: a fall stays dark");
    // The fall was never started: enabling the screen inside its 3.7 s
    // does not bring it back half-way.
    ScreenState u = {};
    u.onWrite(0, payload(SCREEN_AGENT_WATCH, SCREEN_FLAG_DISABLED), false);
    at(u, 0);
    u.onFall(10);
    at(u, 500);
    u.onWrite(1000, payload(SCREEN_AGENT_WATCH), false);
    expect(at(u, 1000).scene == OLED_SCENE_OFF, "a fall while disabled is not replayed once enabled");
    s.onWrite(5000, payload(SCREEN_AGENT_WATCH), false);
    at(s, 5000);
    s.onFall(5010);
    expect(at(s, 5010).scene == OLED_SCENE_FELL, "enabled again: the next fall shows");
    s.onWrite(5100, payload(SCREEN_AGENT_WATCH, SCREEN_FLAG_DISABLED), false);
    expect(at(s, 5100).scene == OLED_SCENE_OFF, "disabled mid-fall: dark at once");
  }
  {
    // A press in flight wins: the person is pressing.
    ScreenState s = {};
    at(s, 0, true);
    at(s, 200);
    s.onFall(300);
    ScreenFrame f = at(s, 300);
    expect(f.scene == OLED_SCENE_SEARCHING, "a fall during a press in flight is dropped");
    f = s.tick(400, false, 3, true, true);
    expect(f.scene == OLED_SCENE_RESULT, "and the press still gets its result");
    // A press outcome on the glass (not in flight) gives way.
    s.onFall(500);
    expect(at(s, 500).scene == OLED_SCENE_FELL, "a fall replaces the result on the glass");
    // A press during the fall wins too.
    f = at(s, 800, true);
    expect(f.scene == OLED_SCENE_PRESSED && f.contrast == ScreenState::CONTRAST_PRESS, "a press cuts the fall short");
    f = at(s, 900);
    expect(f.scene == OLED_SCENE_SEARCHING, "and searches as usual");
  }
  {
    // Codex #1194: a fall that replaces a press outcome inside the press's
    // 1 s contrast boost renders at the normal contrast, not CONTRAST_PRESS.
    ScreenState s = {};
    at(s, 0, true);
    ScreenFrame f = s.tick(200, false, 3, true, true);
    expect(f.scene == OLED_SCENE_RESULT && f.contrast == ScreenState::CONTRAST_PRESS,
           "a result inside the press's first second is boosted");
    s.onFall(300);
    f = at(s, 300);
    expect(f.scene == OLED_SCENE_FELL && f.contrast == ScreenState::CONTRAST_NORMAL,
           "a fall within BOOST_MS after a press result renders at the normal fall contrast");
    ScreenState t = {};
    t.tick(0, false, 0, true, true, true);  // a press the pin could not save: «didn't work», boosted
    t.onFall(100);
    f = at(t, 100);
    expect(f.scene == OLED_SCENE_FELL && f.contrast == ScreenState::CONTRAST_NORMAL,
           "and after a failed press too");
  }
  {
    // A press and a fall in the same loop pass: updateScreen() runs onFall
    // before tick(), so the press wins.
    ScreenState s = {};
    at(s, 0);
    s.onFall(100);
    ScreenFrame f = at(s, 100, true);
    expect(f.scene == OLED_SCENE_PRESSED, "a press in the same pass as a fall wins");
  }
  {
    // A fall during an agent scene replaces it, and then the screen goes off
    // instead of returning to the agent.
    ScreenState s = {};
    s.onWrite(0, payload(SCREEN_AGENT_WATCH, 0, 0x12, SCREEN_LOCALE_RU), true);
    at(s, 0);
    s.wake(10);
    expect(at(s, 10).scene == OLED_SCENE_WATCH, "an agent scene is up");
    s.onFall(1000);
    ScreenFrame f = at(s, 1000);
    expect(f.scene == OLED_SCENE_FELL && f.frame == 0, "the fall replaces it from its first frame");
    expect(f.shape == OLED_SHAPE_CUBE && f.material == OLED_MATERIAL_GLASS && f.locale == SCREEN_LOCALE_RU,
           "in the owner's mascot and language");
    f = at(s, 1000 + 2680 + 999);
    expect(f.scene == OLED_SCENE_FELL, "the agent never repaints over it");
    f = at(s, 1000 + 2680 + 1000);
    expect(f.scene == OLED_SCENE_OFF, "then off, not back to the agent");
  }
  {
    // Speaking holds an agent scene awake; a fall replaces it and ends on time.
    ScreenState s = {};
    s.onWrite(0, payload(SCREEN_AGENT_WATCH, SCREEN_FLAG_AGENT_SPEAKING), true);
    ScreenFrame f = at(s, 0);
    expect(f.scene == OLED_SCENE_SPEAKING, "speaking wakes the screen");
    s.onFall(100);
    f = at(s, 100);
    expect(f.scene == OLED_SCENE_FELL, "a fall replaces «agent speaks»");
    f = at(s, 100 + 3680);
    expect(f.scene == OLED_SCENE_OFF, "and the speaking hold does not keep the fall lit");
  }
  {
    // A wake bit that lands during the fall does not cut it short.
    ScreenState s = {};
    at(s, 0);
    s.onFall(0);
    s.onWrite(500, payload(SCREEN_AGENT_PAUSED, SCREEN_FLAG_WAKE_NOW, 0, SCREEN_LOCALE_EN, true), true);
    ScreenFrame f = at(s, 500);
    expect(f.scene == OLED_SCENE_FELL && f.locale == SCREEN_LOCALE_EN, "a wake bit during the fall does not cut it short");
    f = at(s, 3680);
    expect(f.scene == OLED_SCENE_OFF, "and the fall still ends on time");
  }
  {
    // Modest: the mascot alone (the composer drops status, floor, bubble,
    // words); quiet hours do not stop it.
    ScreenState s = {};
    s.onWrite(0, payload(SCREEN_AGENT_WATCH, SCREEN_FLAG_MODEST | SCREEN_FLAG_QUIET_HOURS, 0, SCREEN_LOCALE_RU,
                         true),
              true);
    at(s, 0);
    s.onFall(10);
    ScreenFrame f = at(s, 10);
    expect(f.scene == OLED_SCENE_FELL && f.modest, "modest: the fall shows the mascot alone");
    expect(s.quietHours(), "and it plays in quiet hours (no motor to silence)");
  }
  {
    // The bench demo gives way; a second fall replays from the top.
    ScreenState s = {};
    s.showDemo(0, OLED_SCENE_RESULT, OLED_SHAPE_DROP, OLED_MATERIAL_GLASS, 2);
    s.onFall(100);
    ScreenFrame f = at(s, 100);
    expect(f.scene == OLED_SCENE_FELL && f.shape == OLED_SHAPE_PEBBLE && f.locale == 0,
           "a fall ends the bench demo and uses the pin's own look");
    f = at(s, 2000);
    expect(f.frame == 7, "on its last frame");
    s.onFall(2000);
    f = at(s, 2000);
    expect(f.scene == OLED_SCENE_FELL && f.frame == 0, "a second drop replays from the top");
    f = at(s, 2000 + 3679);
    expect(f.scene == OLED_SCENE_FELL, "with its own full timing");
  }
  {
    // An unowned pin falls in the default pebble, icons only.
    ScreenState s = {};
    s.tick(0, false, 0, false, false);
    s.onFall(10);
    ScreenFrame f = s.tick(10, false, 0, false, false);
    expect(f.scene == OLED_SCENE_FELL && f.shape == OLED_SHAPE_PEBBLE && f.material == OLED_MATERIAL_SATIN &&
               f.locale == 0 && !f.linked,
           "an unowned pin without a link falls too, default pebble, icons only");
  }
  return failures ? 1 : 0;
}

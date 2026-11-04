// Host tests for the voice layer of screen_state.h (voice from the pin,
// 0.4.4, owner 2026-11-03): «pressed» while held, «speak» from START to the
// release, «thinking» until the answer or 10 s, «speaking» (the answer on
// the phone) while bit 5 lasts, and the failures' scenes. The person
// started all of it: no speaking budget, no wake rate limit, nothing else
// takes the glass. Bit 6 (thinking) is bounded to 10 s past a write and
// VOICE_THINKING_MAX_MS from the release. Every timer across the wrap.
#include <stdio.h>
#include "../RailyPinsP1/screen_state.h"

static int failures = 0;
static void expect(bool condition, const char* name) {
  printf("%s %s\n", condition ? "PASS" : "FAIL", name);
  if (!condition) failures++;
}

static ScreenPayload payload(uint8_t flags, bool encrypted = true, uint8_t locale = SCREEN_LOCALE_RU) {
  const uint8_t bytes[SCREEN_PAYLOAD_LENGTH] = {1, SCREEN_AGENT_WATCH, 0, 0, 0, 0, flags, 0x12, locale};
  ScreenPayload p = {};
  if (parseScreenPayload(bytes, sizeof(bytes), encrypted, &p) != ScreenPayloadError::ok) {
    printf("FAIL test payload rejected\n");
    failures++;
  }
  return p;
}

static ScreenFrame at(ScreenState& s, uint32_t now, uint8_t ack = 0, bool connected = true) {
  return s.tick(now, false, ack, connected, true);
}

static const uint32_t SPEAK = SCREEN_FLAG_AGENT_SPEAKING;
static const uint32_t THINK = SCREEN_FLAG_AGENT_THINKING;
static const uint32_t TAIL = ScreenState::IDLE_MS + ScreenState::FADE_MS;

// A hold that talked and was released at t0 + 3000: the screen is on «thinking».
static void talkAndRelease(ScreenState& s, uint32_t t0) {
  s.onVoiceHold(t0);
  s.onVoiceStart(t0 + 2000);
  s.onVoiceEnd(t0 + 3000, VOICE_CUE_SENT);
}

static void holdFlow(uint32_t t0, const char* label) {
  char name[200];
  ScreenState s = {};
  s.onWrite(t0, payload(0), true);
  at(s, t0);
  s.onVoiceHold(t0 + 10);
  ScreenFrame f = at(s, t0 + 10);
  snprintf(name, sizeof(name), "%s: press-down on an armed link: «pressed» at full contrast", label);
  expect(f.scene == OLED_SCENE_PRESSED && f.contrast == ScreenState::CONTRAST_PRESS, name);
  f = at(s, t0 + 10 + 1999);
  snprintf(name, sizeof(name), "%s: «pressed» stays while held (no searching, no fade)", label);
  expect(f.scene == OLED_SCENE_PRESSED && f.contrast == ScreenState::CONTRAST_NORMAL, name);
  s.onVoiceStart(t0 + 2010);
  f = at(s, t0 + 2010);
  snprintf(name, sizeof(name), "%s: START: «speak», in the phone's locale", label);
  expect(f.scene == OLED_SCENE_TALK && f.frame == 0 && f.locale == SCREEN_LOCALE_RU, name);
  f = at(s, t0 + 2010 + 50000);
  snprintf(name, sizeof(name), "%s: «speak» holds the whole talk, no idle fade (50 s)", label);
  expect(f.scene == OLED_SCENE_TALK && f.contrast == ScreenState::CONTRAST_NORMAL, name);
  s.onVoiceEnd(t0 + 52010, VOICE_CUE_SENT);
  f = at(s, t0 + 52010);
  snprintf(name, sizeof(name), "%s: release (END 0): «thinking»", label);
  expect(f.scene == OLED_SCENE_THINKING && f.frame == 0, name);
  f = at(s, t0 + 52010 + ScreenState::VOICE_REPLY_MS - 1);
  snprintf(name, sizeof(name), "%s: «thinking» at full contrast up to 10 s", label);
  expect(f.scene == OLED_SCENE_THINKING && f.contrast == ScreenState::CONTRAST_NORMAL, name);
  f = at(s, t0 + 52010 + ScreenState::VOICE_REPLY_MS + 500);
  snprintf(name, sizeof(name), "%s: no answer: it fades after 10 s", label);
  expect(f.scene == OLED_SCENE_THINKING && f.contrast < ScreenState::CONTRAST_NORMAL, name);
  f = at(s, t0 + 52010 + ScreenState::VOICE_REPLY_MS + ScreenState::FADE_MS);
  snprintf(name, sizeof(name), "%s: then off", label);
  expect(f.scene == OLED_SCENE_OFF && s.voice == ScreenState::VOICE_SCREEN_NONE, name);
}

static void answer() {
  ScreenState s = {};
  s.onWrite(0, payload(0), true);
  at(s, 0);
  talkAndRelease(s, 1000);
  s.onWrite(5000, payload(THINK), true);
  ScreenFrame f = at(s, 5000 + ScreenState::VOICE_REPLY_MS - 1);
  expect(f.scene == OLED_SCENE_THINKING && f.contrast == ScreenState::CONTRAST_NORMAL,
         "bit 6 keeps «thinking» up 10 s past its write");
  s.onWrite(9000, payload(SPEAK), true);
  f = at(s, 9000);
  expect(f.scene == OLED_SCENE_SPEAKING && f.frame == 0, "bit 5: «speaking», the answer on the phone");
  f = at(s, 9000 + 45000);
  expect(f.scene == OLED_SCENE_SPEAKING && f.contrast == ScreenState::CONTRAST_NORMAL,
         "the answer holds the screen while bit 5 lasts (45 s)");
  s.onWrite(55000, payload(0), true);
  f = at(s, 55000);
  expect(f.scene == OLED_SCENE_WATCH, "bit 5 cleared: the agent's scene");
  f = at(s, 55000 + TAIL - 1);
  expect(f.scene == OLED_SCENE_WATCH && f.contrast < ScreenState::CONTRAST_NORMAL, "then the usual idle and fade");
  expect(at(s, 55000 + TAIL).scene == OLED_SCENE_OFF, "and off");
  expect(s.speaking.count == 0 && s.speaking.usedMs() == 0, "the person's own answer charged nothing to the speaking budget");

  // The answer is bounded like a speaking hold: 60 s, then idle.
  ScreenState c = {};
  c.onWrite(0, payload(0), true);
  talkAndRelease(c, 0);
  c.onWrite(4000, payload(SPEAK, false), false);  // even a plaintext writer
  f = at(c, 4000 + SPEAKING_HOLD_MAX_MS - 1);
  expect(f.scene == OLED_SCENE_SPEAKING && f.contrast == ScreenState::CONTRAST_NORMAL, "the answer up to 60 s");
  at(c, 4000 + SPEAKING_HOLD_MAX_MS);
  f = at(c, 4000 + SPEAKING_HOLD_MAX_MS + TAIL);
  expect(f.scene == OLED_SCENE_OFF, "never past 60 s + the idle and fade, bit 5 still set");

  // Speaking that follows the hold does not use the interval either: an
  // unsolicited speaking wake right before it does not block it.
  ScreenState w = {};
  w.onWrite(0, payload(SPEAK), true);
  expect(at(w, 0).scene == OLED_SCENE_SPEAKING && w.speaking.count == 1, "an unsolicited speaking wake (charged)");
  w.onWrite(1000, payload(0), true);
  at(w, 1000);
  talkAndRelease(w, 2000);
  w.onWrite(6000, payload(SPEAK), true);
  f = at(w, 6000);
  expect(f.scene == OLED_SCENE_SPEAKING && w.speaking.count == 1,
         "the person's answer 6 s later shows (no 60 s interval) and is not charged");
  w.onWrite(9000, payload(0), true);
  at(w, 9000);
  at(w, 9000 + TAIL);
  w.onWrite(20000, payload(SPEAK), true);
  expect(at(w, 20000).scene == OLED_SCENE_OFF, "an unsolicited wake afterwards keeps today's 60 s interval");
}

static void thinkingBounds() {
  // A plaintext writer repeating bit 6 cannot hold «thinking» forever.
  ScreenState s = {};
  s.onWrite(0, payload(0), true);
  talkAndRelease(s, 0);
  bool upAtCap = true;
  for (uint32_t t = 3000 + 9000; t < 3000 + 200000; t += 9000) {
    s.onWrite(t, payload(THINK, false), false);
    const ScreenFrame f = at(s, t);
    if (t < 3000 + ScreenState::VOICE_THINKING_MAX_MS && f.scene != OLED_SCENE_THINKING) upAtCap = false;
  }
  expect(upAtCap, "repeated thinking writes keep «thinking» up to the cap");
  expect(at(s, 3000 + 200000).scene == OLED_SCENE_OFF, "and the screen is off well past the cap");

  // A write just before the cap gets no 10 s more: the fade starts at the cap.
  ScreenState late = {};
  late.onWrite(0, payload(0), true);
  talkAndRelease(late, 0);
  for (uint32_t t = 3000 + 9000; t < 3000 + ScreenState::VOICE_THINKING_MAX_MS; t += 9000) {
    late.onWrite(t, payload(THINK, false), false);
    at(late, t);
  }
  const uint32_t capAt = 3000 + ScreenState::VOICE_THINKING_MAX_MS;
  late.onWrite(capAt - 10, payload(THINK, false), false);
  expect(at(late, capAt - 1).scene == OLED_SCENE_THINKING, "«thinking» is up just before the cap");
  expect(at(late, capAt + ScreenState::FADE_MS).scene == OLED_SCENE_OFF,
         "a write just before the cap does not outlive it: off after the fade");

  // Bit 6 never wakes the screen, and it expires 10 s after its write.
  ScreenState q = {};
  q.onWrite(0, payload(THINK), true);
  expect(at(q, 0).scene == OLED_SCENE_OFF, "bit 6 alone never wakes the screen");
  q.wake(5000);
  expect(at(q, 5000).scene == OLED_SCENE_THINKING, "a wake within 10 s shows «thinking» (after speaking, before the rest)");
  at(q, 5000 + TAIL);
  at(q, 10000);
  q.wake(10001);
  expect(at(q, 10001).scene == OLED_SCENE_WATCH && !q.payload.hasFlag(THINK), "10 s after its write, bit 6 is gone");
  ScreenState both = {};
  both.onWrite(0, payload(SPEAK | THINK), true);
  expect(at(both, 0).scene == OLED_SCENE_SPEAKING, "speaking comes before thinking");

  // Link loss and the stale reset clear bit 6.
  ScreenState l = {};
  l.onWrite(0, payload(THINK), true);
  at(l, 0, 0, true);
  at(l, 100, 0, false);
  expect(!l.payload.hasFlag(THINK), "the link lost: bit 6 cleared by the pin");
}

static void failuresAndAcks() {
  ScreenState s = {};
  s.onWrite(0, payload(0), true);
  talkAndRelease(s, 0);
  ScreenFrame f = at(s, 4000, 4);
  expect(f.scene == OLED_SCENE_ERROR && s.voice == ScreenState::VOICE_SCREEN_NONE,
         "event_ack 4 while «thinking»: «didn't work» (an empty question)");
  expect(at(s, 4000 + TAIL).scene == OLED_SCENE_OFF, "then the usual idle and fade");

  ScreenState a = {};
  a.onWrite(0, payload(0), true);
  talkAndRelease(a, 0);
  a.onWrite(4000, payload(SPEAK), true);
  at(a, 4000);
  expect(at(a, 5000, 4).scene == OLED_SCENE_ERROR, "event_ack 4 during the answer: «didn't work»");

  ScreenState n = {};
  talkAndRelease(n, 0);
  expect(at(n, 4000, 3).scene == OLED_SCENE_THINKING && at(n, 4100, 1).scene == OLED_SCENE_THINKING,
         "ack 1 and 3 leave «thinking» alone");
  at(n, 3000 + TAIL + 5000);
  expect(at(n, 20000, 4).scene == OLED_SCENE_OFF, "an ack 4 after it faded never wakes the screen");

  ScreenState l = {};
  l.onVoiceHold(0);
  l.onVoiceStart(2000);
  l.onVoiceEnd(2500, VOICE_CUE_NO_LINK);
  f = at(l, 2500, 4, false);
  expect(f.scene == OLED_SCENE_NO_LINK && f.contrast == ScreenState::CONTRAST_PRESS,
         "the link lost mid-hold: «no link» (the LED's 4 in the same pass changes nothing)");
  expect(at(l, 2500 + ScreenState::NO_LINK_MS + ScreenState::FADE_MS).scene == OLED_SCENE_OFF, "for 3 s, then off");

  ScreenState u = {};
  u.onVoiceEnd(100, VOICE_CUE_NO_LINK);  // a 2 s hold on a voice pin with no phone: no «pressed» before it
  expect(at(u, 100, 0, false).scene == OLED_SCENE_NO_LINK, "a talk hold with no phone: «no link»");

  ScreenState p = {};
  p.onVoiceHold(0);
  p.onVoiceStart(2000);
  p.onVoiceEnd(2100, VOICE_CUE_SEE_PHONE);
  f = at(p, 2100, 3);
  expect(f.scene == OLED_SCENE_RESULT, "phone STOP 1 (minutes spent): «see the phone»");
  expect(at(p, 2100 + TAIL).scene == OLED_SCENE_OFF, "for the usual 6 s");

  ScreenState e = {};
  e.onVoiceHold(0);
  e.onVoiceStart(2000);
  e.onVoiceEnd(2100, VOICE_CUE_TALK_FAILED);
  expect(at(e, 2100).scene == OLED_SCENE_ERROR && e.voice == ScreenState::VOICE_SCREEN_NONE,
         "ring full, low battery, STOP 2-4: «didn't work»");
  ScreenState e2 = {};
  e2.tick(2100, false, 0, false, true, true);  // the sketch's pendingScreenPressFailed path, no hold live
  expect(at(e2, 2100, 0, false).scene == OLED_SCENE_ERROR && at(e2, 2100 + TAIL, 0, false).scene == OLED_SCENE_OFF,
         "the plain 2 s hold (the flag, no voice layer): «didn't work», as 0.3.0");
}

// A phone STOP that arrives after the release (voiceStopRoute LATE).
static void lateStop() {
  ScreenState s = {};
  expect(!s.voiceReplying(), "idle: no reply awaited");
  s.onVoiceHold(0);
  expect(!s.voiceReplying(), "held: the STOP is a live one");
  s.onVoiceStart(2000);
  expect(!s.voiceReplying(), "talking: the STOP is a live one");
  s.onVoiceEnd(3000, VOICE_CUE_SENT);
  expect(s.voiceReplying(), "«thinking»: a late STOP counts");
  s.onVoiceEnd(4000, voiceStopCue(VOICE_STOP_MINUTES_SPENT));
  ScreenFrame f = at(s, 4000, 3);
  expect(f.scene == OLED_SCENE_RESULT && !s.voiceReplying(), "late STOP 1 while «thinking»: «see the phone»");
  expect(at(s, 4000 + TAIL).scene == OLED_SCENE_OFF, "for the usual 6 s, not a fade from «thinking»");

  ScreenState e = {};
  e.onWrite(0, payload(0), true);
  talkAndRelease(e, 0);
  e.onVoiceEnd(4000, voiceStopCue(2));
  expect(at(e, 4000, 4).scene == OLED_SCENE_ERROR && !e.voiceReplying(), "late STOP 2 while «thinking»: «didn't work»");

  ScreenState a = {};
  a.onWrite(0, payload(0), true);
  talkAndRelease(a, 0);
  a.onWrite(4000, payload(SPEAK), true);
  at(a, 4000);
  expect(a.voiceReplying(), "the answer on the glass: a late STOP counts");
  a.onVoiceEnd(5000, voiceStopCue(VOICE_STOP_MINUTES_SPENT));
  expect(at(a, 5000).scene == OLED_SCENE_RESULT, "late STOP 1 during the answer: «see the phone»");

  ScreenState o = {};
  talkAndRelease(o, 0);
  at(o, 3000 + ScreenState::VOICE_REPLY_MS + ScreenState::FADE_MS);
  expect(!o.voiceReplying(), "faded out: the reply is over, a STOP now is dropped");
}

// A press from the phone during a hold: on the wire as usual, not on the glass.
static void phonePressDuringHold() {
  ScreenState s = {};
  s.onVoiceHold(0);
  ScreenFrame f = s.tick(500, true, 0, true, true);
  expect(f.scene == OLED_SCENE_PRESSED && !s.inFlight && s.voice == ScreenState::VOICE_SCREEN_HELD,
         "a phone press under the threshold: «pressed» stays, the hold stays, nothing in flight");
  f = at(s, 700);
  expect(f.scene == OLED_SCENE_PRESSED, "no search with the mic on");
  s.onVoiceStart(2000);
  expect(at(s, 2000).scene == OLED_SCENE_TALK, "START still shows «speak»");
  f = s.tick(2500, true, 0, true, true);
  expect(f.scene == OLED_SCENE_TALK && !s.inFlight, "a phone press during «speak»: «speak» stays");
  f = s.tick(2600, false, 0, true, true, true);
  expect(f.scene == OLED_SCENE_TALK && s.voice == ScreenState::VOICE_SCREEN_TALK, "and its failure does not show either");
  f = at(s, 2500 + ScreenState::FLIGHT_TIMEOUT_MS + 1000);
  expect(f.scene == OLED_SCENE_TALK, "no flight timeout turns into «didn't work»");
  s.onVoiceEnd(13000, VOICE_CUE_SENT);
  expect(at(s, 13000).scene == OLED_SCENE_THINKING, "the release: «thinking» as usual");
  // After the release a press is an ordinary one again.
  f = s.tick(14000, true, 0, true, true);
  expect(f.scene == OLED_SCENE_PRESSED && s.inFlight && s.voice == ScreenState::VOICE_SCREEN_NONE,
         "a phone press after the release: the press's own screen");
  // A tap lets go first, so its scan shows in the same pass.
  ScreenState t = {};
  t.onVoiceHold(0);
  t.onVoiceEnd(300, VOICE_CUE_NONE);
  f = t.tick(300, true, 0, true, true);
  expect(f.scene == OLED_SCENE_PRESSED && t.inFlight, "a tap's scan in the release's own pass still shows");
  ScreenState tf = {};
  tf.onVoiceHold(0);
  tf.onVoiceEnd(300, VOICE_CUE_NONE);
  expect(tf.tick(300, false, 0, true, true, true).scene == OLED_SCENE_ERROR, "and so does a tap's failed scan");
}

static void tapAndRepeats() {
  // A tap on an armed link: «pressed» at press-down, then the scan's own
  // screen as in 0.4.3.
  ScreenState s = {};
  s.onVoiceHold(0);
  s.onVoiceEnd(300, VOICE_CUE_NONE);
  expect(at(s, 300).scene == OLED_SCENE_PRESSED && s.voice == ScreenState::VOICE_SCREEN_NONE,
         "tap released: «pressed» lets go");
  ScreenFrame f = s.tick(320, true, 0, true, true);
  expect(f.scene == OLED_SCENE_PRESSED && s.inFlight, "the scan press in flight");
  expect(at(s, 420).scene == OLED_SCENE_SEARCHING, "then searching, as 0.4.3");
  ScreenState held = {};
  held.onVoiceHold(0);
  held.onVoiceEnd(300, VOICE_CUE_NONE);
  expect(at(held, 300 + TAIL).scene == OLED_SCENE_OFF, "a tap whose press never shows still idles out");

  // A new hold while «thinking» or «speaking» starts a new utterance.
  ScreenState r = {};
  r.onWrite(0, payload(0), true);
  talkAndRelease(r, 0);
  r.onVoiceHold(5000);
  expect(at(r, 5000).scene == OLED_SCENE_PRESSED && r.voice == ScreenState::VOICE_SCREEN_HELD,
         "a hold while «thinking»: «pressed»");
  r.onVoiceStart(7000);
  expect(at(r, 7000).scene == OLED_SCENE_TALK, "then «speak»");
  r.onVoiceEnd(8000, VOICE_CUE_SENT);
  r.onWrite(9000, payload(SPEAK), true);
  at(r, 9000);
  r.onVoiceHold(12000);
  expect(at(r, 12000).scene == OLED_SCENE_PRESSED, "a hold while «speaking»: «pressed»");
  r.onWrite(12500, payload(SPEAK), true);
  expect(at(r, 12500).scene == OLED_SCENE_PRESSED, "the answer still playing never takes the glass from the hold");
  expect(at(r, 40000).scene == OLED_SCENE_PRESSED, "held, no fade");
  r.onVoiceStart(14000);  // START comes only right after the hold began: a late one is ignored
  expect(at(r, 40001).scene == OLED_SCENE_TALK, "START from the hold");
}

static void othersYield() {
  // A trusted wake bit, «found you» and the fall do not take the glass, and
  // a wake bit is not spent on the voice session.
  ScreenState s = {};
  s.onWrite(0, payload(0), true);
  s.onVoiceHold(100);
  s.onWrite(200, payload(SCREEN_FLAG_WAKE_NOW), true);
  expect(at(s, 200).scene == OLED_SCENE_PRESSED && !s.wakeUsed, "a wake bit while held: ignored, not spent");
  s.onFall(300);
  expect(at(s, 300).scene == OLED_SCENE_PRESSED, "a fall while held is dropped (the person is pressing)");
  ScreenFrame f = s.tick(400, false, 0, true, true, false, true);
  expect(f.scene == OLED_SCENE_PRESSED && !s.foundYouNow, "«found you» while held: no scene, no motor");
  s.onVoiceStart(2100);
  s.onVoiceEnd(3000, VOICE_CUE_SENT);
  f = s.tick(3100, false, 0, true, true, false, true);
  expect(f.scene == OLED_SCENE_THINKING && !s.foundYouNow, "«found you» while «thinking»: not over it");
  s.onWrite(3200, payload(SCREEN_FLAG_WAKE_NOW), true);
  expect(at(s, 3200).scene == OLED_SCENE_THINKING && !s.wakeUsed, "a wake bit while «thinking»: not over it");
  s.onFall(3300);
  expect(at(s, 3300).scene == OLED_SCENE_FELL && s.voice == ScreenState::VOICE_SCREEN_NONE,
         "a fall after the release plays (the button is free)");
  at(s, 3300 + 2680 + 1000);

  // The rate limits are untouched: right after a voice session the first
  // wake bit still wakes.
  ScreenState w = {};
  w.onWrite(0, payload(0), true);
  talkAndRelease(w, 0);
  at(w, 3000 + ScreenState::VOICE_REPLY_MS + ScreenState::FADE_MS);
  w.onWrite(20000, payload(SCREEN_FLAG_WAKE_NOW), true);
  expect(at(w, 20000).scene == OLED_SCENE_WATCH, "voice scenes are not counted in the wake rate limit");

  // Disabled (bit 3) keeps the panel dark, voice included.
  ScreenState d = {};
  d.onWrite(0, payload(SCREEN_FLAG_DISABLED), true);
  d.onVoiceHold(10);
  expect(at(d, 10).scene == OLED_SCENE_OFF, "disabled: dark while held too");
  // The bench demo and a press end the voice layer.
  ScreenState b = {};
  b.onVoiceHold(0);
  b.showDemo(10, OLED_SCENE_WATCH, 0, 0);
  expect(b.voice == ScreenState::VOICE_SCREEN_NONE && at(b, 10).scene == OLED_SCENE_WATCH, "a bench demo ends it");
}

static void wrap() {
  const uint32_t t0 = 0xFFFFFFFFu - 4000;
  ScreenState s = {};
  s.onWrite(t0, payload(0), true);
  talkAndRelease(s, t0);
  ScreenFrame f = at(s, t0 + 3000 + ScreenState::VOICE_REPLY_MS - 1);
  expect(f.scene == OLED_SCENE_THINKING && f.contrast == ScreenState::CONTRAST_NORMAL, "«thinking» 10 s across the wrap");
  s.onWrite(t0 + 13000, payload(THINK), true);
  f = at(s, t0 + 13000 + ScreenState::VOICE_REPLY_MS - 1);
  expect(f.scene == OLED_SCENE_THINKING && f.contrast == ScreenState::CONTRAST_NORMAL, "bit 6 extends it across the wrap");
  s.onWrite(t0 + 20000, payload(SPEAK), true);
  at(s, t0 + 20000);
  f = at(s, t0 + 20000 + SPEAKING_HOLD_MAX_MS - 1);
  expect(f.scene == OLED_SCENE_SPEAKING && f.contrast == ScreenState::CONTRAST_NORMAL, "the answer's 60 s across the wrap");
  at(s, t0 + 20000 + SPEAKING_HOLD_MAX_MS);
  expect(at(s, t0 + 20000 + SPEAKING_HOLD_MAX_MS + TAIL).scene == OLED_SCENE_OFF, "and its end");
}

int main() {
  holdFlow(1000, "from 1 s");
  holdFlow(0xFFFFFFFFu - 30000, "across the wrap");
  answer();
  thinkingBounds();
  failuresAndAcks();
  tapAndRepeats();
  othersYield();
  lateStop();
  phonePressDuringHold();
  wrap();
  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("all screen voice tests passed\n");
  return 0;
}

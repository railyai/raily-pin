// Host tests for voice_gesture.h, with the uplink of voice_ring.h behind it
// (docs/pins/voice-from-pin.md §1, §10 V1, and the 2026-10-09 deltas).
// run_tests.sh builds this twice: with the default 2 s talk threshold (the
// owner's 2026-10-09 choice, keeping 0.3.0's under-2 s scan) and with
// -DRAILY_VOICE_TALK_HOLD_MS=400 (the doc's original 0.4 s); every timing
// below is derived from VOICE_TALK_HOLD_MS.
#include <stdio.h>
#include <string.h>

#include "../RailyPinsP1/idle_sleep.h"
#include "../RailyPinsP1/voice_gesture.h"
#include "../RailyPinsP1/voice_ring.h"

static int failures = 0;

static void expect(bool ok, const char* what) {
  printf("%s [talk %u ms] %s\n", ok ? "PASS" : "FAIL", (unsigned)VOICE_TALK_HOLD_MS, what);
  if (!ok) failures++;
}

static const uint32_t T = VOICE_TALK_HOLD_MS;
static const uint32_t L = VOICE_LONG_HOLD_MS;

static const VoiceLink ARMED = {true, true, true, true};
static const VoiceLink CONNECTED_NOT_SUBSCRIBED = {true, true, false, true};
static const VoiceLink NO_PHONE = {true, false, false, true};
static const VoiceLink UNOWNED_NO_PHONE = {true, false, false, false};
static const VoiceLink UNOWNED_CONNECTED = {true, true, false, false};
static const VoiceLink NO_MIC_CONNECTED = {false, true, true, true};  // armed is meaningless without a mic
static const VoiceLink NO_MIC_NO_PHONE = {false, false, false, true};

static bool is(const VoiceGestureOut& out, uint16_t actions) { return out.actions == actions; }

// A whole press: began at `at`, released `heldMs` later, with ticks every
// 20 ms in between (as loop() would). Returns the OR of every action; `cue`
// gets the one cue the press ended with (VOICE_CUE_NONE if none).
static uint16_t press(const VoiceLink& link, uint32_t at, uint32_t heldMs, uint8_t* endReason = 0,
                      uint8_t* cue = 0) {
  VoiceGesture g = voiceGesture();
  uint8_t cues = 0, last = VOICE_CUE_NONE;
  const VoiceGestureOut began = g.holdBegan(at, link);
  uint16_t all = began.actions;
  if (began.cue != VOICE_CUE_NONE) cues++;
  for (uint32_t t = 20; t < heldMs; t += 20) {
    const VoiceGestureOut out = g.tick(at + t, link);
    all |= out.actions;
    if (out.has(VOICE_ACT_SEND_END) && endReason) *endReason = out.endReason;
    if (out.cue != VOICE_CUE_NONE) {
      cues++;
      last = out.cue;
    }
  }
  const VoiceGestureOut out = g.holdEnded(at + heldMs, link);
  if (out.has(VOICE_ACT_SEND_END) && endReason) *endReason = out.endReason;
  if (out.cue != VOICE_CUE_NONE) {
    cues++;
    last = out.cue;
  }
  if (cue) *cue = cues <= 1 ? last : 0xFF;  // 0xFF: more than one cue for one press
  return all | out.actions;
}

static uint8_t cueOf(const VoiceLink& link, uint32_t heldMs) {
  uint8_t cue = 0xFE;
  press(link, 0, heldMs, 0, &cue);
  return cue;
}

static void testConstants() {
  expect(VOICE_LONG_HOLD_MS == BUTTON_LONG_MS, "the 2 s long hold is the shipped button's (idle_sleep.h)");
  expect(T > 0 && T <= L, "the talk threshold is within the shipped scan window");
  expect(voiceGesture().talkHoldMs == T, "voiceGesture() takes the named constant");
}

static void testTap() {
  VoiceGesture g = voiceGesture();
  expect(is(g.holdBegan(1000, ARMED), VOICE_ACT_MIC_ON | VOICE_ACT_SEND_PRESS) && g.micOn(),
         "armed press-down: mic on, PRESS (pre-roll starts, nothing else leaves)");
  expect(is(g.tick(1000 + T - 1, ARMED), VOICE_ACT_NONE), "no START before the threshold");
  expect(is(g.holdEnded(1000 + T - 1, ARMED), VOICE_ACT_MIC_OFF | VOICE_ACT_DROP_AUDIO | VOICE_ACT_SCAN) && !g.micOn(),
         "armed tap: mic off, pre-roll wiped, scan on release");

  expect(press(CONNECTED_NOT_SUBSCRIBED, 0, T - 1) == VOICE_ACT_SCAN, "tap, voice not on for this phone: scan only");
  expect(press(NO_PHONE, 0, T - 1) == VOICE_ACT_SCAN, "tap with no phone: scan on release (as 0.3.0)");
  expect(press(NO_MIC_NO_PHONE, 0, 30) == VOICE_ACT_SCAN, "tap on a board without a mic: scan");

  VoiceGesture unarmed = voiceGesture();
  expect(is(unarmed.holdBegan(0, CONNECTED_NOT_SUBSCRIBED), VOICE_ACT_NONE) && !unarmed.micOn(),
         "unarmed press-down: no PRESS, the mic stays off");
  expect(is(unarmed.tick(T, CONNECTED_NOT_SUBSCRIBED), VOICE_ACT_NONE), "unarmed hold: no START at the threshold");
}

static void testTalk() {
  VoiceGesture g = voiceGesture();
  g.holdBegan(5000, ARMED);
  expect(g.nextDeadlineMs(5000) == T, "the next deadline is the threshold");
  expect(is(g.tick(5000 + T, ARMED), VOICE_ACT_SEND_START), "the threshold: START (and its one tick)");
  expect(is(g.tick(5000 + T + 1, ARMED), VOICE_ACT_NONE), "START only once");
  expect(g.nextDeadlineMs(5000 + T) == VOICE_UTTERANCE_CAP_MS - T, "then the next deadline is the 60 s cap");
  expect(is(g.tick(5000 + L + 500, ARMED), VOICE_ACT_NONE), "an armed hold past 2 s keeps talking: no error");
  const VoiceGestureOut end = g.holdEnded(5000 + L + 600, ARMED);
  expect(is(end, VOICE_ACT_MIC_OFF | VOICE_ACT_SEND_END) && end.endReason == VOICE_END_RELEASED && !g.micOn(),
         "release: mic off, END 0, no scan");
  expect(end.cue == VOICE_CUE_SENT, "release: the SENT cue (▮ and «thinking»)");
  expect(g.nextDeadlineMs(5000 + L + 600) == VOICE_NO_DEADLINE, "idle: no deadline");

  // loop() late: the release arrives before the tick that would have started.
  VoiceGesture late = voiceGesture();
  late.holdBegan(0, ARMED);
  const VoiceGestureOut both = late.holdEnded(T + 50, ARMED);
  expect(is(both, VOICE_ACT_SEND_START | VOICE_ACT_MIC_OFF | VOICE_ACT_SEND_END) && both.endReason == 0,
         "a late release past the threshold is a talk (START then END), not a scan");
}

static void testCues() {
  expect(cueOf(ARMED, T - 1) == VOICE_CUE_NONE && cueOf(NO_PHONE, 30) == VOICE_CUE_NONE,
         "a tap that scans has no cue: the scan's own screen follows");
  expect(cueOf(ARMED, L + 500) == VOICE_CUE_SENT, "a talk released: SENT");
  expect(cueOf(ARMED, VOICE_UTTERANCE_CAP_MS + 500) == VOICE_CUE_SENT, "a talk ended by the 60 s cap: SENT, once");
  expect(cueOf(NO_PHONE, L) == VOICE_CUE_NO_LINK && cueOf(NO_PHONE, L + 3000) == VOICE_CUE_NO_LINK,
         "2 s on a voice pin with no phone: «no link» (0.4.4)");
  expect(cueOf(CONNECTED_NOT_SUBSCRIBED, L) == VOICE_CUE_DIDNT_WORK,
         "2 s connected but voice not on: «didn't work», no motor (0.3.0)");
  expect(cueOf(NO_MIC_CONNECTED, L) == VOICE_CUE_DIDNT_WORK && cueOf(NO_MIC_NO_PHONE, L) == VOICE_CUE_DIDNT_WORK,
         "2 s on a board without a mic: «didn't work», with or without a phone (0.3.0)");
  if (T < L) {
    expect(cueOf(NO_PHONE, T) == VOICE_CUE_NO_LINK, "a talk hold under 2 s with no phone: «no link»");
  }
  VoiceGesture lost = voiceGesture();
  lost.holdBegan(0, ARMED);
  lost.tick(T, ARMED);
  const VoiceGestureOut dropped = lost.tick(T + 40, NO_PHONE);
  expect(dropped.cue == VOICE_CUE_NO_LINK && dropped.has(VOICE_ACT_FAIL), "the link lost mid-talk: «no link»");
  VoiceGesture unsub = voiceGesture();
  unsub.holdBegan(0, ARMED);
  expect(unsub.tick(T / 2, CONNECTED_NOT_SUBSCRIBED).cue == VOICE_CUE_NO_LINK,
         "unsubscribed in the pre-roll: «no link» (the link no longer carries voice)");

  for (uint8_t reason = VOICE_END_OVERFLOW; reason <= VOICE_END_LOW_BATTERY; reason++) {
    VoiceGesture g = voiceGesture();
    g.holdBegan(0, ARMED);
    g.tick(T, ARMED);
    const VoiceGestureOut out = g.stop(reason);
    expect(out.cue == VOICE_CUE_TALK_FAILED && out.has(VOICE_ACT_FAIL) && out.endReason == reason,
           "the ring full or low battery: a failed talk (▬▬▬), END 2 / 3");
  }
  for (uint8_t why = 0; why < 6; why++) {
    VoiceGesture g = voiceGesture();
    g.holdBegan(0, ARMED);
    g.tick(T, ARMED);
    const VoiceGestureOut out = g.stop(VOICE_END_PHONE_STOP, why);
    const bool spent = why == VOICE_STOP_MINUTES_SPENT;
    expect(out.endReason == VOICE_END_PHONE_STOP && out.has(VOICE_ACT_SEND_END) &&
               out.cue == (spent ? VOICE_CUE_SEE_PHONE : VOICE_CUE_TALK_FAILED) &&
               out.has(VOICE_ACT_FAIL) == !spent,
           spent ? "phone STOP 1 (minutes spent): «see the phone» (▬), END 4, no «didn't work»"
                 : "phone STOP 0, 2-5: a failed talk (▬▬▬), END 4");
  }
  VoiceGesture early = voiceGesture();
  early.holdBegan(0, ARMED);
  const VoiceGestureOut spentEarly = early.stop(VOICE_END_PHONE_STOP, VOICE_STOP_MINUTES_SPENT);
  expect(is(spentEarly, VOICE_ACT_MIC_OFF | VOICE_ACT_DROP_AUDIO) && spentEarly.cue == VOICE_CUE_SEE_PHONE,
         "phone STOP 1 in the pre-roll: the pre-roll dropped, «see the phone»");
  VoiceGesture tap = voiceGesture();
  expect(tap.holdBegan(0, ARMED).cue == VOICE_CUE_NONE && tap.tick(T, ARMED).cue == VOICE_CUE_NONE,
         "press-down and START carry no cue");
}

// An unowned pin keeps 0.4.3: «didn't work», no motor, never «no link».
static void testUnowned() {
  expect(cueOf(UNOWNED_NO_PHONE, L) == VOICE_CUE_DIDNT_WORK && cueOf(UNOWNED_NO_PHONE, L + 3000) == VOICE_CUE_DIDNT_WORK,
         "2 s unowned with no phone: «didn't work», not «no link»");
  expect(press(UNOWNED_NO_PHONE, 0, L) == VOICE_ACT_FAIL, "and no scan");
  expect(cueOf(UNOWNED_CONNECTED, L) == VOICE_CUE_DIDNT_WORK, "2 s unowned and connected: «didn't work»");
  expect(cueOf(UNOWNED_NO_PHONE, T - 1) == VOICE_CUE_NONE && press(UNOWNED_NO_PHONE, 0, T - 1) == VOICE_ACT_SCAN,
         "a tap on an unowned pin still scans");
  if (T < L) {
    expect(cueOf(UNOWNED_NO_PHONE, T) == VOICE_CUE_DIDNT_WORK && press(UNOWNED_NO_PHONE, 0, T) == VOICE_ACT_FAIL,
           "a talk hold under 2 s, unowned with no phone: «didn't work», no scan (0.4.3)");
  }
}

// A STOP: live, after the release, or dropped.
static void testStopRoute() {
  const uint8_t id = 7;
  const uint16_t forLive = (uint16_t)(id + 1);
  expect(voiceStopRoute(forLive, id, true, false, false) == VOICE_STOP_LIVE, "STOP for the live utterance, mic on: ends the talk");
  expect(voiceStopRoute(forLive, id, false, true, false) == VOICE_STOP_LATE, "after the release, the reply awaited: the outcome");
  expect(voiceStopRoute(forLive, id, false, false, false) == VOICE_STOP_DROP, "idle, no reply awaited: dropped");
  expect(voiceStopRoute(0, id, true, true, false) == VOICE_STOP_DROP, "no STOP: nothing");
  expect(voiceStopRoute((uint16_t)(id + 2), id, true, false, false) == VOICE_STOP_DROP &&
             voiceStopRoute((uint16_t)(id + 2), id, false, true, false) == VOICE_STOP_DROP,
         "a STOP for another utterance is dropped, live or late");
  expect(voiceStopRoute(forLive, id, true, false, true) == VOICE_STOP_DROP &&
             voiceStopRoute(forLive, id, false, true, true) == VOICE_STOP_DROP,
         "a bench hold on the serial sink takes no STOP");
  expect(voiceStopRoute(256, 255, false, true, false) == VOICE_STOP_LATE, "utterance 255 (stored as 256) matches");
  expect(voiceStopCue(VOICE_STOP_MINUTES_SPENT) == VOICE_CUE_SEE_PHONE, "late STOP 1: «see the phone»");
  bool failed = true;
  const uint8_t others[] = {0, 2, 3, 4, 5};
  for (uint8_t why : others) failed = failed && voiceStopCue(why) == VOICE_CUE_TALK_FAILED;
  expect(failed, "late STOP, any other reason: a failed talk (▬▬▬)");
}

static void testUnarmedHolds() {
  if (T < L) {
    expect(press(CONNECTED_NOT_SUBSCRIBED, 0, T) == VOICE_ACT_SCAN && press(CONNECTED_NOT_SUBSCRIBED, 0, L - 1) == VOICE_ACT_SCAN,
           "hold to 2 s, connected but voice not on: scan on release");
    expect(press(NO_PHONE, 0, T) == VOICE_ACT_FAIL && press(NO_PHONE, 0, L - 1) == VOICE_ACT_FAIL,
           "talk hold on a voice pin with no phone: «didn't work», no scan");
    expect(press(NO_MIC_NO_PHONE, 0, T) == VOICE_ACT_SCAN && press(NO_MIC_CONNECTED, 0, L - 1) == VOICE_ACT_SCAN,
           "hold under 2 s on a board without a mic: scan (0.3.0)");
  } else {
    expect(press(NO_PHONE, 0, L - 1) == VOICE_ACT_SCAN && press(CONNECTED_NOT_SUBSCRIBED, 0, L - 1) == VOICE_ACT_SCAN,
           "talk at 2 s: every unarmed press under 2 s scans, exactly 0.3.0");
  }
  expect(press(CONNECTED_NOT_SUBSCRIBED, 0, L) == VOICE_ACT_FAIL, "2 s unarmed, connected: «didn't work», no pairing window");
  expect(press(NO_PHONE, 0, L + 3000) == VOICE_ACT_FAIL, "2 s with no phone: «didn't work», no scan");
  expect(press(NO_MIC_CONNECTED, 0, L) == VOICE_ACT_FAIL, "2 s on a board without a mic: «didn't work»");
}

static void testCapAndStops() {
  uint8_t reason = 0xFF;
  const uint16_t all = press(ARMED, 100, VOICE_UTTERANCE_CAP_MS + 5000, &reason);
  expect((all & VOICE_ACT_SEND_END) && !(all & VOICE_ACT_FAIL) && reason == VOICE_END_CAP && !(all & VOICE_ACT_SCAN),
         "the 60 s cap: END 1, a normal end (0.4.4: no «didn't work»), no scan");

  VoiceGesture g = voiceGesture();
  g.holdBegan(0, ARMED);
  g.tick(T, ARMED);
  expect(is(g.tick(VOICE_UTTERANCE_CAP_MS - 1, ARMED), VOICE_ACT_NONE), "no cap before 60 s");
  const VoiceGestureOut capped = g.tick(VOICE_UTTERANCE_CAP_MS, ARMED);
  expect(is(capped, VOICE_ACT_MIC_OFF | VOICE_ACT_SEND_END) && capped.endReason == VOICE_END_CAP &&
             capped.cue == VOICE_CUE_SENT && !g.micOn() && g.held(),
         "the cap at exactly 60 s from press-down: mic off while still held");
  expect(is(g.holdEnded(VOICE_UTTERANCE_CAP_MS + 10, ARMED), VOICE_ACT_NONE), "the release after the cap does nothing");

  VoiceGesture s = voiceGesture();
  s.holdBegan(0, ARMED);
  s.tick(T, ARMED);
  const VoiceGestureOut stopped = s.stop(VOICE_END_PHONE_STOP);
  expect(is(stopped, VOICE_ACT_MIC_OFF | VOICE_ACT_SEND_END | VOICE_ACT_FAIL) && stopped.endReason == 4,
         "phone STOP while talking: END 4, mic off");
  expect(is(s.stop(VOICE_END_PHONE_STOP), VOICE_ACT_NONE) && is(s.holdEnded(T + 10, ARMED), VOICE_ACT_NONE),
         "a second STOP and the release do nothing");

  VoiceGesture p = voiceGesture();
  p.holdBegan(0, ARMED);
  expect(is(p.stop(VOICE_END_OVERFLOW), VOICE_ACT_MIC_OFF | VOICE_ACT_DROP_AUDIO | VOICE_ACT_FAIL),
         "a stop before START drops the pre-roll: there is no utterance to end");
  VoiceGesture idle = voiceGesture();
  expect(is(idle.stop(VOICE_END_PHONE_STOP), VOICE_ACT_NONE) && !idle.micOn(), "STOP while idle never turns the mic on");
}

static void testLinkLoss() {
  VoiceGesture pre = voiceGesture();
  pre.holdBegan(0, ARMED);
  expect(is(pre.tick(T / 2, NO_PHONE), VOICE_ACT_MIC_OFF | VOICE_ACT_DROP_AUDIO | VOICE_ACT_FAIL) && !pre.micOn(),
         "disconnect in the pre-roll: mic off, buffers cleared");
  expect(is(pre.holdEnded(T + 1, ARMED), VOICE_ACT_NONE), "no scan and no START after that, even if the phone is back");

  VoiceGesture talk = voiceGesture();
  talk.holdBegan(0, ARMED);
  talk.tick(T, ARMED);
  expect(is(talk.tick(T + 100, CONNECTED_NOT_SUBSCRIBED), VOICE_ACT_MIC_OFF | VOICE_ACT_DROP_AUDIO | VOICE_ACT_FAIL),
         "unsubscribed mid-talk: mic off, buffers cleared, no END to nobody");
  expect(is(talk.tick(T + 200, ARMED), VOICE_ACT_NONE) && !talk.micOn(), "a reconnect mid-hold never reopens the mic");

  VoiceGesture endsUnarmed = voiceGesture();
  endsUnarmed.holdBegan(0, ARMED);
  expect(is(endsUnarmed.holdEnded(T / 2, NO_PHONE), VOICE_ACT_MIC_OFF | VOICE_ACT_DROP_AUDIO | VOICE_ACT_FAIL),
         "a tap whose link died before the release: «didn't work», no scan");
}

static void testWrapAndRepeats() {
  const uint32_t at = 0xFFFFFFFFu - T / 2;
  VoiceGesture g = voiceGesture();
  g.holdBegan(at, ARMED);
  expect(is(g.tick(at + T - 1, ARMED), VOICE_ACT_NONE), "no early START across the millis() wrap");
  expect(is(g.tick(at + T, ARMED), VOICE_ACT_SEND_START), "START on time across the wrap");
  expect(is(g.tick(at + VOICE_UTTERANCE_CAP_MS - 1, ARMED), VOICE_ACT_NONE) &&
             g.tick(at + VOICE_UTTERANCE_CAP_MS, ARMED).has(VOICE_ACT_SEND_END),
         "the cap on time across the wrap");
  expect(press(CONNECTED_NOT_SUBSCRIBED, 0xFFFFFFFFu - 10, L) == VOICE_ACT_FAIL &&
             press(CONNECTED_NOT_SUBSCRIBED, 0xFFFFFFFFu - 10, T - 1) == VOICE_ACT_SCAN,
         "unarmed holds measured across the wrap");

  VoiceGesture r = voiceGesture();
  r.holdBegan(0, ARMED);
  expect(is(r.holdBegan(10, ARMED), VOICE_ACT_NONE), "a repeated began is ignored (one PRESS per hold)");
  r.holdEnded(20, ARMED);
  expect(is(r.holdEnded(30, ARMED), VOICE_ACT_NONE), "a repeated ended is ignored (one scan per hold)");
}

// ---- Property test: gesture + uplink under random input --------------------

static uint32_t lcgState = 0x1234567u;
static uint32_t rnd(uint32_t n) {
  lcgState = lcgState * 1103515245u + 12345u;
  return (lcgState >> 8) % n;
}

typedef VoiceUplink<8> SmallUplink;  // small, so the ring overflows often

struct StreamCheck {
  bool cut;           // the uplink was reset (link lost) or gave older audio up
  bool open;          // a PRESS was seen for `utterance`
  bool started;
  bool ended;
  uint8_t utterance;
  uint16_t lastSeq;
  uint16_t audio;
  bool ok;

  void see(const uint8_t* payload, uint8_t length) {
    VoiceFrame f;
    if (!voiceParseFrame(payload, length, &f)) {
      ok = false;
      return;
    }
    if (f.kind == VOICE_KIND_PRESS) {
      if (open && f.utterance == utterance) ok = false;  // a new press always has a new id
      if (open && started && !ended && !cut) ok = false;  // never cut short but by a reset
      open = true;
      cut = false;
      started = ended = false;
      utterance = f.utterance;
      lastSeq = 0;
      audio = 0;
      return;
    }
    if (!open || f.utterance != utterance || ended) {
      ok = false;  // nothing outside an open utterance, nothing after its END
      return;
    }
    if (f.kind == VOICE_KIND_START) {
      if (started || f.seq != 0) ok = false;
      started = true;
    } else if (f.kind == VOICE_KIND_AUDIO) {
      if (!started || f.seq != lastSeq + 1) ok = false;  // no audio before START, no gap
      lastSeq = f.seq;
      audio++;
    } else if (f.kind == VOICE_KIND_END) {
      if (!started || f.seq != lastSeq + 1 || f.framesSent != audio) ok = false;
      if ((f.framesDropped != 0) != (f.reason == VOICE_END_OVERFLOW)) ok = false;
      ended = true;
    }
  }
};

static uint32_t givenUp = 0;

static void apply(const VoiceGestureOut& out, SmallUplink& up, StreamCheck& check) {
  if (out.has(VOICE_ACT_SEND_PRESS) && !up.press()) {
    givenUp++;
    check.cut = true;
  }
  if (out.has(VOICE_ACT_SEND_START)) up.start(40);
  if (out.has(VOICE_ACT_DROP_AUDIO)) up.drop();
  if (out.has(VOICE_ACT_SEND_END)) up.finish(out.endReason);
}

static void testProperty() {
  static SmallUplink up;
  memset(&up, 0, sizeof(up));
  VoiceGesture g = voiceGesture();
  StreamCheck check = {false, false, false, false, 0, 0, 0, true};
  VoiceLink link = ARMED;
  bool down = false;
  uint32_t now = 0xFFFFFFFFu - 200000;  // crosses the millis() wrap
  int16_t pcm[VOICE_FRAME_SAMPLES];
  memset(pcm, 0, sizeof(pcm));
  bool micOnlyWhileDown = true, micOnOnlyFromArmedBegan = true, uplinkFollowsMic = true, scansOnRelease = true;
  uint32_t starts = 0, ends = 0, overflows = 0, scans = 0, fails = 0;
  for (int step = 0; step < 100000; step++) {
    const bool micBefore = g.micOn();
    VoiceGestureOut out = {0, 0, 0};
    const uint32_t event = rnd(100);
    bool fromBegan = false, fromEnded = false;
    if (event < 8) {  // the button (sometimes a repeated edge)
      if (!down || rnd(10) == 0) {
        out = g.holdBegan(now, link);
        fromBegan = true;
        down = true;
      } else {
        out = g.holdEnded(now, link);
        fromEnded = true;
        down = false;
      }
    } else if (event < 12) {  // the link changes; loop() ticks at once
      link.voiceCapable = rnd(8) != 0;
      link.linkUp = rnd(4) != 0;
      link.armed = rnd(3) != 0;
      if (!link.canTalk()) {  // lost or disarmed: nothing stale after a reconnect
        up.reset();
        check.cut = true;
      }
      out = g.tick(now, link);
    } else if (event < 14) {  // the phone writes STOP, or the battery is low
      out = g.stop(rnd(2) ? VOICE_END_PHONE_STOP : VOICE_END_LOW_BATTERY);
    } else if (event < 60) {  // time passes, loop() ticks
      now += rnd(10) == 0 ? rnd(70000) : rnd(60);
      out = g.tick(now, link);
    } else if (event < 85) {  // the mic delivers a frame
      if (g.micOn() && !up.capture(pcm)) {
        overflows++;
        out = g.stop(VOICE_END_OVERFLOW);
      }
    } else {  // TX credits: notify what is queued
      uint8_t payload[VOICE_AUDIO_BYTES];
      for (uint32_t n = rnd(4); n > 0 && link.linkUp && link.armed; n--) {
        const uint8_t length = up.peek(payload);
        if (length == 0) break;
        check.see(payload, length);
        up.consume();
      }
    }
    apply(out, up, check);
    if (out.has(VOICE_ACT_MIC_ON) && !(fromBegan && link.canTalk())) micOnOnlyFromArmedBegan = false;
    if (out.has(VOICE_ACT_SCAN) && !fromEnded) scansOnRelease = false;
    if (g.micOn() && !down) micOnlyWhileDown = false;
    if (g.micOn() != up.capturing()) uplinkFollowsMic = false;
    if (g.micOn() != ((micBefore && !out.has(VOICE_ACT_MIC_OFF)) || out.has(VOICE_ACT_MIC_ON))) uplinkFollowsMic = false;
    starts += out.has(VOICE_ACT_SEND_START);
    ends += out.has(VOICE_ACT_SEND_END);
    scans += out.has(VOICE_ACT_SCAN);
    fails += out.has(VOICE_ACT_FAIL);
  }
  expect(micOnlyWhileDown, "property: the mic is on only while the hold is down");
  expect(micOnOnlyFromArmedBegan, "property: only an armed hold-began turns the mic on (never a tick, link change or STOP)");
  expect(scansOnRelease, "property: a scan only ever comes on a release");
  expect(uplinkFollowsMic, "property: the uplink captures exactly while the mic is on");
  expect(check.ok,
         "property: the stream is PRESS, START, AUDIO 1..n with no gap, END n + 1, nothing after; an utterance is "
         "never cut short by the next hold");
  printf("  (property run: %u talks, %u overflows, %u older utterances given up)\n", (unsigned)starts,
         (unsigned)overflows, (unsigned)givenUp);
  expect(starts > 100 && ends > 100 && overflows > 10 && scans > 100 && fails > 100,
         "property: the run reached every path (talks, ends, overflows, scans, errors)");
}

int main() {
  testConstants();
  testTap();
  testTalk();
  testUnarmedHolds();
  testCues();
  testUnowned();
  testStopRoute();
  testCapAndStops();
  testLinkLoss();
  testWrapAndRepeats();
  testProperty();
  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("voice_gesture [talk %u ms]: all passed\n", (unsigned)VOICE_TALK_HOLD_MS);
  return 0;
}

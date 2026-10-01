#pragma once

#include <stdint.h>
#include "oled_assets.h"
#include "screen_payload.h"

// What the keyring OLED shows right now (docs/pins/keyring-oled.md §9-§13).
// contrast 0 means the panel sleeps (SSD1306 power save).
struct ScreenFrame {
  uint8_t scene;     // OLED_SCENE_*
  uint8_t frame;     // animation frame inside the scene
  uint8_t shape;     // OLED_SHAPE_*
  uint8_t material;  // OLED_MATERIAL_*
  bool linked;       // status row link icon
  int8_t shiftX;     // wake shift of the mascot and glyph zone (burn-in); a still scene ignores it
  int8_t shiftY;
  uint8_t contrast;
  uint8_t locale;    // word strips: 1 ru, 2 en, 3 es, 4 pt; 0 icons only
  bool modest;       // the mascot and its pose only (spec §15)
  uint8_t count;     // the number a count scene draws: 1-99, 0 none (withheld or a self-wake)
  bool dot;          // the status row's unseen-notification dot, lit this second (spec §12)

  bool sameImage(const ScreenFrame& o) const {
    return scene == o.scene && frame == o.frame && shape == o.shape && material == o.material &&
           linked == o.linked && shiftX == o.shiftX && shiftY == o.shiftY && locale == o.locale &&
           modest == o.modest && count == o.count && dot == o.dot;
  }
};

static inline bool screenPayloadSame(const ScreenPayload& a, const ScreenPayload& b) {
  return a.version == b.version && a.agent == b.agent && a.found == b.found && a.looking == b.looking &&
         a.notify == b.notify && a.notifyCount == b.notifyCount && a.flags == b.flags && a.mascot == b.mascot &&
         a.locale == b.locale;
}

// Pure, bounded screen state machine, the same shape as FeedbackState: BLE
// callbacks only publish (the press flag, the event_ack byte, the accepted
// screen_state payload through a mailbox); every decision and every I2C
// write happens in loop(). Not thread-safe: loop() only.
//
// Two layers:
// - the press (PR 1, spec §11): the pin's own press, the link, its owned
//   flag and the latest event_ack byte (1 press accepted, 2 phone received,
//   3 result, 4 error; latest wins);
// - screen_state 7B1E0014 (PR 2/3, spec §10): the agent's state, modest,
//   disabled, quiet hours, speaking, the mascot and the word locale. The
//   agent scenes show on a wake that is not a press: a trusted wake bit
//   (at most once a minute), the bounded speaking wake, and the manual
//   wake of gestures S1 (wake()). An agent scene on the glass follows every
//   accepted write; only a trusted write that changes the payload restarts
//   the 6 s idle timer, so a plaintext writer can repaint an awake screen
//   but never keep it lit.
// And the fall (PR 5, onFall()): a one-off that wakes the screen itself.
// «Found you» (event_ack 0x11, onFoundYou()): someone nearby found this
// person; the one self-wake that also vibrates (owner, 2026-09-28).
// The agent's counts (found, looking) show on an agent wake, only from a
// trusted write (the plaintext strip and the stale reset zero them), and
// their digits only on a wake the person asked for (spec §15): a self-wake
// shows the icon without a number (docs/pins/firmware.md «Counts»).
// Notifications (notify 1-7, «Notifications» there) come the same way:
// the kind's scene on an agent wake, and a blinking status-row dot on any
// awake scene until the phone reports them seen (notify 0).
// Unsigned subtraction keeps every timer right through the 32-bit millis
// rollover.
struct ScreenState {
  // The press layer.
  uint8_t scene;
  uint32_t sceneAtMs;     // last scene change: the animation starts here
  uint32_t idleAtMs;      // the 6 s idle timer starts here
  uint32_t pressAtMs;     // last press: in-flight deadline and contrast burst
  uint32_t holdMs;        // how long the scene stays before the fade
  uint32_t wakes;         // counts off -> on, picks the wake shift
  uint8_t demoShape;
  uint8_t demoMaterial;
  uint8_t demoLocale;
  bool demoModest;
  bool inFlight;          // a press waits for ack 3 / 4 or its timeout
  bool boosting;          // full contrast right after a press
  bool demo;              // serial `o` bench scene
  // The screen_state layer.
  ScreenPayload payload;  // the last accepted write, after the plaintext strip
  bool hasPayload;
  bool payloadTrusted;    // it came over an encrypted link
  bool stale;             // 30 min without a write: reset to «nothing known»
  uint32_t lastWriteMs;
  bool wakeUsed;
  uint32_t lastWakeMs;    // the last honoured wake bit
  SpeakingWake speaking;
  bool speakingHeld;      // a speaking wake holds the agent scene awake
  uint8_t mascot;         // the look byte: shape << 4 | material
  bool owned;
  bool agentShown;        // the scene on the glass is an agent scene
  bool askedWake;         // the person woke it (wake()): counts may show their digits
  bool linkWasUp;
  bool foundYouUsed;
  uint32_t lastFoundYouMs;  // the last honoured «found you»
  bool foundYouNow;       // this pass honoured one: loop() lets the motor play ▬ ▬

  static const uint32_t PRESSED_MS = 100;         // squash frame, then the radar (spec §11)
  static const uint32_t FLIGHT_TIMEOUT_MS = 10000;
  static const uint32_t IDLE_MS = 6000;           // after the last state change (spec §13)
  static const uint32_t NO_LINK_MS = 3000;
  static const uint32_t DEMO_MS = 30000;          // long enough for a bench photo or video
  static const uint32_t FADE_MS = 1000;
  static const uint32_t BOOST_MS = 1000;
  static const uint32_t STALE_MS = 30UL * 60UL * 1000UL;  // spec §10 «Nothing known»
  static const uint32_t WAKE_INTERVAL_MS = 60000;         // spec §10 «Wake rate limit»
  static const uint32_t FOUND_YOU_INTERVAL_MS = 60000;    // spec §10 «Found you»
  static const uint8_t ACK_FOUND_YOU = 0x11;              // rhythm.h RHYTHM_FOUND
  static const uint32_t DOT_PERIOD_MS = 4000;             // spec §12: the dot blinks 1 s in 4
  static const uint32_t DOT_ON_MS = 1000;
  static const uint8_t DEMO_FOUND = 3;                    // the pack's drawn counts (k05, a03)
  static const uint8_t DEMO_LOOKING = 5;
  static const uint8_t CONTRAST_NORMAL = 0x20;
  static const uint8_t CONTRAST_PRESS = 0x7F;

  // press: a press was emitted to the phone this pass. pressFailed: a press
  // was dropped because its counter could not be saved (emitButtonEvent);
  // the pin knows that itself, so it shows «didn't work» at once instead of
  // searching for an answer that will never come. ownedNow: the server-pass
  // owned flag, read every pass; an unowned pin forgets the payload and the
  // mascot (spec §15).
  // foundYou: event_ack 0x11 arrived since the last pass (its own flag:
  // `ack` holds only the latest byte).
  ScreenFrame tick(uint32_t now, bool press, uint8_t ack, bool connected, bool ownedNow,
                   bool pressFailed = false, bool foundYou = false) {
    owned = ownedNow;
    foundYouNow = false;
    if (!owned && (hasPayload || mascot != 0)) release(now);
    if (linkWasUp && !connected) linkLost(now);
    linkWasUp = connected;
    if (press) onPress(now, connected, owned);
    if (pressFailed) onPressFailed(now);
    // An ack from the phone read in the same pass as a new press almost
    // surely answers the one before it (a result needs a server round
    // trip): the screen drops it, so a late «3» never resolves a press that
    // was just made. The LED still shows it.
    if (ack != 0 && !press && !pressFailed) onAck(now, ack);
    if (foundYou && !press && !pressFailed) onFoundYou(now);
    advance(now);
    return frameAt(now, connected);
  }

  // An accepted screen_state write (parseScreenPayload already validated it
  // and stripped it for a plaintext link). loop() calls it before tick(),
  // only for a write accepted while the pin was owned.
  void onWrite(uint32_t now, const ScreenPayload& p, bool trusted) {
    const bool changed = !hasPayload || stale || !screenPayloadSame(p, payload);
    payload = p;
    hasPayload = true;
    stale = false;
    payloadTrusted = trusted;
    lastWriteMs = now;  // every accepted write restarts the stale timer
    mascot = p.mascot;
    // Bit 5 wakes on its own bounded terms on any link (quiet hours do not
    // gate it: the person started the voice session).
    bool wake = speaking.onAcceptedWrite(now, p.hasFlag(SCREEN_FLAG_AGENT_SPEAKING));
    // Bit 0 arrives only on a trusted link (the plaintext strip clears it);
    // quiet hours win over it, and it is honoured once a minute at most.
    if (trusted && p.hasFlag(SCREEN_FLAG_WAKE_NOW) && !p.hasFlag(SCREEN_FLAG_QUIET_HOURS) &&
        (!wakeUsed || (uint32_t)(now - lastWakeMs) >= WAKE_INTERVAL_MS)) {
      wakeUsed = true;
      lastWakeMs = now;
      wake = true;
    }
    // Never over a press in flight, the bench or the fall: a wake that
    // lands in the fall's 3.7 s is dropped, the write itself is kept.
    if (demo || inFlight || falling()) return;
    if (wake) {
      wakeAgent(now, false);
    } else if (trusted && changed && agentShown && scene != OLED_SCENE_OFF) {
      idleAtMs = now;  // a trusted change keeps the screen up like any change (§13)
    }
  }

  // A manual wake (the IMU lift or tap of gestures S1): the agent scene, or
  // «nothing known» with no state. Nothing calls it yet on the pin. The
  // person asked, so the counts show their digits. A lift while the screen
  // is already awake changes nothing: a self-woken screen keeps its icons.
  void wake(uint32_t now) {
    if (scene != OLED_SCENE_OFF) return;
    wakeAgent(now, true);
  }

  // The pin was dropped (keyring-oled PR 5; gestures S1's gesture 3 calls
  // it through screenOnFall(), serial `of` on the bench): «Упал. Было
  // больно» plays once, ≈2.7 s, its last frame holds through the 1 s fade,
  // then the screen is off. The owner decided on 2026-09-28 that it wakes
  // the screen and does not vibrate: this is the screen only, no motor, no
  // radio. Disabled (bit 3) stays dark. A press in flight wins (the person
  // is pressing): the fall is dropped. Anything else on the glass gives
  // way: an agent scene, a press outcome, the bench demo. Quiet hours do
  // not stop it (they silence the motor and the self-wakes; this is the
  // person's own keyring). Modest draws the mascot alone (the composer);
  // the words follow the phone's locale like every scene.
  void onFall(uint32_t now) {
    if (disabled() || inFlight) return;
    demo = false;
    agentShown = false;
    boosting = false;  // a press outcome's full-contrast second ends here: the fall is normal contrast
    holdMs = sceneMs(OLED_SCENE_FELL);
    show(OLED_SCENE_FELL, now);
  }

  bool falling() const { return scene == OLED_SCENE_FELL; }

  // Unbind or release (and any unowned pass): the payload and the mascot
  // go, speaking stops. loop() runs it in the same pass as the release, and
  // the release erases the mascot record under passMutex.
  void release(uint32_t now) {
    payload = ScreenPayload();
    hasPayload = false;
    payloadTrusted = false;
    stale = false;
    mascot = 0;
    speaking.stop(now);
  }

  // The mascot byte the flash record kept (only an owned pin loads it).
  void restoreMascot(uint8_t value) {
    if ((value >> 4) < OLED_SHAPE_COUNT && (value & 0x0F) < OLED_MATERIAL_COUNT) mascot = value;
  }

  // Bench: serial `o<letter>[shape[material[locale]]]` (docs/pins/firmware.md).
  // Holds the scene DEMO_MS without a phone; a real press takes over.
  void showDemo(uint32_t now, uint8_t demoScene, uint8_t shape, uint8_t material, uint8_t locale = 0,
                bool modestLook = false) {
    if (demoScene >= OLED_SCENE_COUNT) return;
    inFlight = false;
    agentShown = false;
    demo = demoScene != OLED_SCENE_OFF;
    demoShape = shape < OLED_SHAPE_COUNT ? shape : OLED_SHAPE_PEBBLE;
    demoMaterial = material < OLED_MATERIAL_COUNT ? material : OLED_MATERIAL_SATIN;
    demoLocale = locale <= OLED_WORD_LOCALES ? locale : 0;
    demoModest = modestLook;
    holdMs = DEMO_MS;
    show(demoScene, now);
  }

  bool quietHours() const { return hasPayload && payload.hasFlag(SCREEN_FLAG_QUIET_HOURS); }
  bool disabled() const { return hasPayload && payload.hasFlag(SCREEN_FLAG_DISABLED); }
  bool modest() const { return hasPayload && payload.hasFlag(SCREEN_FLAG_MODEST); }
  uint8_t mascotByte() const { return mascot; }

  // What an agent wake shows now. Order: set me up (unowned), nothing
  // known, speaking, a notification, paused, needs setup, then the counts
  // (found before looking), then the agent's own state. A notification is
  // what woke it; a paused agent or one that needs setting up says so
  // before any count: that is what the person has to act on.
  uint8_t agentScene() const {
    if (!owned) return OLED_SCENE_SET_ME_UP;  // spec §15: only K9, default pebble
    if (!hasPayload) return OLED_SCENE_NOTHING_KNOWN;
    if (payload.hasFlag(SCREEN_FLAG_AGENT_SPEAKING)) return OLED_SCENE_SPEAKING;
    switch (payload.notify) {  // non-zero only after a trusted write
      case SCREEN_NOTIFY_NEW_FINDING: return OLED_SCENE_FOUND_SELF;  // the pack's a02: never a digit
      case SCREEN_NOTIFY_REQUEST_RECEIVED: return OLED_SCENE_REQUEST;
      case SCREEN_NOTIFY_REQUEST_ACCEPTED: return OLED_SCENE_ACCEPTED;
      case SCREEN_NOTIFY_AGENT_REPORT: return OLED_SCENE_REPORT;
      case SCREEN_NOTIFY_NEGOTIATION_REPORT: return OLED_SCENE_TALKS;
      case SCREEN_NOTIFY_DOOR_CLEAR: return OLED_SCENE_DOOR;
      case SCREEN_NOTIFY_AGENT_QUESTION: return OLED_SCENE_QUESTION;
      default: break;
    }
    if (payload.agent == SCREEN_AGENT_PAUSED) return OLED_SCENE_PAUSED;
    if (payload.agent == SCREEN_AGENT_SET_UP) return OLED_SCENE_NEEDS_SETUP;
    // found and looking are non-zero only after a trusted write: 1-99, or
    // SCREEN_COUNT_WITHHELD. A withheld or self-woken «found» is the icon.
    if (payload.found != 0) {
      return askedWake && payload.found <= OLED_COUNT_MAX ? OLED_SCENE_FOUND : OLED_SCENE_FOUND_SELF;
    }
    if (payload.looking != 0) return OLED_SCENE_LOOKING;
    switch (payload.agent) {
      case SCREEN_AGENT_WATCH: return OLED_SCENE_WATCH;
      case SCREEN_AGENT_SCANNING: return OLED_SCENE_SEARCHING;
      case SCREEN_AGENT_PAUSED: return OLED_SCENE_PAUSED;
      case SCREEN_AGENT_SET_UP: return OLED_SCENE_NEEDS_SETUP;
      default: return OLED_SCENE_NOTHING_KNOWN;  // unknown and stale: the mascot alone
    }
  }

  ScreenFrame frameAt(uint32_t now, bool connected) const {
    ScreenFrame f = {};
    f.scene = scene;
    if (scene == OLED_SCENE_OFF) return f;
    if (!demo && disabled()) {  // bit 3: the panel stays off, press included
      f.scene = OLED_SCENE_OFF;
      return f;
    }
    f.frame = animFrame((uint32_t)(now - sceneAtMs));
    uint8_t look = mascot;
    f.shape = demo ? demoShape : (uint8_t)(look >> 4);
    f.material = demo ? demoMaterial : (uint8_t)(look & 0x0F);
    if (f.shape >= OLED_SHAPE_COUNT) f.shape = OLED_SHAPE_PEBBLE;
    if (f.material >= OLED_MATERIAL_COUNT) f.material = OLED_MATERIAL_SATIN;
    f.locale = demo ? demoLocale : (hasPayload && payload.drawsWords() ? payload.locale : 0);
    f.modest = demo ? demoModest : modest();
    f.count = countFor(scene);
    // Unseen notifications: the status-row dot on any awake scene, 1 s in
    // every 4 from the scene's start, so the one static element never burns
    // in. The bench demo draws none.
    f.dot = !demo && hasPayload && payload.notify != 0 && (uint32_t)(now - sceneAtMs) % DOT_PERIOD_MS < DOT_ON_MS;
    f.linked = connected;
    f.shiftX = kOledWakeShift[wakes % OLED_WAKE_SHIFTS][0];
    f.shiftY = kOledWakeShift[wakes % OLED_WAKE_SHIFTS][1];
    f.contrast = boosting && (uint32_t)(now - pressAtMs) < BOOST_MS ? CONTRAST_PRESS : CONTRAST_NORMAL;
    uint32_t idle = (uint32_t)(now - idleAtMs);
    if (!inFlight && !speakingHeld && idle >= holdMs) {
      // 1 s linear ramp down; advance() turns the panel off at the end.
      uint32_t left = holdMs + FADE_MS - idle;
      f.contrast = (uint8_t)((uint32_t)f.contrast * left / FADE_MS);
      if (f.contrast == 0) f.contrast = 1;
    }
    return f;
  }

 private:
  void show(uint8_t next, uint32_t now) {
    if (scene == OLED_SCENE_OFF && next != OLED_SCENE_OFF) wakes++;
    scene = next;
    sceneAtMs = now;
    idleAtMs = now;
  }

  void change(uint8_t next, uint32_t now) {  // a state change only if the scene differs
    if (next != scene) show(next, now);
  }

  // A new image on an awake screen that is not a state change: the idle
  // timer keeps running.
  void repaint(uint8_t next, uint32_t now) {
    scene = next;
    sceneAtMs = now;
  }

  // asked: the person woke it. A self-wake over an asked agent scene keeps
  // the digits (the person is already looking); anywhere else it is
  // self-woken.
  void wakeAgent(uint32_t now, bool asked) {
    askedWake = asked || (askedWake && agentShown && scene != OLED_SCENE_OFF);
    agentShown = true;
    holdMs = IDLE_MS;
    if (scene == OLED_SCENE_OFF) {
      show(agentScene(), now);
    } else {
      if (agentScene() != scene) repaint(agentScene(), now);
      idleAtMs = now;
    }
  }

  // The number on the glass: the demo's drawn counts on the bench, the
  // payload's on an asked wake. «looking» shows without a number on a
  // self-wake or when withheld; «found» only reaches its digit scene asked.
  uint8_t countFor(uint8_t shown) const {
    if (demo) return shown == OLED_SCENE_FOUND ? DEMO_FOUND : (shown == OLED_SCENE_LOOKING ? DEMO_LOOKING : 0);
    if (!agentShown || !askedWake || !hasPayload) return 0;
    uint8_t n = shown == OLED_SCENE_FOUND ? payload.found : (shown == OLED_SCENE_LOOKING ? payload.looking : 0);
    return n <= OLED_COUNT_MAX ? n : 0;
  }

  void linkLost(uint32_t now) {
    // The app clears bit 5 on disconnect, but cannot write once the link is
    // gone: the pin clears it itself.
    speaking.stop(now);
    payload.flags = (uint8_t)(payload.flags & ~SCREEN_FLAG_AGENT_SPEAKING);
  }

  // Spec §10 «Nothing known»: the agent is unknown, counts and notification
  // go, the trusted-only bits (0, 1, 4) and speaking (5) are cleared, and
  // modest/disabled (2, 3) survive only if a trusted write set them. The
  // mascot and the locale stay.
  void staleReset(uint32_t now) {
    speaking.stop(now);
    payload.agent = SCREEN_AGENT_UNKNOWN;
    payload.found = 0;
    payload.looking = 0;
    payload.notify = 0;
    payload.notifyCount = 0;
    payload.flags = payloadTrusted ? (uint8_t)(payload.flags & (SCREEN_FLAG_MODEST | SCREEN_FLAG_DISABLED)) : 0;
    stale = true;
  }

  void onPress(uint32_t now, bool connected, bool ownedNow) {
    demo = false;
    agentShown = false;
    pressAtMs = now;
    boosting = true;
    holdMs = IDLE_MS;
    // An unowned pin has nobody to search for: «set me up» with the default
    // pebble, whatever the link (a new pin needs the app first).
    if (!ownedNow) {
      inFlight = false;
      show(OLED_SCENE_SET_ME_UP, now);
    } else if (!connected) {
      // Not delivered, as today; K10 «no link» for 3 s.
      inFlight = false;
      holdMs = NO_LINK_MS;
      show(OLED_SCENE_NO_LINK, now);
    } else {
      inFlight = true;
      show(OLED_SCENE_PRESSED, now);
    }
  }

  void onPressFailed(uint32_t now) {
    demo = false;
    agentShown = false;
    pressAtMs = now;
    boosting = true;
    holdMs = IDLE_MS;
    inFlight = false;
    show(OLED_SCENE_ERROR, now);
  }

  void onAck(uint32_t now, uint8_t ack) {
    if (demo) return;
    // Only a press outcome may change the screen: an ack never wakes it.
    bool outcome = inFlight || scene == OLED_SCENE_RESULT || scene == OLED_SCENE_ERROR;
    if ((ack == 1 || ack == 2) && inFlight) {
      change(OLED_SCENE_SEARCHING, now);
    } else if (ack == 3 && outcome) {
      inFlight = false;
      agentShown = false;
      change(OLED_SCENE_RESULT, now);
    } else if (ack == 4 && outcome) {
      inFlight = false;
      agentShown = false;
      change(OLED_SCENE_ERROR, now);
    }
    // The gestures rhythms 0x10-0x18 are not screen events here; 0x11
    // «found you» comes through tick()'s own flag.
  }

  // «Found you» (k20, tick()'s foundYou flag): the app sends event_ack 0x11 when the server's
  // press-again notice appears: someone nearby pressed and found this
  // person. The screen wakes on its own for 6 s, and only then does the
  // motor play ▬ ▬ (foundYouNow). event_ack is an open characteristic until
  // bonding (C12), so a stranger in range can send the byte: it counts only
  // on an owned pin, at most once a minute, never over a press in flight,
  // the fall or the bench, never with the screen disabled or in quiet
  // hours (bits 3 and 1; bit 1 only arrives on a trusted write, so before
  // bonding quiet hours never hold it back).
  void onFoundYou(uint32_t now) {
    if (!owned || demo || inFlight || falling() || disabled() || quietHours()) return;
    if (foundYouUsed && (uint32_t)(now - lastFoundYouMs) < FOUND_YOU_INTERVAL_MS) return;
    foundYouUsed = true;
    lastFoundYouMs = now;
    foundYouNow = true;
    agentShown = false;
    boosting = false;
    holdMs = IDLE_MS;
    show(OLED_SCENE_FOUND_YOU, now);
  }

  void advance(uint32_t now) {
    if (boosting && (uint32_t)(now - pressAtMs) >= BOOST_MS) boosting = false;
    if (inFlight && scene == OLED_SCENE_PRESSED && (uint32_t)(now - sceneAtMs) >= PRESSED_MS) {
      change(OLED_SCENE_SEARCHING, now);
    }
    if (inFlight && (uint32_t)(now - pressAtMs) >= FLIGHT_TIMEOUT_MS) {
      inFlight = false;
      change(OLED_SCENE_ERROR, now);
    }
    if (hasPayload && !stale && (uint32_t)(now - lastWriteMs) >= STALE_MS) staleReset(now);
    // holdsAwake() also ends a hold at its cap: call it every pass.
    const bool held = speaking.holdsAwake(now) && agentShown && scene != OLED_SCENE_OFF;
    if (speakingHeld && !held && agentShown && scene != OLED_SCENE_OFF) idleAtMs = now;  // then idle and fade
    speakingHeld = held;
    if (agentShown && scene != OLED_SCENE_OFF && !demo) {
      uint8_t next = agentScene();
      if (next != scene) repaint(next, now);
    }
    if (scene != OLED_SCENE_OFF && !inFlight && !speakingHeld && (uint32_t)(now - idleAtMs) >= holdMs + FADE_MS) {
      scene = OLED_SCENE_OFF;
      demo = false;
      agentShown = false;
    }
  }

  // One pass of a scene's frames, in ms.
  static uint32_t sceneMs(uint8_t id) {
    const OledSceneDesc& s = kOledScenes[id];
    uint32_t total = 0;
    for (uint8_t i = 0; i < s.frameCount; i++) total += kOledFrames[s.firstFrame + i].ms;
    return total;
  }

  uint8_t animFrame(uint32_t elapsed) const {
    const OledSceneDesc& s = kOledScenes[scene];
    if (s.frameCount <= 1) return 0;
    uint32_t total = sceneMs(scene);
    // The generator refuses 0 ms frames; this guard keeps a bad table from
    // dividing by zero on the pin (a hard fault in loop()).
    if (total == 0) return 0;
    if (s.flags & OLED_SCENE_LOOP) {
      elapsed %= total;
    } else if (elapsed >= total) {
      return s.frameCount - 1;  // the last frame holds
    }
    for (uint8_t i = 0; i < s.frameCount; i++) {
      uint16_t ms = kOledFrames[s.firstFrame + i].ms;
      if (elapsed < ms) return i;
      elapsed -= ms;
    }
    return s.frameCount - 1;
  }
};

#pragma once

#include <stdint.h>

// Push-to-talk gesture for voice from the pin (docs/pins/voice-from-pin.md
// §1, with the 2026-10-09 deltas at the top of that doc). Portable: it takes
// abstract hold began / ended events and a monotonic millisecond clock (the
// u32 millis() wrap is fine), never a GPIO. The P1 feeds it from the D1
// button after ButtonDebounce (idle_sleep.h); on the XIAO 1.47" touch board
// it can be the USR1 button or a hold on the screen's orb (the owner has not
// chosen, 2026-10-09). Debounce is the source's job. On a board whose mic
// cannot be unpowered (voice_board.h), «mic on» means the PDM clock runs.
//
// The mic is on only between a hold that began on an armed link and the end
// of that hold (or an earlier stop). Nothing but holdBegan() turns it on: no
// tick, link change or phone STOP can. The gesture builds no pairing window
// and erases no bond (owner, 2026-10-09): a hold of VOICE_LONG_HOLD_MS or
// more on an unarmed link is «didn't work», as in 0.3.0.
//
//   armed  = a voice-capable board on a trusted link (encrypted, bonded,
//            admitted) whose app subscribed to voice_stream; the caller
//            decides it (V2), this header only reads the flag.
//   armed hold:   under the talk threshold = scan on release;
//                 at the threshold = START, talk until release (END 0) or
//                 the 60 s cap (END 1).
//   unarmed hold: under the talk threshold = scan on release; from the
//                 threshold to 2 s = scan when connected (voice is just not
//                 on for this phone) or on a board without a mic, «didn't
//                 work» on a voice pin with no phone (link read at release);
//                 2 s or more = «didn't work», no scan (the shipped 0.3.0
//                 rule).
//
// The talk threshold is 2 s (owner, 2026-10-09): a press under 2 s is the
// scan on release, as shipped in 0.3.0; an armed hold of 2 s or more talks
// while held, where 0.3.0 says «didn't work». On a board without a mic, or
// with no armed link, a 2 s hold stays «didn't work». The doc's original
// 0.4 s is one -DRAILY_VOICE_TALK_HOLD_MS=400 away.
// Pure, host-tested in hardware/firmware/tests/test_voice_gesture.cpp (run at
// the default 2000 and at 400).

#ifndef RAILY_VOICE_TALK_HOLD_MS
#define RAILY_VOICE_TALK_HOLD_MS 2000
#endif

static const uint32_t VOICE_TALK_HOLD_MS = RAILY_VOICE_TALK_HOLD_MS;
static const uint32_t VOICE_LONG_HOLD_MS = 2000;      // = BUTTON_LONG_MS (idle_sleep.h)
static const uint32_t VOICE_UTTERANCE_CAP_MS = 60000;  // from the hold's start
static const uint32_t VOICE_NO_DEADLINE = 0xFFFFFFFFUL;

struct VoiceLink {
  bool voiceCapable;  // this board has a mic (voice_board.h, the runtime probe)
  bool linkUp;        // a phone is connected
  bool armed;         // trusted link + voice_stream subscribed (meaningful only with the two above)

  bool canTalk() const { return voiceCapable && linkUp && armed; }
};

// What the caller does, as bits; several can come at once, in this order:
// mic, then the frames, then the scan or the cue.
enum VoiceAction : uint16_t {
  VOICE_ACT_NONE = 0,
  VOICE_ACT_MIC_ON = 1 << 0,      // power the mic, start the pre-roll
  VOICE_ACT_MIC_OFF = 1 << 1,
  VOICE_ACT_SEND_PRESS = 1 << 2,  // queue PRESS (a new utterance id)
  VOICE_ACT_SEND_START = 1 << 3,  // queue START; the one START tick
  VOICE_ACT_SEND_END = 1 << 4,    // queue END with endReason after the queued audio
  VOICE_ACT_DROP_AUDIO = 1 << 5,  // wipe the pre-roll / everything queued, send nothing more
  VOICE_ACT_SCAN = 1 << 6,        // emitButtonEvent(0), the scan press
  VOICE_ACT_FAIL = 1 << 7,        // «didn't work»
};

struct VoiceGestureOut {
  uint16_t actions;
  uint8_t endReason;  // with VOICE_ACT_SEND_END: a VoiceEndReason (voice_frame.h)

  bool has(uint16_t action) const { return (actions & action) != 0; }
};

enum VoiceGestureState : uint8_t {
  VOICE_GESTURE_IDLE = 0,      // not held
  VOICE_GESTURE_UNARMED = 1,   // held, mic off: decided on release
  VOICE_GESTURE_PREROLL = 2,   // held, mic on, nothing but PRESS sent yet
  VOICE_GESTURE_TALKING = 3,   // held, mic on, START sent
  VOICE_GESTURE_SPENT = 4,     // held, already ended (cap, stop, link lost): the release does nothing
};

struct VoiceGesture {
  uint32_t talkHoldMs;  // set from VOICE_TALK_HOLD_MS by voiceGesture()
  uint8_t state;
  uint32_t downAtMs;

  bool held() const { return state != VOICE_GESTURE_IDLE; }
  bool micOn() const { return state == VOICE_GESTURE_PREROLL || state == VOICE_GESTURE_TALKING; }

  VoiceGestureOut holdBegan(uint32_t now, const VoiceLink& link) {
    if (state != VOICE_GESTURE_IDLE) return out(VOICE_ACT_NONE);  // a repeated began: ignored
    downAtMs = now;
    if (!link.canTalk()) {
      state = VOICE_GESTURE_UNARMED;
      return out(VOICE_ACT_NONE);
    }
    state = VOICE_GESTURE_PREROLL;
    return out(VOICE_ACT_MIC_ON | VOICE_ACT_SEND_PRESS);
  }

  VoiceGestureOut holdEnded(uint32_t now, const VoiceLink& link) {
    // The time up to the release counts first: a START or a cap the loop
    // had not ticked yet comes before the release.
    VoiceGestureOut result = tick(now, link);
    const uint32_t heldMs = now - downAtMs;
    switch (state) {
      case VOICE_GESTURE_UNARMED:
        result.actions |= unarmedRelease(heldMs, link);
        break;
      case VOICE_GESTURE_PREROLL:  // a tap: the scan, the pre-roll never leaves the pin
        result.actions |= VOICE_ACT_MIC_OFF | VOICE_ACT_DROP_AUDIO | VOICE_ACT_SCAN;
        break;
      case VOICE_GESTURE_TALKING:
        result.actions |= VOICE_ACT_MIC_OFF | VOICE_ACT_SEND_END;
        result.endReason = 0;  // VOICE_END_RELEASED
        break;
      default:  // IDLE (a repeated ended) or SPENT
        break;
    }
    state = VOICE_GESTURE_IDLE;
    return result;
  }

  // From loop(), at least by nextDeadlineMs(), and on every link change.
  VoiceGestureOut tick(uint32_t now, const VoiceLink& link) {
    if (!micOn()) return out(VOICE_ACT_NONE);
    if (!link.canTalk()) {
      // Disconnected or unsubscribed mid-hold: mic off, buffers gone, no END
      // (nobody can receive it), never a replay after a reconnect.
      state = VOICE_GESTURE_SPENT;
      return out(VOICE_ACT_MIC_OFF | VOICE_ACT_DROP_AUDIO | VOICE_ACT_FAIL);
    }
    const uint32_t heldMs = now - downAtMs;
    uint16_t actions = VOICE_ACT_NONE;
    if (state == VOICE_GESTURE_PREROLL && heldMs >= talkHoldMs) {
      state = VOICE_GESTURE_TALKING;
      actions |= VOICE_ACT_SEND_START;
    }
    if (state == VOICE_GESTURE_TALKING && heldMs >= VOICE_UTTERANCE_CAP_MS) {
      state = VOICE_GESTURE_SPENT;
      VoiceGestureOut capped = out(actions | VOICE_ACT_MIC_OFF | VOICE_ACT_SEND_END | VOICE_ACT_FAIL);
      capped.endReason = 1;  // VOICE_END_CAP
      return capped;
    }
    return out(actions);
  }

  // The pin or the phone ends the utterance early: the frame ring overflowed
  // (END 2), low battery (3), voice_control STOP (4). Never turns the mic on;
  // before START there is no utterance to end, so the pre-roll is dropped.
  VoiceGestureOut stop(uint8_t reason) {
    if (state == VOICE_GESTURE_PREROLL) {
      state = VOICE_GESTURE_SPENT;
      return out(VOICE_ACT_MIC_OFF | VOICE_ACT_DROP_AUDIO | VOICE_ACT_FAIL);
    }
    if (state == VOICE_GESTURE_TALKING) {
      state = VOICE_GESTURE_SPENT;
      VoiceGestureOut result = out(VOICE_ACT_MIC_OFF | VOICE_ACT_SEND_END | VOICE_ACT_FAIL);
      result.endReason = reason;
      return result;
    }
    return out(VOICE_ACT_NONE);
  }

  // Milliseconds until tick() has something to do (idle sleep), or
  // VOICE_NO_DEADLINE.
  uint32_t nextDeadlineMs(uint32_t now) const {
    const uint32_t heldMs = now - downAtMs;
    uint32_t at;
    if (state == VOICE_GESTURE_PREROLL) {
      at = talkHoldMs;
    } else if (state == VOICE_GESTURE_TALKING) {
      at = VOICE_UTTERANCE_CAP_MS;
    } else {
      return VOICE_NO_DEADLINE;
    }
    return heldMs >= at ? 0 : at - heldMs;
  }

 private:
  static VoiceGestureOut out(uint16_t actions) {
    VoiceGestureOut result = {actions, 0};
    return result;
  }

  uint16_t unarmedRelease(uint32_t heldMs, const VoiceLink& link) const {
    if (heldMs >= VOICE_LONG_HOLD_MS) return VOICE_ACT_FAIL;
    if (heldMs < talkHoldMs || !link.voiceCapable || link.linkUp) return VOICE_ACT_SCAN;
    return VOICE_ACT_FAIL;  // a talk hold on a voice pin with no phone: no scan
  }
};

static inline VoiceGesture voiceGesture(uint32_t talkHoldMs = VOICE_TALK_HOLD_MS) {
  VoiceGesture gesture = {talkHoldMs, VOICE_GESTURE_IDLE, 0};
  return gesture;
}

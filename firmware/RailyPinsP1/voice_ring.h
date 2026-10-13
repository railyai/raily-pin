#pragma once

#include <stdint.h>
#include <string.h>

#include "adpcm.h"
#include "voice_frame.h"
#include "voice_gesture.h"

// The pin side of voice_stream (docs/pins/voice-from-pin.md §4 «Sequence,
// loss, and flow control»): encoded AUDIO frames wait in a fixed ring until
// the link has TX credits; PRESS, START and END are queued around them in
// stream order (PRESS, START, AUDIO 1..n, END n + 1).
//
// - Pre-roll: frames captured from the press are kept but not sendable
//   until START; a tap drops them (drop()).
// - A new hold while the previous utterance is still draining (a quick
//   retry, a slow link) never cuts it short: the previous one keeps its
//   queued frames and its END, which go first, then the new PRESS. Only a
//   third hold while two are still queued gives up the older two (press()
//   returns false; the phone sees that as a lost link).
// - A full ring never drops a frame in the middle: the frame that does not
//   fit is refused, capture() returns false, the caller ends the utterance
//   (VoiceGesture::stop(VOICE_END_OVERFLOW)) and the ring takes nothing
//   more for this utterance. What is queued still goes, then END 2 with
//   frames_dropped.
// - A lost link, or one that can no longer carry voice (unsubscribed),
//   drops everything (reset()): nothing stale is replayed on a reconnect.
// - The first VOICE_WARMUP_FRAMES after the mic powers on are discarded and
//   never counted (mic power-up and the PDM filter delay).
// Pure, host-tested in hardware/firmware/tests/test_voice_ring.cpp.

static const uint8_t VOICE_WARMUP_FRAMES = 2;
// The pre-roll up to the talk threshold plus 1.6 s of link stall: 100
// frames (16.8 KB) at the doc's 0.4 s, 180 frames at 2 s.
static const uint16_t VOICE_RING_FRAMES = (uint16_t)(VOICE_TALK_HOLD_MS / VOICE_FRAME_MS + 80);

template <uint16_t N>
struct VoiceRing {
  uint8_t slots[N][VOICE_AUDIO_BYTES];
  uint16_t head;
  uint16_t count;

  bool full() const { return count == N; }
  bool empty() const { return count == 0; }
  bool push(const uint8_t frame[VOICE_AUDIO_BYTES]) {
    if (full()) return false;
    memcpy(slots[(uint16_t)((head + count) % N)], frame, VOICE_AUDIO_BYTES);
    count++;
    return true;
  }
  const uint8_t* front() const { return empty() ? 0 : slots[head]; }
  void pop() {
    if (empty()) return;
    head = (uint16_t)((head + 1) % N);
    count--;
  }
  void clear() {
    head = 0;
    count = 0;
  }
  // Drops the newest frames, keeping the oldest `keep`.
  void truncate(uint16_t keep) {
    if (keep < count) count = keep;
  }
};

enum VoiceUplinkPhase : uint8_t {
  VOICE_UPLINK_IDLE = 0,
  VOICE_UPLINK_PREROLL = 1,    // PRESS queued or sent, capturing, audio held back
  VOICE_UPLINK_STREAMING = 2,  // START queued or sent, audio sendable
  VOICE_UPLINK_ENDING = 3,     // capture over: the queue drains, then END
};

// One utterance's place in the stream: which of its PRESS, START and END
// are still to go, and what its END reports.
struct VoiceUtteranceOut {
  uint8_t utterance;
  bool pressPending;
  bool startPending;
  bool endPending;
  int8_t startGain;
  uint8_t startFlags;
  uint8_t endReason;
  uint16_t lastSentSeq;
  uint16_t framesSent;
  uint16_t framesDropped;
};

template <uint16_t N>
struct VoiceUplink {
  VoiceRing<N> ring;
  AdpcmState encoder;
  uint8_t phase;      // the current utterance's
  uint8_t utterance;  // the current (or last) utterance id
  uint16_t nextSeq;   // the current utterance's next AUDIO seq
  uint8_t warmupLeft;
  bool overflowed;
  VoiceUtteranceOut current;
  // The previous utterance, ended but not all sent yet: its frames are the
  // oldest `previousQueued` in the ring; whatever of its PRESS, START and
  // END is pending goes around them, all before anything of `current`.
  bool previousActive;
  uint16_t previousQueued;
  VoiceUtteranceOut previous;

  // VOICE_ACT_SEND_PRESS: a new utterance, its pre-roll starts now. False
  // when older audio had to be given up to make room (see above).
  bool press() {
    bool kept = true;
    if (phase == VOICE_UPLINK_ENDING && !previousActive) {
      previous = current;
      previousQueued = ring.count;
      previousActive = true;
      phase = VOICE_UPLINK_IDLE;
    } else if (phase == VOICE_UPLINK_ENDING) {
      kept = false;
      reset();
    } else {
      drop();
    }
    utterance = (uint8_t)(utterance + 1);
    memset(&current, 0, sizeof(current));
    current.utterance = utterance;
    current.pressPending = true;
    nextSeq = 1;
    warmupLeft = VOICE_WARMUP_FRAMES;
    overflowed = false;
    encoder.predictor = 0;
    encoder.stepIndex = 0;
    phase = VOICE_UPLINK_PREROLL;
    return kept;
  }

  // VOICE_ACT_SEND_START: the pre-roll and everything after it may leave.
  void start(int8_t gainHalfDb) {
    if (phase != VOICE_UPLINK_PREROLL) return;
    current.startGain = gainHalfDb;
    current.startFlags = nextSeq > 1 ? VOICE_START_FLAG_PREROLL : 0;
    current.startPending = true;
    phase = VOICE_UPLINK_STREAMING;
  }

  // One 20 ms PCM frame from the mic. False when the ring is full: the
  // caller stops the utterance (END 2). Ignored outside a capture.
  bool capture(const int16_t pcm[VOICE_FRAME_SAMPLES]) {
    if (!capturing()) return true;
    if (warmupLeft > 0) {
      warmupLeft--;
      return true;
    }
    if (overflowed || ring.full()) {
      overflowed = true;
      current.framesDropped++;
      return false;
    }
    uint8_t frame[VOICE_AUDIO_BYTES];
    voicePackAudio(frame, utterance, nextSeq, encoder, pcm);
    nextSeq++;
    ring.push(frame);
    return true;
  }

  // VOICE_ACT_SEND_END: no more capture; END goes after what is queued. A
  // phone STOP drops this utterance's queue (the phone wants no more audio).
  void finish(uint8_t reason) {
    if (phase != VOICE_UPLINK_STREAMING) return;
    if (reason == VOICE_END_PHONE_STOP) ring.truncate(previousFrames());
    current.endReason = reason;
    current.endPending = true;
    phase = VOICE_UPLINK_ENDING;
  }

  // VOICE_ACT_DROP_AUDIO: the current utterance sends nothing more (a tap,
  // a stop before START). A previous utterance still draining is kept.
  void drop() {
    ring.truncate(previousFrames());
    current.pressPending = false;
    current.startPending = false;
    current.endPending = false;
    phase = VOICE_UPLINK_IDLE;
  }

  // The link went down or can no longer carry voice: nothing more is sent.
  void reset() {
    previousActive = false;
    previousQueued = 0;
    ring.clear();
    drop();
  }

  // The next notification, copied into `out` (VOICE_AUDIO_BYTES at least);
  // 0 when there is nothing to send. It stays the next one until consume().
  uint8_t peek(uint8_t* out) const {
    if (previousActive) return peekOf(previous, true, previousQueued, out);
    return peekOf(current, streaming(), (uint16_t)(ring.count - previousFrames()), out);
  }

  // The notification peek() gave was sent.
  void consume() {
    if (previousActive) {
      if (consumeOf(previous, true, previousQueued)) previousQueued--;
      if (!previous.pressPending && !previous.startPending && !previous.endPending && previousQueued == 0) {
        previousActive = false;
      }
      return;
    }
    consumeOf(current, streaming(), ring.count);
    if (phase == VOICE_UPLINK_ENDING && !current.endPending) phase = VOICE_UPLINK_IDLE;
  }

  bool capturing() const { return phase == VOICE_UPLINK_PREROLL || phase == VOICE_UPLINK_STREAMING; }

 private:
  bool streaming() const { return phase == VOICE_UPLINK_STREAMING || phase == VOICE_UPLINK_ENDING; }
  uint16_t previousFrames() const { return previousActive ? previousQueued : 0; }

  // Stream order for one utterance: PRESS, START, its queued AUDIO, END.
  uint8_t peekOf(const VoiceUtteranceOut& u, bool started, uint16_t queued, uint8_t* out) const {
    if (u.pressPending) return voicePackPress(out, u.utterance);
    if (!started) return 0;
    if (u.startPending) return voicePackStart(out, u.utterance, u.startGain, u.startFlags);
    if (queued > 0) {
      memcpy(out, ring.front(), VOICE_AUDIO_BYTES);
      return VOICE_AUDIO_BYTES;
    }
    if (u.endPending) {
      return voicePackEnd(out, u.utterance, (uint16_t)(u.lastSentSeq + 1), u.endReason, u.framesSent,
                          u.framesDropped);
    }
    return 0;
  }

  // Marks sent what peekOf() gave. True when it was an AUDIO frame.
  bool consumeOf(VoiceUtteranceOut& u, bool started, uint16_t queued) {
    if (u.pressPending) {
      u.pressPending = false;
      return false;
    }
    if (!started) return false;
    if (u.startPending) {
      u.startPending = false;
      return false;
    }
    if (queued > 0) {
      u.lastSentSeq = voiceGetU16(ring.front() + 2);
      u.framesSent++;
      ring.pop();
      return true;
    }
    u.endPending = false;
    return false;
  }
};

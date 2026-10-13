#pragma once

#include <stdint.h>
#include <string.h>

#include "voice_frame.h"

// The capture side of voice from the pin (docs/pins/voice-from-pin.md §2),
// between the PDM interrupt and loop(): pure, no registers, host-tested in
// hardware/firmware/tests/test_voice_capture.cpp.
//
// - VoicePcmFifo: 20 ms PCM frames from the PDM interrupt (one writer) to
//   loop() (one reader). The interrupt copies each finished EasyDMA buffer
//   in and never waits; a full FIFO drops the new frame and counts it, so
//   a loop() pass stuck on flash or the screen costs audio, never the
//   interrupt. loop() encodes what it pops (voice_ring.h).
// - VoiceHighPass: the 1-pole high-pass of §2 (about 100 Hz at 16 kHz)
//   before the encoder: PDM output carries DC, and ADPCM would spend its
//   steps on it.

// 8 frames = 160 ms of loop() latency before the first frame is lost.
static const uint8_t VOICE_PCM_FIFO_FRAMES = 8;

template <uint8_t N>
struct VoicePcmFifo {
  int16_t frames[N][VOICE_FRAME_SAMPLES];
  volatile uint8_t head;      // writer only: the next slot to fill
  volatile uint8_t tail;      // reader only: the next slot to read
  volatile uint32_t pushed;   // frames taken since reset()
  volatile uint32_t overruns; // frames dropped because the FIFO was full

  // The writer: false when full (the frame is dropped and counted).
  bool push(const int16_t* pcm) {
    const uint8_t next = (uint8_t)((head + 1) % N);
    if (next == tail) {
      overruns = overruns + 1;
      return false;
    }
    memcpy(frames[head], pcm, sizeof(frames[head]));
    __atomic_signal_fence(__ATOMIC_SEQ_CST);  // the frame is whole before the reader sees it
    head = next;
    pushed = pushed + 1;
    return true;
  }
  bool empty() const { return head == tail; }
  // The reader: the oldest frame, or 0. It stays valid until pop().
  const int16_t* front() const { return empty() ? 0 : frames[tail]; }
  void pop() {
    if (empty()) return;
    __atomic_signal_fence(__ATOMIC_SEQ_CST);  // done with the frame before the writer may reuse it
    tail = (uint8_t)((tail + 1) % N);
  }
  // Only while the writer is stopped (the mic is off).
  void reset() {
    head = 0;
    tail = 0;
    pushed = 0;
    overruns = 0;
  }
};

// y[n] = x[n] - x[n-1] + a * y[n-1], a = exp(-2 pi 100 / 16000) ~ 0.9615
// in Q15. The state keeps 8 fraction bits so a quiet input does not stall
// on rounding.
static const int32_t VOICE_HPF_A_Q15 = 31507;

struct VoiceHighPass {
  int32_t lastIn;
  int32_t lastOut;  // Q8

  void reset() {
    lastIn = 0;
    lastOut = 0;
  }
  void run(int16_t* pcm, uint16_t count) {
    for (uint16_t i = 0; i < count; i++) {
      const int32_t in = pcm[i];
      const int64_t feedback = ((int64_t)lastOut * VOICE_HPF_A_Q15) >> 15;
      const int32_t out = (int32_t)((int64_t)(in - lastIn) * 256 + feedback);
      lastIn = in;
      lastOut = out;
      const int32_t sample = out >= 0 ? (out + 128) >> 8 : -((-out + 128) >> 8);
      pcm[i] = (int16_t)(sample > 32767 ? 32767 : sample < -32768 ? -32768 : sample);
    }
  }
};

// The START cue (the motor's ▮ when audio starts leaving the pin) and the
// audio it would leak into the mic: frames captured while it plays are
// zeroed before encoding (voice-from-pin.md §1 «Cues»). That only fits a
// short talk hold, where START comes before the person speaks. With the
// 2 s hold the pre-roll already holds the start of the question and START
// lands mid-sentence: a zeroed window cut «ло» out of «колоде» on the bench
// (2026-10-09, transcript «в коде»), and an unzeroed tick would buzz into
// the word. So above VOICE_START_CUE_MAX_HOLD_MS there is no START cue at
// all; the magenta LED (mic on from the press) stays the indicator.
static const uint32_t VOICE_START_CUE_ZERO_MS = 200;
static const uint32_t VOICE_START_CUE_MAX_HOLD_MS = 500;

struct VoiceStartCue {
  bool active;
  uint32_t untilMs;

  // START: whether the motor plays its ▮.
  bool start(uint32_t now, uint32_t talkHoldMs) {
    active = false;
    if (talkHoldMs > VOICE_START_CUE_MAX_HOLD_MS) return false;
    active = true;
    untilMs = now + VOICE_START_CUE_ZERO_MS;
    return true;
  }
  // A frame captured at `now` is zeroed.
  bool zeroes(uint32_t now) {
    if (!active) return false;
    if ((int32_t)(now - untilMs) < 0) return true;
    active = false;
    return false;
  }
};

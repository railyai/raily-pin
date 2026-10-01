#pragma once

#include <stdint.h>

// Vibration rhythms for the keyring pin: docs/pins/keyring-oled/implementation-plan.md
// §3 «PR 4», owner decisions of 2026-09-28 (spec §17). Pure: no Arduino, no
// radio. The event_ack callback only publishes the raw byte (as it does for
// feedback_state.h); loop() maps it with rhythmForEventAck(), calls
// trigger(), and writes the motor pin from tick(). Host-tested in
// hardware/firmware/tests/test_rhythm.cpp.
//
// Only important events vibrate:
//   ack 1  signal sent        ▮
//   ack 3  result ready       ▬
//   ack 4  didn't work        ▬▬▬
//   0x11   found for you      ▬ ▬  (the gestures-spec «look at the phone»
//                                  rhythm byte; the app sends it unprompted,
//                                  so it is also the self-wake signal: the
//                                  ▬ ▬ IS the found-for-you pattern, no ▮
//                                  is played before it)
// Everything else (ack 2, 0x10, 0x12-0x18, unknown bytes) is silent here.
// Gestures S1 extends the table for its own rhythms behind a gesture slot.

// Timings. Starting values; the bench tunes them (▮ and ▬ must be told
// apart by touch, gestures-spec S1).
static const uint16_t RHYTHM_TICK_MS = 80;         // ▮ on
static const uint16_t RHYTHM_BUZZ_MS = 300;        // ▬ on
static const uint16_t RHYTHM_GAP_MS = 150;         // off between parts
static const uint16_t RHYTHM_SPLIT_GAP_MS = 450;   // off inside «▬ ▬»

// Safety bounds.
// At most one pattern starts per window, like feedback_state.h's LED: a
// flood of open event_ack writes only replaces the one pending signal.
static const uint32_t RHYTHM_MIN_START_INTERVAL_MS = 800;
// Motor on-time budget: a bucket of 1400 ms refilled by 1 ms every 40 ms
// (1.5 s per minute). A pattern starts only if the bucket holds its whole
// on-time, and the bucket is charged the whole on-time at start. Any 60 s
// span therefore holds at most 1400 + (60000 + 1200) / 40 = 2930 ms of motor
// on-time (1200 ms = the longest pattern, which may start before the span):
// under the gestures-spec cap of 3 s per minute. The largest pattern (▬▬▬,
// 900 ms on) always fits a full bucket.
static const uint16_t RHYTHM_BUDGET_CAP_MS = 1400;
static const uint32_t RHYTHM_REFILL_EVERY_MS = 40;

enum RhythmSignal : uint8_t {
  RHYTHM_NONE = 0,
  RHYTHM_SENT = 1,    // ▮
  RHYTHM_RESULT = 2,  // ▬
  RHYTHM_ERROR = 3,   // ▬▬▬
  RHYTHM_FOUND = 4,   // ▬ ▬
};

static const uint8_t RHYTHM_MAX_STEPS = 5;

// steps[0], steps[2], ... are motor on; steps[1], steps[3], ... are off.
// count is odd: every pattern starts and ends with the motor on.
struct RhythmPattern {
  uint8_t count;
  uint16_t steps[RHYTHM_MAX_STEPS];
};

static const RhythmPattern RHYTHM_PATTERN_SENT = {1, {RHYTHM_TICK_MS, 0, 0, 0, 0}};
static const RhythmPattern RHYTHM_PATTERN_RESULT = {1, {RHYTHM_BUZZ_MS, 0, 0, 0, 0}};
static const RhythmPattern RHYTHM_PATTERN_ERROR = {
    5, {RHYTHM_BUZZ_MS, RHYTHM_GAP_MS, RHYTHM_BUZZ_MS, RHYTHM_GAP_MS, RHYTHM_BUZZ_MS}};
static const RhythmPattern RHYTHM_PATTERN_FOUND = {
    3, {RHYTHM_BUZZ_MS, RHYTHM_SPLIT_GAP_MS, RHYTHM_BUZZ_MS, 0, 0}};

static inline const RhythmPattern* rhythmPattern(RhythmSignal signal) {
  switch (signal) {
    case RHYTHM_SENT: return &RHYTHM_PATTERN_SENT;
    case RHYTHM_RESULT: return &RHYTHM_PATTERN_RESULT;
    case RHYTHM_ERROR: return &RHYTHM_PATTERN_ERROR;
    case RHYTHM_FOUND: return &RHYTHM_PATTERN_FOUND;
    case RHYTHM_NONE: break;
  }
  return 0;
}

static inline uint16_t rhythmOnTimeMs(const RhythmPattern& pattern) {
  uint16_t total = 0;
  for (uint8_t i = 0; i < pattern.count; i += 2) total = (uint16_t)(total + pattern.steps[i]);
  return total;
}

// The raw event_ack byte (untrusted radio input) → the owner's pattern.
static inline RhythmSignal rhythmForEventAck(uint8_t ack) {
  switch (ack) {
    case 1: return RHYTHM_SENT;
    case 3: return RHYTHM_RESULT;
    case 4: return RHYTHM_ERROR;
    case 0x11: return RHYTHM_FOUND;
    default: return RHYTHM_NONE;
  }
}

// One event_ack write feeds two outputs from different views of the byte:
// the LED keeps today's normalisation (an empty write or 0 flashes once),
// the motor maps the raw byte, so an empty or zero write never vibrates.
// `rhythm` 0 means nothing for the motor (rhythmForEventAck(0) is silent).
struct EventAckBytes {
  uint8_t led;
  uint8_t rhythm;
};

static inline EventAckBytes eventAckBytes(const uint8_t* data, uint16_t len) {
  const uint8_t raw = (data != 0 && len > 0) ? data[0] : 0;
  EventAckBytes out;
  out.led = raw == 0 ? 1 : raw;
  out.rhythm = raw;
  return out;
}

// Bounded, latest-wins motor player. Zero-initialise (RhythmPlayer p = {};
// or a global); use from loop() only.
struct RhythmPlayer {
  const RhythmPattern* pattern;
  uint32_t stepStartMs;
  uint32_t lastStartMs;
  uint32_t motorOffAtMs;
  uint32_t refillAtMs;
  uint16_t leadInMs;
  uint16_t budgetMs;
  uint8_t step;
  uint8_t pending;
  bool primed;
  bool active;
  bool inLeadIn;
  bool hasStarted;
  bool quiet;

  // Queue a signal. A newer signal replaces the pending one and, once the
  // start window allows, the running pattern. RHYTHM_NONE is ignored, so a
  // silent ack never cancels an important one.
  void trigger(RhythmSignal signal, uint32_t now) {
    advance(now);
    if (quiet || rhythmPattern(signal) == 0) return;
    pending = (uint8_t)signal;
    startPending(now);
  }

  // Quiet hours (screen_state flag bit 1): the motor stops at once and
  // nothing plays or queues until they end.
  void setQuietHours(bool on, uint32_t now) {
    advance(now);
    quiet = on;
    if (on) cancel(now);
  }

  // Stop the motor and drop the running and pending pattern.
  void cancel(uint32_t now) {
    advance(now);
    if (motorOn()) motorOffAtMs = now;
    active = false;
    pending = RHYTHM_NONE;
  }

  // Motor level for this instant.
  bool tick(uint32_t now) {
    advance(now);
    startPending(now);
    return motorOn();
  }

  bool motorOn() const {
    return active && !inLeadIn && (step & 1u) == 0;
  }

 private:
  void refill(uint32_t now) {
    if (!primed) {
      primed = true;
      budgetMs = RHYTHM_BUDGET_CAP_MS;
      refillAtMs = now;
      return;
    }
    const uint32_t elapsed = now - refillAtMs;  // rollover safe
    const uint32_t earned = elapsed / RHYTHM_REFILL_EVERY_MS;
    if (earned >= RHYTHM_BUDGET_CAP_MS) {
      budgetMs = RHYTHM_BUDGET_CAP_MS;
      refillAtMs = now;
      return;
    }
    const uint32_t total = budgetMs + earned;
    budgetMs = (uint16_t)(total > RHYTHM_BUDGET_CAP_MS ? RHYTHM_BUDGET_CAP_MS : total);
    refillAtMs += earned * RHYTHM_REFILL_EVERY_MS;
  }

  // Walk step edges up to now. Bounded by the pattern length; unsigned
  // subtraction keeps every deadline through the 32-bit millis rollover.
  void advance(uint32_t now) {
    refill(now);
    while (active) {
      const uint16_t duration = inLeadIn ? leadInMs : pattern->steps[step];
      if ((uint32_t)(now - stepStartMs) < duration) break;
      stepStartMs += duration;
      if (inLeadIn) {
        inLeadIn = false;
        continue;
      }
      if ((step & 1u) == 0) motorOffAtMs = stepStartMs;
      step++;
      if (step >= pattern->count) active = false;
    }
  }

  void startPending(uint32_t now) {
    if (pending == RHYTHM_NONE) return;
    if (hasStarted && (uint32_t)(now - lastStartMs) < RHYTHM_MIN_START_INTERVAL_MS) return;
    const RhythmPattern* next = rhythmPattern((RhythmSignal)pending);
    pending = RHYTHM_NONE;
    const uint16_t onTime = rhythmOnTimeMs(*next);
    // Over budget: drop the signal. A cut-short ▬ would read as ▮, and a
    // signal replayed a minute late would mean nothing.
    if (onTime > budgetMs) return;
    if (motorOn()) motorOffAtMs = now;  // replacing a buzz mid-way
    budgetMs = (uint16_t)(budgetMs - onTime);
    // Keep the motor off for one gap before the new pattern so a
    // replacement never merges with the buzz it cut or just followed.
    const uint32_t offFor = now - motorOffAtMs;
    leadInMs = (hasStarted && offFor < RHYTHM_GAP_MS) ? (uint16_t)(RHYTHM_GAP_MS - offFor) : 0;
    pattern = next;
    step = 0;
    stepStartMs = now;
    inLeadIn = leadInMs != 0;
    active = true;
    hasStarted = true;
    lastStartMs = now;
  }
};

#pragma once

#include <stdint.h>
#include "screen_payload.h"

// The mascot byte in flash (docs/pins/keyring-oled.md §10 «Storage», §15).
// Pure: the sketch owns the file (/raily/mascot.bin); this decides what it
// holds and when it may be written. Host-tested in tests/test_mascot_record.cpp.
//
// - Only a trusted (encrypted) write is ever persisted: before bonding the
//   mascot lives in RAM until the next reboot, so no radio can wear the
//   flash by cycling shapes.
// - Only on change (against what the flash holds; no record means the
//   default pebble/satin, 0x00), and at most once an hour: the last value
//   waits in RAM. The first write after boot may go at once.
// - Unbind and release erase the record at once, inside the release's
//   critical section (not throttled).

static const uint8_t MASCOT_RECORD_LENGTH = 4;
static const uint8_t MASCOT_RECORD_MAGIC0 = 'R';
static const uint8_t MASCOT_RECORD_MAGIC1 = 'M';
static const uint32_t MASCOT_PERSIST_MIN_MS = 3600000;  // once an hour
static const uint8_t MASCOT_DEFAULT = 0x00;             // pebble, satin

static inline bool mascotValid(uint8_t value) {
  return (value >> 4) < SCREEN_SHAPE_COUNT && (value & 0x0F) < SCREEN_MATERIAL_COUNT;
}

// {'R', 'M', value, ~value}: a torn or foreign file reads as no record.
static inline void mascotRecordEncode(uint8_t value, uint8_t out[MASCOT_RECORD_LENGTH]) {
  out[0] = MASCOT_RECORD_MAGIC0;
  out[1] = MASCOT_RECORD_MAGIC1;
  out[2] = value;
  out[3] = (uint8_t)~value;
}

static inline bool mascotRecordDecode(const uint8_t* data, uint32_t len, uint8_t* value) {
  if (data == nullptr || value == nullptr || len != MASCOT_RECORD_LENGTH) return false;
  if (data[0] != MASCOT_RECORD_MAGIC0 || data[1] != MASCOT_RECORD_MAGIC1) return false;
  if ((uint8_t)~data[2] != data[3] || !mascotValid(data[2])) return false;
  *value = data[2];
  return true;
}

// Loop-only; zero-initialise.
struct MascotPersist {
  uint8_t stored;       // what the flash holds (MASCOT_DEFAULT when nothing)
  uint8_t pending;
  bool hasPending;
  bool wrote;           // a write was attempted this boot
  uint32_t lastWriteMs;

  // The record read at boot (an owned pin only).
  void loaded(uint8_t value) { stored = value; }

  // Every accepted screen_state write. A plaintext one never reaches flash.
  void offer(uint8_t value, bool trusted) {
    if (!trusted || !mascotValid(value)) return;
    hasPending = value != stored;  // a change back to what flash holds cancels the wait
    pending = value;
  }

  // Whether a write is due now, and of what.
  bool due(uint32_t now, uint8_t* value) const {
    if (!hasPending) return false;
    if (wrote && (uint32_t)(now - lastWriteMs) < MASCOT_PERSIST_MIN_MS) return false;
    *value = pending;
    return true;
  }

  // The write happened (ok) or failed; either way the next waits an hour,
  // so a failing flash is not retried on every pass. A failed value stays
  // pending.
  void written(uint32_t now, uint8_t value, bool ok) {
    wrote = true;
    lastWriteMs = now;
    if (!ok) return;
    stored = value;
    if (pending == value) hasPending = false;
  }

  // Unbind or release: the record is gone, nothing waits. The hourly limit
  // keeps counting from the last write.
  void erase() {
    stored = MASCOT_DEFAULT;
    hasPending = false;
  }
};

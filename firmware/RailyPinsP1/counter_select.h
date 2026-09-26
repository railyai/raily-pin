#ifndef RAILY_COUNTER_SELECT_H
#define RAILY_COUNTER_SELECT_H

// Pure record layout + restore-selection logic for the persisted button-event
// counter. Kept free of Arduino/InternalFS dependencies so the exact same
// decision the firmware makes at boot can be unit-tested on the host
// (firmware/tests/test_counter_select.cpp).

#include <stdint.h>

// Two generation-numbered, checksummed slots: a torn write can only ever
// damage the slot being written, and restore picks the newest VALID record
// instead of trusting whatever four bytes happened to survive.
struct CounterRecord {
  uint32_t magic;     // COUNTER_MAGIC — identifies this record format
  uint32_t counter;
  uint32_t seq;       // generation — disambiguates the newest valid slot
  uint32_t checksum;  // magic ^ counter ^ seq ^ COUNTER_CHECKSUM_MASK
} __attribute__((packed));
static_assert(sizeof(CounterRecord) == 16, "on-disk counter record layout changed");
static const uint32_t COUNTER_MAGIC = 0x52314354;  // "R1CT"
// Integrity mask, NOT a secret: it detects flash corruption, it does not
// authenticate the record — the server's monotone watermark is the actual
// replay defense.
static const uint32_t COUNTER_CHECKSUM_MASK = 0x9E3779B9;
// A torn slot record is untrusted — bound any unvalidated candidate by the
// last persisted legacy counter plus slack for legacy-write failures, so
// relocated garbage cannot push the counter to an absurd range.
static const uint32_t COUNTER_SALVAGE_SLACK = 16;
// Never adopt a counter within this distance of the u32 ceiling: a few more
// presses would wrap to 0 and land permanently under the server watermark.
static const uint32_t COUNTER_WRAP_HEADROOM = 1024;
static const uint32_t COUNTER_ADOPT_MAX = 0xFFFFFFFFu - COUNTER_WRAP_HEADROOM;
// Legacy is dual-written right after a slot commit, so in healthy operation
// it is within one generation of the newest valid slot; a downgrade window
// (old firmware bumps only the legacy file) widens it by the number of
// presses there. A gap beyond this drift window is flash corruption, not
// evidence, and must not override a checksum-validated record.
static const uint32_t COUNTER_LEGACY_DRIFT = 256;

/// Computes the integrity checksum stored with a counter record.
static inline uint32_t counterChecksum(const CounterRecord& rec) {
  return rec.magic ^ rec.counter ^ rec.seq ^ COUNTER_CHECKSUM_MASK;
}

/// Returns whether a counter record has the expected format and checksum.
static inline bool counterRecordValid(const CounterRecord& rec) {
  return rec.magic == COUNTER_MAGIC && rec.checksum == counterChecksum(rec);
}

/// A slot file as read from storage: absent, or present with raw contents.
struct CounterSlotCandidate {
  bool present;
  CounterRecord rec;
};

/// What restore decided: counter/seq to resume from, and which slot holds the
/// newest valid record (-1 when no slot validated and counter came from
/// salvage — the next persist then writes slot 0).
struct CounterDecision {
  uint32_t counter;
  uint32_t seq;
  int8_t slot;
};

/// Picks the newest valid slot record; when none validates, returns the
/// largest plausible counter from torn records and the legacy bare-u32 file.
/// Overshoot is a harmless gap; undershoot is a replay-rejected press.
static inline CounterDecision selectCounterRecord(
    const CounterSlotCandidate& a,
    const CounterSlotCandidate& b,
    bool hasLegacy, uint32_t legacy) {
  CounterRecord best = {0, 0, 0, 0};
  int8_t bestSlot = -1;
  uint32_t salvage = 0;
  uint32_t maxSeqSeen = 0;
  const CounterSlotCandidate slots[2] = {a, b};
  for (int slot = 0; slot < 2; slot++) {
    if (!slots[slot].present) continue;
    const CounterRecord& rec = slots[slot].rec;
    if (rec.magic != COUNTER_MAGIC) continue;
    if (rec.counter > salvage) {
      // Torn record — untrusted, but still a plausible lower bound.
      salvage = rec.counter;
    }
    if (rec.seq > maxSeqSeen) {
      // Keeps the persisted generation monotone even on the salvage path —
      // without it a healed high-seq record could later outrank a fresh
      // low-seq persist and rewind the counter.
      maxSeqSeen = rec.seq;
    }
    // seq comparison is valid within one u32 epoch (~4e9 presses) — a
    // wrap would mis-rank the two slots, noted for the record.
    if (counterRecordValid(rec) && (bestSlot < 0 || rec.seq > best.seq)) {
      best = rec;
      bestSlot = slot;
    }
  }
  if (bestSlot >= 0) {
    // Legacy is dual-written only after a slot commit, so it is always <=
    // the true counter — a legitimate floor when the newest slot record is
    // itself torn later. But it is unchecksummed, so it may only lift the
    // validated record inside the drift window — beyond that it is
    // corruption, not evidence.
    uint32_t counter = best.counter;
    if (hasLegacy && legacy > counter &&
        legacy <= counter + COUNTER_LEGACY_DRIFT) {
      counter = legacy;
    }
    if (counter > COUNTER_ADOPT_MAX) counter = COUNTER_ADOPT_MAX;
    CounterDecision d = {counter, best.seq, bestSlot};
    return d;
  }
  // The legacy bare-u32 file can coexist with a torn slot after an
  // interrupted upgrade. It has no integrity field, but remains a plausible
  // lower bound — and a NONZERO value bounds salvage (a zeroed file is
  // corruption, not evidence, so it imposes no bound). The cap saturates:
  // a wrapped legacy+slack would clamp DOWN into an undershoot, which is a
  // replay-rejected press.
  if (hasLegacy && legacy > salvage) salvage = legacy;
  const uint32_t cap =
      !hasLegacy || legacy == 0 || legacy > UINT32_MAX - COUNTER_SALVAGE_SLACK
          ? COUNTER_ADOPT_MAX
          : legacy + COUNTER_SALVAGE_SLACK;
  if (salvage > cap) salvage = cap;
  CounterDecision d = {salvage, maxSeqSeen, -1};
  return d;
}

#endif  // RAILY_COUNTER_SELECT_H

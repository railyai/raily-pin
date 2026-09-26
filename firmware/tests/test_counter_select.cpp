// Host-side unit tests for the firmware counter restore selection.
// Compile and run: firmware/tests/run_tests.sh
// The scenarios mirror the bench matrix: fresh flash, legacy upgrade,
// ping-pong, torn writes, garbage, and the legacy+slack salvage bound.

#include <stdio.h>
#include "../RailyPinsP1/counter_select.h"

static int failures = 0;
static void expect(bool cond, const char* name) {
  if (cond) {
    printf("PASS %s\n", name);
  } else {
    printf("FAIL %s\n", name);
    failures++;
  }
}

static CounterSlotCandidate absent() {
  CounterSlotCandidate c = {false, {0, 0, 0, 0}};
  return c;
}

static CounterSlotCandidate present(const CounterRecord& r) {
  CounterSlotCandidate c = {true, r};
  return c;
}

static CounterRecord valid(uint32_t counter, uint32_t seq) {
  CounterRecord r = {COUNTER_MAGIC, counter, seq, 0};
  r.checksum = counterChecksum(r);
  return r;
}

// Magic intact, checksum broken — what a write interrupted mid-record or
// flash bit-rot looks like to restore.
static CounterRecord torn(uint32_t counter, uint32_t seq) {
  CounterRecord r = valid(counter, seq);
  r.checksum ^= 0xA5A5A5A5u;
  return r;
}

// Relocated garbage — wrong magic, must never count even as salvage.
static CounterRecord garbage() {
  CounterRecord r = {0xDEADBEEFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x12345678u};
  return r;
}

int main() {
  CounterDecision d;

  d = selectCounterRecord(absent(), absent(), false, 0);
  expect(d.counter == 0 && d.slot == -1, "fresh device starts at 0");

  d = selectCounterRecord(absent(), absent(), true, 42);
  expect(d.counter == 42 && d.seq == 0 && d.slot == -1,
         "upgrade: legacy-only restore");

  d = selectCounterRecord(present(valid(10, 3)), absent(), true, 5);
  expect(d.counter == 10 && d.seq == 3 && d.slot == 0,
         "single valid slot wins");

  d = selectCounterRecord(present(valid(10, 3)), present(valid(11, 4)),
                          true, 5);
  expect(d.counter == 11 && d.seq == 4 && d.slot == 1,
         "ping-pong: newest seq wins");

  d = selectCounterRecord(present(valid(10, 3)), present(torn(999, 9)),
                          false, 0);
  expect(d.counter == 10 && d.slot == 0,
         "torn sibling ignored while a valid slot exists");

  d = selectCounterRecord(present(torn(30, 5)), present(torn(35, 6)),
                          true, 40);
  expect(d.counter == 40 && d.slot == -1,
         "both torn: legacy floor beats torn counters");

  d = selectCounterRecord(present(torn(30, 5)), present(torn(35, 6)),
                          false, 0);
  expect(d.counter == 35 && d.slot == -1,
         "both torn, no legacy: largest torn counter");

  d = selectCounterRecord(present(torn(110, 9)), absent(), true, 100);
  expect(d.counter == 110 && d.slot == -1,
         "torn within legacy+slack is kept");

  d = selectCounterRecord(present(torn(1u << 30, 9)), absent(), true, 100);
  expect(d.counter == 100 + COUNTER_SALVAGE_SLACK && d.slot == -1,
         "torn beyond slack clamped to legacy+slack");

  d = selectCounterRecord(present(torn(1u << 30, 9)), absent(), false, 0);
  expect(d.counter == (1u << 30) && d.slot == -1,
         "no legacy: torn salvage unbounded");

  d = selectCounterRecord(present(garbage()), absent(), true, 7);
  expect(d.counter == 7 && d.slot == -1,
         "bad-magic garbage ignored, legacy wins");

  // The newest valid slot is the authority for seq/slot, but legacy is a
  // floor inside the drift window: a downgrade window (old firmware bumps
  // only /counter.bin) legitimately leaves legacy above the surviving slot.
  d = selectCounterRecord(absent(), present(valid(50, 7)), true, 60);
  expect(d.counter == 60 && d.seq == 7 && d.slot == 1,
         "valid slot: legacy floor inside drift window");

  d = selectCounterRecord(absent(),
                          present(valid(50, 7)),
                          true, 50 + COUNTER_LEGACY_DRIFT);
  expect(d.counter == 50 + COUNTER_LEGACY_DRIFT,
         "valid slot: legacy at drift edge adopted");

  d = selectCounterRecord(absent(),
                          present(valid(50, 7)),
                          true, 50 + COUNTER_LEGACY_DRIFT + 1);
  expect(d.counter == 50 && d.slot == 1,
         "valid slot: legacy beyond drift is corruption, rejected");

  d = selectCounterRecord(absent(), present(valid(50, 7)), true,
                          0xFFFFFFF0u);
  expect(d.counter == 50 && d.slot == 1,
         "valid slot: huge garbage legacy cannot override record");

  // Equal seq on both slots: strict '>' keeps the first (slot 0) — a seq
  // collision resolves deterministically rather than rewinding.
  d = selectCounterRecord(present(valid(10, 3)), present(valid(20, 3)),
                          false, 0);
  expect(d.counter == 10 && d.slot == 0, "equal seq: slot 0 wins");

  d = selectCounterRecord(absent(), present(valid(9, 0)), false, 0);
  expect(d.counter == 9 && d.seq == 0 && d.slot == 1,
         "seq 0 is a valid record");

  // seq wrap mis-ranks the slots (strict '>' picks the pre-wrap record) —
  // accepted risk after ~4e9 persists; this pins the chosen behavior.
  d = selectCounterRecord(present(valid(10, UINT32_MAX)),
                          present(valid(11, 0)), false, 0);
  expect(d.counter == 10 && d.slot == 0,
         "seq wrap: pre-wrap record wins (accepted risk)");

  // A legacy file present but zeroed is corruption, not evidence — it
  // imposes no bound, so a large torn counter is still adopted.
  d = selectCounterRecord(present(torn(1u << 30, 9)), absent(), true, 0);
  expect(d.counter == (1u << 30) && d.slot == -1,
         "legacy=0: no bound, torn floor kept");

  // Adoption is capped below the u32 ceiling: a counter adopted at
  // UINT32_MAX would wrap to 0 within a press and brick against the
  // server watermark — COUNTER_ADOPT_MAX leaves headroom.
  d = selectCounterRecord(present(torn(1u << 30, 9)), absent(), true,
                          UINT32_MAX);
  expect(d.counter == COUNTER_ADOPT_MAX && d.slot == -1,
         "legacy=UINT32_MAX: adoption capped at headroom");

  d = selectCounterRecord(present(torn(1u << 30, 9)), absent(), true,
                          UINT32_MAX - 4);
  expect(d.counter == COUNTER_ADOPT_MAX && d.slot == -1,
         "legacy near max: capped, no wrapped clamp-down");

  d = selectCounterRecord(present(torn(UINT32_MAX, 9)), absent(), false, 0);
  expect(d.counter == COUNTER_ADOPT_MAX && d.slot == -1,
         "torn at ceiling: capped at headroom");

  d = selectCounterRecord(absent(), present(valid(UINT32_MAX, 3)),
                          false, 0);
  expect(d.counter == COUNTER_ADOPT_MAX && d.slot == 1,
         "valid record at ceiling: capped at headroom");

  // Salvage keeps the generation monotone: a torn record's seq is
  // untrusted data but still the newest persisted generation — adopting
  // it prevents a healed high-seq record from outranking the next persist
  // and rewinding the counter.
  d = selectCounterRecord(present(torn(30, 500)), present(torn(35, 6)),
                          true, 40);
  expect(d.counter == 40 && d.seq == 500 && d.slot == -1,
         "salvage path returns max seq seen");

  d = selectCounterRecord(absent(), absent(), true, 42);
  expect(d.seq == 0, "legacy-only salvage: seq 0");

  if (failures) {
    printf("%d FAILURES\n", failures);
  } else {
    printf("ALL PASS\n");
  }
  return failures ? 1 : 0;
}

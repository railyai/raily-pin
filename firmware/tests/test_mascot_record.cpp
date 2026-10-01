// Host tests for mascot_record.h (docs/pins/keyring-oled.md §10 «Storage»,
// §15): the flash record and when it may be written.
#include <stdint.h>
#include <stdio.h>
#include "../RailyPinsP1/mascot_record.h"

static int failures = 0;
static void expect(bool condition, const char* name) {
  printf("%s %s\n", condition ? "PASS" : "FAIL", name);
  if (!condition) failures++;
}

static void record() {
  uint8_t buf[MASCOT_RECORD_LENGTH];
  uint8_t value = 0xEE;
  mascotRecordEncode(0x51, buf);
  expect(mascotRecordDecode(buf, sizeof(buf), &value) && value == 0x51, "a record round-trips");
  buf[3] ^= 1;
  value = 0xEE;
  expect(!mascotRecordDecode(buf, sizeof(buf), &value) && value == 0xEE, "a torn record reads as none");
  mascotRecordEncode(0x51, buf);
  expect(!mascotRecordDecode(buf, 3, &value) && !mascotRecordDecode(buf, 5, &value), "a wrong length reads as none");
  buf[0] = 'X';
  expect(!mascotRecordDecode(buf, sizeof(buf), &value), "a foreign file reads as none");
  mascotRecordEncode(0x62, buf);  // shape 6 does not exist
  expect(!mascotRecordDecode(buf, sizeof(buf), &value), "an out-of-range look reads as none");
  mascotRecordEncode(0x03, buf);  // material 3 does not exist
  expect(!mascotRecordDecode(buf, sizeof(buf), &value), "so does a bad material");
  expect(!mascotRecordDecode(nullptr, 4, &value), "no buffer reads as none");
}

static void policy() {
  MascotPersist p = {};
  uint8_t value = 0;
  p.offer(0x12, false);
  expect(!p.due(0, &value), "a plaintext mascot is never persisted");
  p.offer(0x00, true);
  expect(!p.due(0, &value), "the default look needs no record");
  p.offer(0x12, true);
  expect(p.due(0, &value) && value == 0x12, "a trusted change is written at once, the first time");
  p.written(0, 0x12, true);
  expect(!p.due(1, &value), "and then nothing is pending");
  p.offer(0x12, true);
  expect(!p.due(1, &value), "the same value again writes nothing (only on change)");
  p.offer(0x51, true);
  expect(!p.due(MASCOT_PERSIST_MIN_MS - 1, &value), "the next change waits for the hour");
  p.offer(0x31, true);
  expect(p.due(MASCOT_PERSIST_MIN_MS, &value) && value == 0x31, "and then writes the last value, not every one");
  p.offer(0x12, true);
  expect(!p.due(MASCOT_PERSIST_MIN_MS, &value), "a change back to what flash holds cancels the wait");
  p.offer(0x31, true);
  p.written(MASCOT_PERSIST_MIN_MS, 0x31, false);
  expect(!p.due(MASCOT_PERSIST_MIN_MS + 1, &value), "a failed write waits the hour too");
  expect(p.due(2 * MASCOT_PERSIST_MIN_MS, &value) && value == 0x31, "and is retried after it");
  p.written(2 * MASCOT_PERSIST_MIN_MS, 0x31, true);
  p.offer(0x51, true);
  p.erase();
  expect(!p.due(5 * MASCOT_PERSIST_MIN_MS, &value), "unbind drops the waiting value");
  p.offer(0x00, true);
  expect(!p.due(5 * MASCOT_PERSIST_MIN_MS, &value), "after the erase the default needs no record");
  p.offer(0x31, true);
  expect(p.due(5 * MASCOT_PERSIST_MIN_MS, &value) && value == 0x31, "a new owner's mascot is written again");

  MascotPersist w = {};
  w.loaded(0x51);
  w.offer(0x51, true);
  expect(!w.due(0, &value), "the boot record counts as stored");
  const uint32_t t0 = UINT32_MAX - 10;
  w.offer(0x12, true);
  expect(w.due(t0, &value), "the first write after boot may go at once");
  w.written(t0, 0x12, true);
  w.offer(0x21, true);
  expect(!w.due(t0 + MASCOT_PERSIST_MIN_MS - 1, &value), "the hour holds across the millis wrap");
  expect(w.due(t0 + MASCOT_PERSIST_MIN_MS, &value), "and ends on time after it");
  w.offer(0x72, true);
  expect(w.due(t0 + MASCOT_PERSIST_MIN_MS, &value) && value == 0x21, "an invalid look is never offered");
}

int main() {
  record();
  policy();
  return failures ? 1 : 0;
}

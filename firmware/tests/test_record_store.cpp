// Host tests for record_store.h: the flash steps behind the press counter,
// the device secret, the admitted-phones table and the mascot byte, run on
// the RAM file system in fake_fs.h (lost closes, short writes, a full
// volume, failed renames, and a power loss before every single operation).
// The sketch (RailyPinsP1.ino) calls exactly these templates on InternalFS.
// Compile and run: firmware/tests/run_tests.sh

#include <stdio.h>
#include <string.h>

#include <string>

#include "fake_fs.h"
#include "../RailyPinsP1/record_store.h"

static int failures = 0;
static void expect(bool condition, const char* name) {
  printf("%s %s\n", condition ? "PASS" : "FAIL", name);
  if (!condition) failures++;
}
static void expectf(bool condition, const char* name, int k) {
  if (!condition) {
    printf("FAIL %s (power lost before op %d)\n", name, k);
    failures++;
  }
}

// The sketch's paths (RailyPinsP1.ino).
static const char* const COUNTER_PATHS[2] = {"/counter_a.bin", "/counter_b.bin"};
static const char COUNTER_PATH[] = "/counter.bin";
static const char SECRET_DIR[] = "/raily";
static const char SECRET_PATH[] = "/raily/device_secret.bin";
static const char SECRET_TEMP_PATH[] = "/raily/device_secret.new";
static const char MASCOT_PATH[] = "/raily/mascot.bin";
static const char BONDS_PATH[] = "/raily/bonds.bin";
static const char BONDS_TEMP_PATH[] = "/raily/bonds.new";

static std::string trace(const FakeFs& fs) {
  std::string out;
  for (size_t i = 0; i < fs.trace.size(); i++) out += (i ? "; " : "") + fs.trace[i];
  return out;
}
static std::string n(size_t value) { return std::to_string(value); }

// The pin's counter RAM and its two calls, exactly as the sketch makes them
// (restoreCounter in setup(), persistCounter after eventCounter++ in
// emitButtonEvent). A power cycle is a new Pin on the same flash.
struct Pin {
  explicit Pin(FakeFs& f) : fs(f) {}
  FakeFs& fs;
  uint32_t eventCounter = 0;
  uint32_t counterSeq = 0;
  int8_t counterSlot = -1;
  void boot() {
    CounterDecision d = restoreCounterRecord(fs, COUNTER_PATHS, COUNTER_PATH);
    if (d.counter > eventCounter) eventCounter = d.counter;
    counterSeq = d.seq;
    counterSlot = d.slot;
  }
  bool press() {
    eventCounter++;
    return persistCounterRecord(fs, COUNTER_PATHS, COUNTER_PATH, eventCounter, &counterSeq, &counterSlot);
  }
};

static CounterRecord counterRecord(const FakeFs& fs, int slot) {
  CounterRecord rec = {};
  FakeFs::Bytes bytes = fs.get(COUNTER_PATHS[slot]);
  if (bytes.size() >= sizeof(rec)) memcpy(&rec, &bytes[0], sizeof(rec));
  return rec;
}
static uint32_t legacyValue(const FakeFs& fs) {
  uint32_t value = 0;
  FakeFs::Bytes bytes = fs.get(COUNTER_PATH);
  if (bytes.size() >= sizeof(value)) memcpy(&value, &bytes[0], sizeof(value));
  return value;
}
static uint32_t restoredCounter(FakeFs& fs) {
  fs.reboot();
  Pin pin(fs);
  pin.boot();
  return pin.eventCounter;
}

// ---- fileStartsWith ----

static void testFileStartsWith() {
  FakeFs fs;
  uint8_t pattern[100];
  for (size_t i = 0; i < sizeof(pattern); i++) pattern[i] = (uint8_t)(i * 7 + 1);
  fs.put("/f", pattern, sizeof(pattern));

  fs.trace.clear();
  expect(fileStartsWith(fs, "/f", pattern, 100), "fileStartsWith: the whole file matches");
  expect(trace(fs) == "open-r /f; read 32; read 32; read 32; read 4; close",
         "fileStartsWith: read in 32-byte chunks, then closed");
  expect(fileStartsWith(fs, "/f", pattern, 32) && fileStartsWith(fs, "/f", pattern, 64) &&
             fileStartsWith(fs, "/f", pattern, 33) && fileStartsWith(fs, "/f", pattern, 1),
         "fileStartsWith: lengths at and around a chunk boundary");
  expect(fileStartsWith(fs, "/f", pattern, 50),
         "fileStartsWith: a longer file matches its prefix (intended: the secret and bond steps truncate first, "
         "the counter restore reads only 16 bytes)");
  expect(fileStartsWith(fs, "/f", pattern, 0), "fileStartsWith: length 0 on a file that opens is true");

  const size_t at[] = {0, 31, 32, 63, 64, 95, 96, 99};
  bool allCaught = true;
  for (size_t i = 0; i < sizeof(at) / sizeof(at[0]); i++) {
    uint8_t expected[100];
    memcpy(expected, pattern, sizeof(expected));
    expected[at[i]] ^= 0x01;
    if (fileStartsWith(fs, "/f", expected, 100)) allCaught = false;
  }
  expect(allCaught, "fileStartsWith: one differing byte at either side of every chunk boundary is caught");
  {
    uint8_t expected[100];
    memcpy(expected, pattern, sizeof(expected));
    expected[5] ^= 0x80;
    fs.trace.clear();
    expect(!fileStartsWith(fs, "/f", expected, 100) && trace(fs) == "open-r /f; read 32; close",
           "fileStartsWith: stops at the first differing chunk and still closes");
  }

  fs.put("/short", pattern, 40);
  expect(!fileStartsWith(fs, "/short", pattern, 41), "fileStartsWith: a file one byte short is false");
  fs.put("/short31", pattern, 31);
  expect(!fileStartsWith(fs, "/short31", pattern, 32), "fileStartsWith: 31 bytes for a 32-byte record is false");
  {
    // The read count matters, not only the bytes in the chunk: the second
    // read returns nothing and the chunk still holds the first 32 bytes,
    // which equal the record's second half here.
    uint8_t same[64];
    memset(same, 0x3C, sizeof(same));
    fs.put("/half", same, 32);
    expect(!fileStartsWith(fs, "/half", same, 64), "fileStartsWith: a file a whole chunk short is false");
  }
  fs.put("/empty", FakeFs::Bytes());
  expect(!fileStartsWith(fs, "/empty", pattern, 1), "fileStartsWith: an empty file is false");
  expect(!fileStartsWith(fs, "/missing", pattern, 1) && !fileStartsWith(fs, "/missing", pattern, 0),
         "fileStartsWith: a missing file is false, even for length 0");
  fs.failReadOn = "/f";
  expect(!fileStartsWith(fs, "/f", pattern, 10), "fileStartsWith: a read error is false");
  fs.failReadOn.clear();
  fs.failOpenOn = "/f";
  expect(!fileStartsWith(fs, "/f", pattern, 10), "fileStartsWith: a file that does not open is false");
  expect(fs.openFiles == 0, "fileStartsWith: no file left open");
}

// ---- press counter ----

static void testCounterSequence() {
  FakeFs fs;
  Pin pin(fs);
  pin.boot();
  expect(trace(fs) == "open-r /counter_a.bin; open-r /counter_b.bin; open-r /counter.bin",
         "counter restore on a fresh pin: three opens, nothing read");
  expect(pin.eventCounter == 0 && pin.counterSeq == 0 && pin.counterSlot == -1, "counter restore: fresh pin at 0");

  // The sequence persistCounter() ran before record_store.h (RailyPinsP1.ino
  // at 5c9ffde76): open-w slot, seek 0, write 16, close, read back, then the
  // legacy file open-w, seek 0, write 4, close.
  fs.trace.clear();
  expect(pin.press(), "counter: first press persists");
  expect(trace(fs) ==
             "open-w /counter_a.bin; seek 0; write 16; close; open-r /counter_a.bin; read 16; close; "
             "open-w /counter.bin; seek 0; write 4; close",
         "counter persist: the same file operations as before the seam");
  fs.trace.clear();
  pin.boot();
  expect(trace(fs) ==
             "open-r /counter_a.bin; read 16; close; open-r /counter_b.bin; open-r /counter.bin; read 4; close",
         "counter restore: the same file operations as before the seam");
  expect(fs.openFiles == 0, "counter: no file left open");
}

static void testCounterAlternates() {
  FakeFs fs;
  Pin pin(fs);
  pin.boot();
  bool alternates = true;
  for (uint32_t i = 1; i <= 6; i++) {
    const int expectedSlot = (i % 2 == 1) ? 0 : 1;
    if (!pin.press() || pin.counterSlot != expectedSlot || pin.counterSeq != i || pin.eventCounter != i) {
      alternates = false;
    }
    CounterRecord rec = counterRecord(fs, expectedSlot);
    if (!counterRecordValid(rec) || rec.counter != i || rec.seq != i || legacyValue(fs) != i) alternates = false;
  }
  expect(alternates, "counter: persists alternate a, b, a, ... with seq 1, 2, 3 and the legacy file current");
  expect(restoredCounter(fs) == 6, "counter: a reboot resumes at the last persisted press");
  Pin again(fs);
  again.boot();
  expect(again.counterSlot == 1 && again.counterSeq == 6, "counter: restore names the newest slot and its seq");
  expect(again.press() && again.counterSlot == 0 && counterRecord(fs, 0).counter == 7,
         "counter: after a reboot the next persist overwrites the older slot");
}

static void testCounterLostClose() {
  FakeFs fs;
  Pin pin(fs);
  pin.boot();
  pin.press();
  pin.press();  // slot b holds 2 (seq 2), slot a holds 1 (seq 1)
  const FakeFs::Bytes slotA = fs.get(COUNTER_PATHS[0]);
  fs.loseCloseOn = COUNTER_PATHS[0];
  fs.trace.clear();
  expect(!pin.press(), "counter: a lost close on the slot is detected (read back) and the persist fails");
  expect(pin.counterSeq == 2 && pin.counterSlot == 1,
         "counter: a failed persist leaves counterSeq and counterSlot untouched");
  expect(pin.eventCounter == 3, "counter: the RAM counter stays bumped (emitButtonEvent's rule: gaps are harmless)");
  expect(fs.get(COUNTER_PATHS[0]) == slotA && legacyValue(fs) == 2,
         "counter: after a lost close the slot keeps its old record and the legacy file is not written");
  expect(trace(fs).find("/counter.bin") == std::string::npos, "counter: no legacy write after a failed slot commit");
  expect(restoredCounter(fs) == 2, "counter: a reboot after the lost close resumes at the last persisted press");

  fs.loseCloseOn.clear();
  Pin next(fs);
  next.boot();
  expect(next.press() && next.counterSlot == 0 && counterRecord(fs, 0).counter == 3,
         "counter: the next persist goes to the same slot again");

  FakeFs fresh;
  Pin first(fresh);
  first.boot();
  fresh.loseCloseOn = COUNTER_PATHS[0];
  expect(!first.press() && fresh.has(COUNTER_PATHS[0]) && fresh.get(COUNTER_PATHS[0]).empty(),
         "counter: a lost close on a new slot file leaves it empty and fails the persist");
  expect(restoredCounter(fresh) == 0, "counter: an empty slot file reads as no record");
}

static void testCounterOtherFailures() {
  {
    FakeFs fs;
    Pin pin(fs);
    pin.boot();
    pin.press();
    pin.press();
    // 8 bytes short: the new magic and counter land over the old seq and
    // checksum. (A write 1 to 4 bytes short can leave a VALID record here:
    // two presses apart, counter ^ seq and so the checksum's high bytes are
    // often the same. That record is one the pin did not emit, a harmless
    // gap; the persist still fails on the write count.)
    fs.shortWriteBy = 8;
    expect(!pin.press() && pin.counterSeq == 2 && pin.counterSlot == 1,
           "counter: a short write fails the persist, state untouched");
    // The short write still commits its bytes over the older slot: a torn
    // record there, never in the newest valid slot.
    expect(!counterRecordValid(counterRecord(fs, 0)) && counterRecordValid(counterRecord(fs, 1)),
           "counter: a short write can only tear the slot being written");
    fs.shortWriteBy = 0;
    expect(restoredCounter(fs) == 2, "counter: restore after a short write picks the newest valid slot");
  }
  {
    FakeFs fs;
    Pin pin(fs);
    pin.boot();
    pin.press();
    pin.press();
    fs.shortWriteBy = 1;
    expect(!pin.press() && pin.counterSeq == 2, "counter: a write 1 byte short fails the persist");
    fs.shortWriteBy = 0;
    expect(restoredCounter(fs) == 3,
           "counter: a write 1 byte short leaves a valid record of the press not emitted (a gap, harmless; today's "
           "behaviour)");
  }
  {
    FakeFs fs;
    Pin pin(fs);
    pin.boot();
    pin.press();
    fs.full = true;
    expect(!pin.press() && pin.counterSlot == 0, "counter: a full volume (new slot file) fails the persist");
    fs.full = false;
    pin.press();
    pin.press();  // both slot files exist now
    fs.full = true;
    const uint32_t seq = pin.counterSeq;
    expect(!pin.press() && pin.counterSeq == seq, "counter: a full volume (commit lost at close) fails the persist");
    fs.full = false;
  }
  {
    FakeFs fs;
    Pin pin(fs);
    pin.boot();
    pin.press();
    fs.failOpenOn = COUNTER_PATH;
    expect(pin.press() && pin.counterSlot == 1 && legacyValue(fs) == 1,
           "counter: a legacy file that does not open does not fail the persist");
    fs.failOpenOn.clear();
    fs.loseCloseOn = COUNTER_PATH;
    expect(pin.press() && legacyValue(fs) == 1, "counter: a lost legacy close does not fail the persist");
    fs.loseCloseOn.clear();
    expect(restoredCounter(fs) == 3, "counter: a stale legacy file does not hold restore back");
  }
  {
    FakeFs fs;
    uint8_t foreign[40];
    memset(foreign, 0xEE, sizeof(foreign));
    fs.put(COUNTER_PATHS[0], foreign, sizeof(foreign));
    Pin pin(fs);
    pin.boot();
    expect(pin.press() && counterRecord(fs, 0).counter == 1, "counter: a longer old slot file is overwritten from 0");
    expect(fs.get(COUNTER_PATHS[0]).size() == 40,
           "counter: the slot is not truncated, a longer file keeps its tail (restore reads only 16 bytes)");
    expect(restoredCounter(fs) == 1, "counter: restore reads the record at the head of a longer slot file");
  }
}

// A power loss before each operation of one press, from `fs` as it is.
// Afterwards a reboot must resume at or above every press the pin emitted
// (persist returned true), and never above the press being persisted.
static void powerLossEveryStep(const FakeFs& start, const char* name) {
  // Count the operations of a clean press.
  FakeFs probe = start;
  Pin probePin(probe);
  probePin.boot();
  const uint32_t before = probePin.eventCounter;
  probe.ops = 0;
  probePin.press();
  const int total = probe.ops;
  bool ok = total > 0;
  for (int k = 1; k <= total + 1; k++) {
    FakeFs fs = start;
    Pin pin(fs);
    pin.boot();
    fs.ops = 0;
    fs.crashAtOp = k;
    const bool emitted = pin.press();
    const uint32_t lastEmitted = emitted ? before + 1 : before;
    const uint32_t resumed = restoredCounter(fs);
    expectf(resumed >= lastEmitted, name, k);
    expectf(resumed <= before + 1, name, k);
    // And the pin carries on: the next press is above everything emitted.
    Pin after(fs);
    after.boot();
    expectf(after.press() && after.eventCounter > lastEmitted && restoredCounter(fs) == after.eventCounter, name,
            k);
    if (resumed < lastEmitted) ok = false;
  }
  expect(ok, name);
}

static void testCounterPowerLoss() {
  FakeFs fresh;
  powerLossEveryStep(fresh, "counter: power loss at every step of the first press never loses an emitted press");

  FakeFs three;
  Pin pin(three);
  pin.boot();
  pin.press();
  pin.press();
  pin.press();  // the next press goes to slot b
  three.trace.clear();
  powerLossEveryStep(three, "counter: power loss at every step (slot b next) never goes backwards");
  pin.press();  // the next press goes to slot a
  powerLossEveryStep(three, "counter: power loss at every step (slot a next) never goes backwards");

  FakeFs legacyOnly;
  const uint32_t legacy = 41;
  legacyOnly.put(COUNTER_PATH, &legacy, sizeof(legacy));
  expect(restoredCounter(legacyOnly) == 41, "counter: an upgrade from the legacy file resumes at its value");
  powerLossEveryStep(legacyOnly, "counter: power loss at every step of the first slot write after an upgrade");
}

static void testCounterRestoreSelection() {
  CounterRecord a = {COUNTER_MAGIC, 50, 5, 0};
  a.checksum = counterChecksum(a);
  CounterRecord b = {COUNTER_MAGIC, 60, 6, 0};
  b.checksum = counterChecksum(b);
  {
    FakeFs fs;
    fs.put(COUNTER_PATHS[0], &a, sizeof(a));
    fs.put(COUNTER_PATHS[1], &b, sizeof(b));
    Pin pin(fs);
    pin.boot();
    expect(pin.eventCounter == 60 && pin.counterSlot == 1 && pin.counterSeq == 6, "counter restore: newest valid slot");
  }
  {
    FakeFs fs;
    CounterRecord torn = b;
    torn.checksum ^= 1;
    fs.put(COUNTER_PATHS[0], &a, sizeof(a));
    fs.put(COUNTER_PATHS[1], &torn, sizeof(torn));
    Pin pin(fs);
    pin.boot();
    expect(pin.eventCounter == 50 && pin.counterSlot == 0, "counter restore: a torn newer slot falls back to the valid one");
  }
  {
    FakeFs fs;
    fs.put(COUNTER_PATHS[0], &a, sizeof(a));
    fs.put(COUNTER_PATHS[1], &b, 8);
    Pin pin(fs);
    pin.boot();
    expect(pin.eventCounter == 50 && pin.counterSlot == 0, "counter restore: a short slot file counts as absent");
  }
  {
    FakeFs fs;
    fs.put(COUNTER_PATHS[0], &a, sizeof(a));
    fs.put(COUNTER_PATHS[1], &b, sizeof(b));
    fs.failReadOn = COUNTER_PATHS[1];
    Pin pin(fs);
    pin.boot();
    expect(pin.eventCounter == 50 && pin.counterSlot == 0, "counter restore: a slot that cannot be read counts as absent");
  }
  {
    FakeFs fs;
    CounterRecord tornA = a, tornB = b;
    tornA.checksum ^= 1;
    tornB.checksum ^= 1;
    fs.put(COUNTER_PATHS[0], &tornA, sizeof(tornA));
    fs.put(COUNTER_PATHS[1], &tornB, sizeof(tornB));
    const uint32_t legacy = 55;
    fs.put(COUNTER_PATH, &legacy, sizeof(legacy));
    Pin pin(fs);
    pin.boot();
    // selectCounterRecord's salvage: max(torn 60, legacy 55) bounded by legacy + 16.
    expect(pin.eventCounter == 60 && pin.counterSlot == -1 && pin.counterSeq == 6,
           "counter restore: two torn slots salvage within the legacy bound (counter_select.h)");
  }
}

// ---- device secret ----

static void secretBytes(uint8_t out[SECRET_LENGTH], uint8_t tag) {
  for (size_t i = 0; i < SECRET_LENGTH; i++) out[i] = (uint8_t)(tag + i * 3);
}
static bool storeSecret(FakeFs& fs, uint8_t tag, bool owned) {
  uint8_t secret[SECRET_LENGTH];
  secretBytes(secret, tag);
  return storeSecretRecord(fs, SECRET_DIR, SECRET_TEMP_PATH, SECRET_PATH, secret, owned);
}
// 1: the record with `tag`/`owned`; 0: no valid record (the sketch mints a
// new secret, or waits when it was unreadable); -1: a valid record with
// other contents.
static int loadedSecret(FakeFs& fs, uint8_t tag, bool owned) {
  SecretRecord rec;
  if (loadSecretRecord(fs, SECRET_PATH, rec) != SECRET_LOADED) return 0;
  uint8_t secret[SECRET_LENGTH];
  secretBytes(secret, tag);
  const bool same = memcmp(rec.secret, secret, SECRET_LENGTH) == 0 && ((rec.flags & SECRET_FLAG_OWNED) != 0) == owned;
  return same ? 1 : -1;
}

static void testSecretSequence() {
  const std::string len = n(sizeof(SecretRecord));
  FakeFs fs;
  expect(storeSecret(fs, 0x10, false), "secret: stored on a fresh volume");
  // writeSecretFile() before record_store.h: exists/mkdir the directory,
  // open-w temp, seek 0, write, truncate, close, read back, rename.
  expect(trace(fs) == "exists /raily; mkdir /raily; open-w /raily/device_secret.new; seek 0; write " + len +
                          "; truncate " + len + "; close; open-r /raily/device_secret.new; read " + len +
                          "; close; rename /raily/device_secret.new /raily/device_secret.bin",
         "secret store: the same file operations as before the seam");
  expect(!fs.has(SECRET_TEMP_PATH) && fs.get(SECRET_PATH).size() == sizeof(SecretRecord),
         "secret store: the temp file is renamed over the record");
  fs.trace.clear();
  expect(loadedSecret(fs, 0x10, false) == 1, "secret: loads back");
  expect(trace(fs) == "open-r /raily/device_secret.bin; read " + n(sizeof(SecretRecord) + 1) + "; close",
         "secret restore: the same file operations as before the seam (one byte more than the record)");
  fs.trace.clear();
  expect(storeSecret(fs, 0x20, true) && trace(fs).find("exists /raily; open-w") == 0,
         "secret store: an existing directory is not made again");
  expect(fs.openFiles == 0, "secret: no file left open");

  // Since 0.4.5 an open that fails asks exists(): the one sequence that
  // gained an operation. A record that opens is read as before.
  FakeFs fresh;
  SecretRecord rec;
  expect(loadSecretRecord(fresh, SECRET_PATH, rec) == SECRET_ABSENT &&
             trace(fresh) == "open-r /raily/device_secret.bin; exists /raily/device_secret.bin",
         "secret restore: no record: the failed open, then exists() says it is absent");
  fs.failOpenOn = SECRET_PATH;
  fs.trace.clear();
  expect(loadSecretRecord(fs, SECRET_PATH, rec) == SECRET_UNREADABLE &&
             trace(fs) == "open-r /raily/device_secret.bin; exists /raily/device_secret.bin",
         "secret restore: an open that fails on a record that exists is unreadable");
}

static void testSecretFailuresKeepOld() {
  struct Case {
    const char* name;
    void (*arm)(FakeFs&);
  };
  const Case cases[] = {
      {"secret: a lost close on the temp file never replaces the valid record",
       [](FakeFs& fs) { fs.loseCloseOn = SECRET_TEMP_PATH; }},
      {"secret: a short write never replaces the valid record", [](FakeFs& fs) { fs.shortWriteBy = 1; }},
      {"secret: a failed truncate never replaces the valid record", [](FakeFs& fs) { fs.failTruncate = true; }},
      {"secret: a failed rename keeps the valid record", [](FakeFs& fs) { fs.failRename = true; }},
      {"secret: a full volume never replaces the valid record", [](FakeFs& fs) { fs.full = true; }},
      {"secret: a temp file that does not open keeps the valid record",
       [](FakeFs& fs) { fs.failOpenOn = SECRET_TEMP_PATH; }},
      {"secret: a temp file that cannot be read back keeps the valid record",
       [](FakeFs& fs) { fs.failReadOn = SECRET_TEMP_PATH; }},
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    FakeFs fs;
    storeSecret(fs, 0x10, true);
    // A leftover temp file from an earlier interrupted store, as the worst case.
    uint8_t junk[50];
    memset(junk, 0x5A, sizeof(junk));
    fs.put(SECRET_TEMP_PATH, junk, sizeof(junk));
    cases[i].arm(fs);
    const bool stored = storeSecret(fs, 0x20, false);
    fs.reboot();
    expect(!stored && loadedSecret(fs, 0x10, true) == 1 && fs.openFiles == 0, cases[i].name);
  }
}

static void testSecretPowerLoss() {
  FakeFs start;
  storeSecret(start, 0x10, true);
  FakeFs probe = start;
  probe.ops = 0;
  storeSecret(probe, 0x20, false);
  const int total = probe.ops;
  bool ok = total > 0;
  for (int k = 1; k <= total + 1; k++) {
    FakeFs fs = start;
    fs.ops = 0;
    fs.crashAtOp = k;
    const bool stored = storeSecret(fs, 0x20, false);
    fs.reboot();
    const int oldOne = loadedSecret(fs, 0x10, true);
    const int newOne = loadedSecret(fs, 0x20, false);
    const bool good = stored ? newOne == 1 : (oldOne == 1 || newOne == 1);
    expectf(good, "secret: power loss keeps the old secret or the new one", k);
    // The next store after the interrupted one (a temp file may be left) works.
    expectf(storeSecret(fs, 0x30, true) && loadedSecret(fs, 0x30, true) == 1, "secret: the next store works", k);
    if (!good) ok = false;
  }
  expect(ok, "secret: power loss at every step never loses the secret (old or new, never none)");

  FakeFs fs = start;
  expect(storeSecret(fs, 0x20, false), "secret: replaced");
  fs.reboot();  // the power goes right after the rename
  expect(loadedSecret(fs, 0x20, false) == 1, "secret: a store that returned true survives a crash right after the rename");
}

static void testSecretForeignTemp() {
  FakeFs fs;
  storeSecret(fs, 0x10, true);
  uint8_t foreign[100];
  memset(foreign, 0xAB, sizeof(foreign));
  fs.put(SECRET_TEMP_PATH, foreign, sizeof(foreign));
  expect(storeSecret(fs, 0x20, false), "secret: stored over a longer foreign temp file");
  expect(fs.get(SECRET_PATH).size() == sizeof(SecretRecord) && loadedSecret(fs, 0x20, false) == 1,
         "secret: the longer foreign temp file is truncated to the record, so the next boot keeps it");
}

static void testSecretRestoreCharacterised() {
  FakeFs base;
  storeSecret(base, 0x10, true);
  const FakeFs::Bytes good = base.get(SECRET_PATH);
  struct Variant {
    const char* name;
    FakeFs::Bytes bytes;
  };
  std::vector<Variant> variants;
  {
    FakeFs::Bytes b(good.begin(), good.end() - 1);
    variants.push_back({"secret restore: a short record mints a new secret", b});
  }
  {
    FakeFs::Bytes b = good;
    b.push_back(0);
    variants.push_back(
        {"secret restore: a valid record with one byte more mints a new secret (why the store truncates)", b});
  }
  {
    FakeFs::Bytes b = good;
    b[0] ^= 0xFF;
    variants.push_back({"secret restore: a foreign magic mints a new secret", b});
  }
  {
    FakeFs::Bytes b = good;
    b[4] = 2;
    variants.push_back({"secret restore: another record version mints a new secret", b});
  }
  {
    FakeFs::Bytes b = good;
    b[5] |= 0x02;
    variants.push_back({"secret restore: an unknown flag bit mints a new secret", b});
  }
  {
    FakeFs::Bytes b = good;
    b[10] ^= 0x01;
    variants.push_back({"secret restore: a flipped secret byte (check fails) mints a new secret", b});
  }
  {
    FakeFs::Bytes b = good;
    b[b.size() - 1] ^= 0x01;
    variants.push_back({"secret restore: a flipped check byte mints a new secret", b});
  }
  variants.push_back({"secret restore: an empty file mints a new secret", FakeFs::Bytes()});
  for (size_t i = 0; i < variants.size(); i++) {
    FakeFs fs;
    fs.put(SECRET_PATH, variants[i].bytes);
    SecretRecord rec;
    const SecretLoad load = loadSecretRecord(fs, SECRET_PATH, rec);
    expect(load == SECRET_INVALID && secretLoadMints(load), variants[i].name);
  }
  {
    FakeFs fs;
    SecretRecord rec;
    const SecretLoad load = loadSecretRecord(fs, SECRET_PATH, rec);
    expect(load == SECRET_ABSENT && secretLoadMints(load), "secret restore: no record mints a new secret");
  }
  {
    FakeFs fs = base;
    fs.failReadOn = SECRET_PATH;
    SecretRecord rec;
    const SecretLoad load = loadSecretRecord(fs, SECRET_PATH, rec);
    expect(load == SECRET_UNREADABLE && !secretLoadMints(load),
           "secret restore: a read error on an intact record is unreadable and mints nothing (the owner stays)");
  }
  {
    FakeFs fs = base;
    fs.failOpenOn = SECRET_PATH;
    SecretRecord rec;
    const SecretLoad load = loadSecretRecord(fs, SECRET_PATH, rec);
    expect(load == SECRET_UNREADABLE && !secretLoadMints(load),
           "secret restore: an open that fails on an intact record is unreadable and mints nothing");
  }
  {
    FakeFs fs = base;
    expect(loadedSecret(fs, 0x10, true) == 1, "secret restore: the intact record is kept, owned flag included");
  }
  expect(!secretLoadMints(SECRET_LOADED), "secret restore: a loaded record mints nothing");
}

// The pin's secret RAM and restoreSecret()'s decision, as the sketch makes
// it: setup() once, then loop() every 5 s while the record is unreadable
// (and while a mint failed). A mint here stores tag 0x99, unowned.
struct SecretPin {
  explicit SecretPin(FakeFs& f) : fs(f) {}
  FakeFs& fs;
  bool hasSecret = false;
  bool owned = false;
  bool unreadable = false;
  bool minted = false;
  uint16_t tries = 0;
  uint8_t secret[SECRET_LENGTH] = {};
  SecretLoad restore() {
    SecretRecord rec;
    const SecretLoad load = loadSecretRecord(fs, SECRET_PATH, rec);
    unreadable = load == SECRET_UNREADABLE;
    tries = secretUnreadableTriesAfter(load, tries);
    if (load == SECRET_LOADED) {
      memcpy(secret, rec.secret, SECRET_LENGTH);
      owned = (rec.flags & SECRET_FLAG_OWNED) != 0;
      hasSecret = true;
    } else if (secretLoadMints(load, tries) && storeSecret(fs, 0x99, false)) {
      secretBytes(secret, 0x99);
      owned = false;
      hasSecret = true;
      minted = true;
    }
    return load;
  }
};

// The operations that change flash.
static bool wroteAnything(const FakeFs& fs) {
  const char* writes[] = {"open-w ", "write ", "truncate ", "rename ", "remove ", "mkdir "};
  for (size_t i = 0; i < fs.trace.size(); i++) {
    for (size_t k = 0; k < sizeof(writes) / sizeof(writes[0]); k++) {
      if (fs.trace[i].find(writes[k]) == 0) return true;
    }
  }
  return false;
}

static void testSecretRestoreOutcomes() {
  uint8_t original[SECRET_LENGTH];
  secretBytes(original, 0x10);
  {
    // (a) No record at all: a first boot mints.
    FakeFs fs;
    SecretPin pin(fs);
    expect(pin.restore() == SECRET_ABSENT && pin.hasSecret && pin.minted && !pin.owned &&
               loadedSecret(fs, 0x99, false) == 1,
           "secret outcome: no record file mints a new secret, unowned (first boot)");
  }
  {
    // (b) Read, and not a valid record: mints, as the docs say.
    FakeFs fs;
    storeSecret(fs, 0x10, true);
    FakeFs::Bytes torn = fs.get(SECRET_PATH);
    torn[8] ^= 0x01;
    fs.put(SECRET_PATH, torn);
    SecretPin pin(fs);
    expect(pin.restore() == SECRET_INVALID && pin.hasSecret && pin.minted && loadedSecret(fs, 0x99, false) == 1,
           "secret outcome: a torn record mints a new secret");
  }
  const char* const which[] = {"read", "open"};
  for (int failOpen = 0; failOpen < 2; failOpen++) {
    // (c) The record exists and does not read: no secret, nothing written,
    // read again until it reads; then the same secret and owner flag.
    FakeFs fs;
    storeSecret(fs, 0x10, true);
    const FakeFs::Bytes before = fs.get(SECRET_PATH);
    const size_t filesBefore = fs.files.size();
    if (failOpen) {
      fs.failOpenOn = SECRET_PATH;
    } else {
      fs.failReadOn = SECRET_PATH;
    }
    fs.reboot();
    fs.trace.clear();
    SecretPin pin(fs);
    bool waits = true;
    for (int retry = 0; retry < 3; retry++) {
      if (pin.restore() != SECRET_UNREADABLE || pin.hasSecret || pin.minted || !pin.unreadable) waits = false;
    }
    const std::string name = std::string("secret outcome: a failing ") + which[failOpen];
    expect(waits, (name + " leaves the pin without a secret on every try, unreadable").c_str());
    expect(!wroteAnything(fs) && fs.get(SECRET_PATH) == before && fs.files.size() == filesBefore,
           (name + " writes nothing: the record stays as it was").c_str());
    expect(fs.openFiles == 0, (name + " leaves no file open").c_str());
    fs.failOpenOn.clear();
    fs.failReadOn.clear();
    expect(pin.restore() == SECRET_LOADED && pin.hasSecret && pin.owned && !pin.minted && !pin.unreadable &&
               memcmp(pin.secret, original, SECRET_LENGTH) == 0,
           (name + " that later reads returns the original secret and owner flag").c_str());
    expect(!wroteAnything(fs) && fs.get(SECRET_PATH) == before,
           (name + ": the record was never rewritten, before or after it read").c_str());
  }
  {
    // The record goes away while it was unreadable (a later check finds no
    // file): the next try mints, as on a first boot.
    FakeFs fs;
    storeSecret(fs, 0x10, true);
    fs.failOpenOn = SECRET_PATH;
    SecretPin pin(fs);
    pin.restore();
    fs.files.erase(SECRET_PATH);
    fs.failOpenOn.clear();
    expect(!pin.hasSecret && pin.restore() == SECRET_ABSENT && pin.hasSecret && pin.minted,
           "secret outcome: a record gone after an unreadable try mints on the next try");
  }
}

// The bounded wait: a record that stays unreadable mints on the 24th read
// in a row (about 2 minutes at loop()'s 5 s), never before.
static void testSecretUnreadableBound() {
  expect(SECRET_UNREADABLE_MAX_TRIES == 24, "unreadable bound: 24 reads in a row");
  {
    uint16_t tries = 0;
    bool waits = true;
    for (uint16_t n = 1; n < SECRET_UNREADABLE_MAX_TRIES; n++) {
      tries = secretUnreadableTriesAfter(SECRET_UNREADABLE, tries);
      if (tries != n || secretLoadMints(SECRET_UNREADABLE, tries)) waits = false;
    }
    expect(waits, "unreadable bound: reads 1..23 in a row mint nothing");
    tries = secretUnreadableTriesAfter(SECRET_UNREADABLE, tries);
    expect(tries == 24 && secretLoadMints(SECRET_UNREADABLE, tries), "unreadable bound: read 24 mints");
  }
  expect(secretUnreadableTriesAfter(SECRET_UNREADABLE, 0xFFFF) == 0xFFFF &&
             secretLoadMints(SECRET_UNREADABLE, 0xFFFF),
         "unreadable bound: the count saturates and never wraps back to waiting");
  const SecretLoad others[] = {SECRET_LOADED, SECRET_ABSENT, SECRET_INVALID};
  for (size_t i = 0; i < 3; i++) {
    expect(secretUnreadableTriesAfter(others[i], 23) == 0, "unreadable bound: any other result ends the run");
  }
  for (uint16_t t = 0; t <= 30; t++) {
    if (!secretLoadMints(SECRET_ABSENT, t) || !secretLoadMints(SECRET_INVALID, t) || secretLoadMints(SECRET_LOADED, t)) {
      expect(false, "unreadable bound: absent and invalid mint at once, loaded never, whatever the count");
      break;
    }
  }
  {
    // A read that succeeds on try 23 keeps the original secret and owner.
    FakeFs fs;
    storeSecret(fs, 0x10, true);
    const FakeFs::Bytes before = fs.get(SECRET_PATH);
    fs.failReadOn = SECRET_PATH;
    fs.reboot();
    fs.trace.clear();
    SecretPin pin(fs);
    for (int n = 1; n < 23; n++) pin.restore();
    fs.failReadOn.clear();
    uint8_t original[SECRET_LENGTH];
    secretBytes(original, 0x10);
    expect(pin.restore() == SECRET_LOADED && pin.hasSecret && pin.owned && !pin.minted && pin.tries == 0 &&
               memcmp(pin.secret, original, SECRET_LENGTH) == 0 && !wroteAnything(fs) &&
               fs.get(SECRET_PATH) == before,
           "unreadable bound: a read that works on try 23 keeps the secret and owner, nothing written");
  }
  {
    // A record that never reads: 23 tries write nothing, the 24th mints.
    FakeFs fs;
    storeSecret(fs, 0x10, true);
    fs.failOpenOn = SECRET_PATH;
    fs.reboot();
    fs.trace.clear();
    SecretPin pin(fs);
    bool waits = true;
    for (int n = 1; n < 24; n++) {
      if (pin.restore() != SECRET_UNREADABLE || pin.hasSecret || pin.minted) waits = false;
    }
    expect(waits && !wroteAnything(fs), "unreadable bound: tries 1..23 leave no secret and write nothing");
    expect(pin.restore() == SECRET_UNREADABLE && pin.hasSecret && pin.minted && !pin.owned && pin.tries == 24,
           "unreadable bound: try 24 mints a new secret, unowned");
    fs.failOpenOn.clear();
    expect(loadedSecret(fs, 0x99, false) == 1, "unreadable bound: the new secret is on flash, unowned");
  }
}

// ---- admitted-phones table ----

static void bondRecordOf(size_t phones, uint8_t out[BOND_RECORD_LENGTH]) {
  BondTable table;
  memset(&table, 0, sizeof(table));
  for (size_t i = 0; i < phones; i++) {
    uint8_t addr[BOND_ADDR_LENGTH];
    for (size_t k = 0; k < BOND_ADDR_LENGTH; k++) addr[k] = (uint8_t)(0x10 * (i + 1) + k);
    table.admit(addr, 1, NULL, NULL);
  }
  encodeBondTable(table, out);
}
static bool storeBonds(FakeFs& fs, size_t phones) {
  uint8_t rec[BOND_RECORD_LENGTH];
  bondRecordOf(phones, rec);
  return storeRecordAtomically(fs, SECRET_DIR, BONDS_TEMP_PATH, BONDS_PATH, rec, sizeof(rec));
}
// The phone count of the stored table, -1 when it does not load.
static int loadedBonds(FakeFs& fs) {
  static uint8_t buffer[BOND_RECORD_LENGTH + 1];
  BondTable table;
  if (!loadBondTableRecord(fs, BONDS_PATH, buffer, sizeof(buffer), table)) return -1;
  return table.count;
}

static void testBondTable() {
  const std::string len = n(BOND_RECORD_LENGTH);
  {
    FakeFs fs;
    fs.dirs.insert(SECRET_DIR);
    expect(storeBonds(fs, 2), "bonds: stored");
    // writeBondTableFile() before record_store.h, with the record read back
    // in 32-byte chunks.
    std::string readBack;
    for (size_t at = 0; at < BOND_RECORD_LENGTH; at += 32) {
      readBack += "read " + n(BOND_RECORD_LENGTH - at < 32 ? BOND_RECORD_LENGTH - at : 32) + "; ";
    }
    expect(trace(fs) == "exists /raily; open-w /raily/bonds.new; seek 0; write " + len + "; truncate " + len +
                            "; close; open-r /raily/bonds.new; " + readBack +
                            "close; rename /raily/bonds.new /raily/bonds.bin",
           "bonds store: the same file operations as before the seam");
    fs.trace.clear();
    expect(loadedBonds(fs) == 2, "bonds: load back");
    expect(trace(fs) == "open-r /raily/bonds.bin; read " + n(BOND_RECORD_LENGTH + 1) + "; close",
           "bonds restore: the same file operations as before the seam");
  }
  const char* names[] = {"bonds: a lost close keeps the old table", "bonds: a short write keeps the old table",
                         "bonds: a failed truncate keeps the old table", "bonds: a failed rename keeps the old table",
                         "bonds: a full volume keeps the old table"};
  for (int i = 0; i < 5; i++) {
    FakeFs fs;
    storeBonds(fs, 1);
    if (i == 0) fs.loseCloseOn = BONDS_TEMP_PATH;
    if (i == 1) fs.shortWriteBy = 7;
    if (i == 2) fs.failTruncate = true;
    if (i == 3) fs.failRename = true;
    if (i == 4) fs.full = true;
    const bool stored = storeBonds(fs, 3);
    fs.reboot();
    expect(!stored && loadedBonds(fs) == 1 && fs.openFiles == 0, names[i]);
  }
  {
    FakeFs start;
    storeBonds(start, 1);
    FakeFs probe = start;
    probe.ops = 0;
    storeBonds(probe, 3);
    const int total = probe.ops;
    bool ok = total > 0;
    for (int k = 1; k <= total + 1; k++) {
      FakeFs fs = start;
      fs.ops = 0;
      fs.crashAtOp = k;
      const bool stored = storeBonds(fs, 3);
      fs.reboot();
      const int count = loadedBonds(fs);
      const bool good = stored ? count == 3 : (count == 1 || count == 3);
      expectf(good, "bonds: power loss keeps the old table or the new one", k);
      if (!good) ok = false;
    }
    expect(ok, "bonds: power loss at every step keeps the old table or the new one, never a torn one");
  }
  {
    FakeFs fs;
    storeBonds(fs, 1);
    FakeFs::Bytes foreign(BOND_RECORD_LENGTH * 2, 0x77);
    fs.put(BONDS_TEMP_PATH, foreign);
    expect(storeBonds(fs, 2) && fs.get(BONDS_PATH).size() == BOND_RECORD_LENGTH && loadedBonds(fs) == 2,
           "bonds: a longer foreign temp file is truncated to the record");
    FakeFs::Bytes longer = fs.get(BONDS_PATH);
    longer.push_back(0);
    fs.put(BONDS_PATH, longer);
    expect(loadedBonds(fs) == -1, "bonds restore: a longer record file is rejected (every phone admitted again)");
  }
  {
    FakeFs fs;
    storeBonds(fs, 2);
    fs.trace.clear();
    expect(eraseRecordFile(fs, BONDS_PATH) && !fs.has(BONDS_PATH) &&
               trace(fs) == "exists /raily/bonds.bin; remove /raily/bonds.bin",
           "bonds erase: exists then remove (clearBondsLocked)");
    fs.trace.clear();
    expect(eraseRecordFile(fs, BONDS_PATH) && trace(fs) == "exists /raily/bonds.bin",
           "bonds erase: nothing to erase is success");
  }
}

// ---- mascot byte ----

static int loadedMascot(FakeFs& fs) {
  uint8_t value = MASCOT_DEFAULT;
  return loadMascotRecord(fs, MASCOT_PATH, &value) ? value : -1;
}

static void testMascot() {
  const uint8_t v1 = 0x21, v2 = 0x52;
  expect(mascotValid(v1) && mascotValid(v2), "mascot: test values are valid mascots");
  {
    FakeFs fs;
    expect(storeMascotRecord(fs, SECRET_DIR, MASCOT_PATH, v1), "mascot: stored");
    // writeMascotFile() before record_store.h, in place, and since 0.4.5
    // the read-back the counter and the secret have.
    expect(trace(fs) == "exists /raily; mkdir /raily; open-w /raily/mascot.bin; seek 0; write 4; truncate 4; close; "
                        "open-r /raily/mascot.bin; read 4; close",
           "mascot store: in place, then read back");
    fs.trace.clear();
    expect(loadedMascot(fs) == v1 && trace(fs) == "open-r /raily/mascot.bin; read 5; close",
           "mascot restore: loads back with the same file operations");
    expect(storeMascotRecord(fs, SECRET_DIR, MASCOT_PATH, v2) && loadedMascot(fs) == v2, "mascot: rewritten in place");
  }
  {
    FakeFs fs;
    storeMascotRecord(fs, SECRET_DIR, MASCOT_PATH, v1);
    fs.loseCloseOn = MASCOT_PATH;
    const bool reported = storeMascotRecord(fs, SECRET_DIR, MASCOT_PATH, v2);
    fs.reboot();
    expect(!reported && loadedMascot(fs) == v1,
           "mascot: a lost close is detected by the read-back: the write fails and the old mascot stays");
    fs.loseCloseOn.clear();
    expect(storeMascotRecord(fs, SECRET_DIR, MASCOT_PATH, v2) && loadedMascot(fs) == v2,
           "mascot: the retry after a lost close stores the value");
  }
  {
    FakeFs fs;
    storeMascotRecord(fs, SECRET_DIR, MASCOT_PATH, v1);
    fs.shortWriteBy = 1;
    expect(!storeMascotRecord(fs, SECRET_DIR, MASCOT_PATH, v2), "mascot: a short write fails");
    fs.shortWriteBy = 0;
    expect(loadedMascot(fs) == -1,
           "mascot: a short write still commits a torn record in place: it reads as no record (default mascot)");
    expect(storeMascotRecord(fs, SECRET_DIR, MASCOT_PATH, v2) && loadedMascot(fs) == v2,
           "mascot: the retry after a short write stores the value");
  }
  {
    // The sketch's loop: persistMascot() with MascotPersist's hourly limit.
    // A failed write stays pending and is written again after the hour,
    // never on every pass.
    FakeFs fs;
    MascotPersist store = {};
    storeMascotRecord(fs, SECRET_DIR, MASCOT_PATH, v1);
    store.loaded(v1);
    store.offer(v2, true);
    uint8_t value = 0;
    fs.loseCloseOn = MASCOT_PATH;
    const bool due = store.due(0, &value);
    store.written(0, value, storeMascotRecord(fs, SECRET_DIR, MASCOT_PATH, value));
    fs.loseCloseOn.clear();
    expect(due && value == v2 && !store.due(MASCOT_PERSIST_MIN_MS - 1, &value),
           "mascot: a failed write is not retried before the hour");
    expect(store.due(MASCOT_PERSIST_MIN_MS, &value) && value == v2, "mascot: the failed value is still pending");
    store.written(MASCOT_PERSIST_MIN_MS, value, storeMascotRecord(fs, SECRET_DIR, MASCOT_PATH, value));
    expect(loadedMascot(fs) == v2 && !store.due(3 * MASCOT_PERSIST_MIN_MS, &value),
           "mascot: the retry after the hour stores it and nothing is pending");
  }
  {
    FakeFs fs;
    storeMascotRecord(fs, SECRET_DIR, MASCOT_PATH, v1);
    fs.failTruncate = true;
    expect(!storeMascotRecord(fs, SECRET_DIR, MASCOT_PATH, v2), "mascot: a failed truncate fails the write");
  }
  {
    FakeFs fs;
    FakeFs::Bytes foreign(9, 0x42);
    fs.put(MASCOT_PATH, foreign);
    expect(storeMascotRecord(fs, SECRET_DIR, MASCOT_PATH, v2) && fs.get(MASCOT_PATH).size() == MASCOT_RECORD_LENGTH &&
               loadedMascot(fs) == v2,
           "mascot: a longer foreign file is truncated to the record");
    FakeFs::Bytes longer = fs.get(MASCOT_PATH);
    longer.push_back(0);
    fs.put(MASCOT_PATH, longer);
    expect(loadedMascot(fs) == -1, "mascot restore: a longer file reads as no record");
  }
  for (int existing = 0; existing < 2; existing++) {
    FakeFs start;
    if (existing) storeMascotRecord(start, SECRET_DIR, MASCOT_PATH, v1);
    FakeFs probe = start;
    probe.ops = 0;
    storeMascotRecord(probe, SECRET_DIR, MASCOT_PATH, v2);
    const int total = probe.ops;
    bool ok = total > 0;
    for (int k = 1; k <= total + 1; k++) {
      FakeFs fs = start;
      fs.ops = 0;
      fs.crashAtOp = k;
      // Read back: a write that returned true is on flash.
      const bool stored = storeMascotRecord(fs, SECRET_DIR, MASCOT_PATH, v2);
      fs.reboot();
      const int value = loadedMascot(fs);
      const int before = existing ? v1 : -1;  // no file, or an empty new one: no record
      const bool good = stored || k > total ? value == v2 : (value == before || value == v2);
      expectf(good, "mascot: power loss keeps the old mascot or the new one", k);
      if (!good) ok = false;
    }
    expect(ok, existing ? "mascot: power loss at every step of a rewrite keeps the old mascot or the new one"
                        : "mascot: power loss at every step of a first write leaves no record or the new one");
  }
  {
    FakeFs fs;
    storeMascotRecord(fs, SECRET_DIR, MASCOT_PATH, v1);
    expect(eraseRecordFile(fs, MASCOT_PATH) && loadedMascot(fs) == -1, "mascot: erased (release, unowned boot)");
  }
}

int main() {
  testFileStartsWith();
  testCounterSequence();
  testCounterAlternates();
  testCounterLostClose();
  testCounterOtherFailures();
  testCounterPowerLoss();
  testCounterRestoreSelection();
  testSecretSequence();
  testSecretFailuresKeepOld();
  testSecretPowerLoss();
  testSecretForeignTemp();
  testSecretRestoreCharacterised();
  testSecretRestoreOutcomes();
  testSecretUnreadableBound();
  testBondTable();
  testMascot();
  if (failures) {
    printf("%d FAILURES\n", failures);
  } else {
    printf("ALL PASS\n");
  }
  return failures ? 1 : 0;
}

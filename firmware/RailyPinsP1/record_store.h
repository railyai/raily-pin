#pragma once

// The flash steps that store and read back the records a pin cannot lose:
// the press counter (counter_select.h), the device secret (secret_record.h),
// the admitted-phones table (bond_table.h) and the mascot byte
// (mascot_record.h). Each step is a function template over a file-system
// type, so the sketch runs it on InternalFS (a thin adapter in
// RailyPinsP1.ino) and tests/test_record_store.cpp runs the very same code on
// a RAM fake that can lose a close, shorten a write or cut the power
// between any two operations (tests/fake_fs.h).
//
// The file-system type `Fs` mirrors Adafruit_LittleFS (Seeed core 1.1.13)
// and needs only what these steps use:
//   bool fs.exists(path); bool fs.mkdir(path); bool fs.remove(path);
//   bool fs.rename(from, to);
//   typename Fs::File file(fs);            // a closed file, like File(InternalFS)
//   bool file.open(path, RECORD_O_READ or RECORD_O_WRITE);
//   bool file.seek(pos); size_t file.write(bytes, n);  // the count stored
//   int file.read(buffer, uint16_t n);     // the count read, < 0 on an error
//   bool file.truncate(size); void file.close();
// Adafruit_LittleFS quirks these steps depend on (the fake keeps them):
// - RECORD_O_WRITE (FILE_O_WRITE) opens read-write + create and positions at
//   the END, not O_APPEND: seek(0) rewrites the record in place, and a longer
//   old file keeps its tail unless it is truncated.
// - Opening a new file for writing creates it, empty, at once.
// - close() returns nothing: lfs_file_close()'s result is dropped, and a
//   small write sits in the cache until close, so a full write() count does
//   not prove the record reached flash. Only reading it back does.
// - rename() replaces the target atomically: a power loss leaves the old
//   record or the new one.
//
// Pure: no Arduino, no RTOS, no static state. Callers keep their own locks
// (the counter persist runs under emitMutex, the rest on the loop task).

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bond_table.h"
#include "counter_select.h"
#include "mascot_record.h"
#include "secret_record.h"

// Adafruit_LittleFS's FILE_O_READ / FILE_O_WRITE (the sketch asserts it).
enum { RECORD_O_READ = 0, RECORD_O_WRITE = 1 };

template <class Fs>
static inline void ensureRecordDir(Fs& fs, const char* dir) {
  if (!fs.exists(dir)) fs.mkdir(dir);
}

// True when there is no file left at `path`.
template <class Fs>
static inline bool eraseRecordFile(Fs& fs, const char* path) {
  return !fs.exists(path) || fs.remove(path);
}

// True when `path` starts with exactly these bytes. Adafruit_LittleFS's
// File::close() drops lfs_file_close()'s result, and a small write sits in
// the cache until close, so a full write() count does not prove the record
// reached flash: only reading it back does. Its own frame (the File
// object), 32 bytes compared at a time, the chunk wiped (it may hold the
// secret). No flash write and no static state: the counter's caller also
// runs on the Ada callback task (press_command, under emitMutex).
// A prefix match is enough on purpose: the secret, the bond table and the
// mascot truncate the file to the record before this check, and the
// counter's restore reads only the record's 16 bytes.
template <class Fs>
static __attribute__((noinline)) bool fileStartsWith(Fs& fs, const char* path, const uint8_t* expected,
                                                     size_t length) {
  typename Fs::File file(fs);
  if (!file.open(path, RECORD_O_READ)) return false;
  uint8_t chunk[32];
  bool same = true;
  for (size_t at = 0; same && at < length; at += sizeof(chunk)) {
    const size_t n = length - at < sizeof(chunk) ? length - at : sizeof(chunk);
    same = file.read(chunk, (uint16_t)n) == (int)n && memcmp(chunk, expected + at, n) == 0;
  }
  file.close();
  memset(chunk, 0, sizeof(chunk));
  return same;
}

// Up to `capacity` bytes of `path` into `buffer`. False when the file does
// not open. `*length` is File::read()'s int as a size_t, as the sketch has
// always taken it: a read error becomes a huge length no decoder accepts.
template <class Fs>
static inline bool readRecordFile(Fs& fs, const char* path, uint8_t* buffer, uint16_t capacity, size_t* length) {
  typename Fs::File file(fs);
  if (!file.open(path, RECORD_O_READ)) return false;
  *length = file.read(buffer, capacity);
  file.close();
  return true;
}

// ---- Press counter (two ping-pong slots plus the legacy bare-u32 file) ----

// Persists `counter` to the slot that does not hold the last valid record.
// On success `*seq` and `*slot` move to the new record; on any failure they
// stay as they were (the caller's counter stays bumped: see emitButtonEvent).
template <class Fs>
static inline bool persistCounterRecord(Fs& fs, const char* const slotPaths[2], const char* legacyPath,
                                        uint32_t counter, uint32_t* seq, int8_t* slot) {
  // Ping-pong: overwrite the slot that is NOT the newest valid one, so a
  // torn write can never destroy the last good record.
  // Total for every *slot value, including -1 (no valid slot yet).
  int target = (*slot == 0) ? 1 : 0;
  CounterRecord rec = {COUNTER_MAGIC, counter, *seq + 1, 0};
  rec.checksum = counterChecksum(rec);
  typename Fs::File file(fs);
  if (!file.open(slotPaths[target], RECORD_O_WRITE)) return false;
  // FILE_O_WRITE is append mode in Adafruit_LittleFS — seek back to 0
  // so the file stays a single record.
  file.seek(0);
  size_t written = file.write((const uint8_t*)&rec, sizeof(rec));
  file.close();
  if (written != sizeof(rec)) return false;
  // Read back: a close that failed leaves the old record, not this one.
  if (!fileStartsWith(fs, slotPaths[target], (const uint8_t*)&rec, sizeof(rec))) return false;
  *seq = rec.seq;
  *slot = (int8_t)target;
  // Best-effort dual-write of the legacy bare-u32 file AFTER the validated
  // slot commit: keeps it current so a downgrade to pre-slot firmware does
  // not resume from a stale floor, and gives restore a plausible bound for
  // torn-slot salvage. Failure here must not fail the persist — the slot
  // record is authoritative.
  typename Fs::File legacy(fs);
  if (legacy.open(legacyPath, RECORD_O_WRITE)) {
    legacy.seek(0);
    legacy.write((const uint8_t*)&counter, sizeof(counter));
    legacy.close();
  }
  return true;
}

// Reads both slots and the legacy file; the decision itself is
// selectCounterRecord() (counter_select.h).
template <class Fs>
static inline CounterDecision restoreCounterRecord(Fs& fs, const char* const slotPaths[2], const char* legacyPath) {
  CounterSlotCandidate slots[2] = {};
  for (int slot = 0; slot < 2; slot++) {
    typename Fs::File file(fs);
    if (!file.open(slotPaths[slot], RECORD_O_READ)) continue;
    CounterRecord rec = {};
    if (file.read(&rec, sizeof(rec)) == (int)sizeof(rec)) {
      slots[slot].present = true;
      slots[slot].rec = rec;
    }
    file.close();
  }
  // The legacy bare-u32 file can coexist with a torn slot after an interrupted
  // upgrade. It has no integrity field, but remains a plausible lower bound.
  bool hasLegacy = false;
  uint32_t legacy = 0;
  typename Fs::File legacyFile(fs);
  if (legacyFile.open(legacyPath, RECORD_O_READ)) {
    hasLegacy = legacyFile.read(&legacy, sizeof(legacy)) == (int)sizeof(legacy);
    legacyFile.close();
  }
  return selectCounterRecord(slots[0], slots[1], hasLegacy, legacy);
}

// ---- Temp file + rename (the secret and the admitted-phones table) ----

enum RecordStage { RECORD_NOT_OPENED, RECORD_NOT_STAGED, RECORD_STAGED };

// Writes `bytes` as the whole of `path` and reads it back. RECORD_STAGED only
// when the file now reads back as exactly the record.
template <class Fs>
static inline RecordStage stageRecordFile(Fs& fs, const char* path, const uint8_t* bytes, size_t length) {
  typename Fs::File file(fs);
  if (!file.open(path, RECORD_O_WRITE)) return RECORD_NOT_OPENED;
  // FILE_O_WRITE opens read-write + create and seeks to the end (not
  // O_APPEND), so seek(0) rewrites the record in place, as the counter
  // slots do.
  file.seek(0);
  // A longer foreign file must not survive past the record, or the next
  // boot would reject it again (the secret: mint a new one every time).
  const bool ok = file.write(bytes, length) == length && file.truncate(length);
  file.close();
  // Only a temp file that reads back as the record replaces the valid one.
  return ok && fileStartsWith(fs, path, bytes, length) ? RECORD_STAGED : RECORD_NOT_STAGED;
}

// Temp file + rename: LittleFS renames atomically, so a power loss keeps
// the old record or the new one, never a torn one.
template <class Fs>
static inline bool storeRecordAtomically(Fs& fs, const char* dir, const char* tempPath, const char* finalPath,
                                         const uint8_t* bytes, size_t length) {
  ensureRecordDir(fs, dir);
  // Renamed only once the temp file reads back as the record (fileStartsWith).
  return stageRecordFile(fs, tempPath, bytes, length) == RECORD_STAGED && fs.rename(tempPath, finalPath);
}

// The secret record through the same steps as storeRecordAtomically, with
// the record's bytes wiped before the rename. A torn record would make the
// next boot mint a new secret (the owner lost).
template <class Fs>
static inline bool storeSecretRecord(Fs& fs, const char* dir, const char* tempPath, const char* finalPath,
                                     const uint8_t secret[SECRET_LENGTH], bool owned) {
  ensureRecordDir(fs, dir);
  SecretRecord rec = makeSecretRecord(secret, owned);
  const RecordStage stage = stageRecordFile(fs, tempPath, (const uint8_t*)&rec, sizeof(rec));
  // Wiped on every path, a temp file that does not open included (since
  // 0.4.5): the record holds the secret.
  memset(&rec, 0, sizeof(rec));
  return stage == RECORD_STAGED && fs.rename(tempPath, finalPath);
}

// What restoreSecret found. ABSENT and INVALID mint a new secret at once (the
// owner is lost, docs/pins/firmware.md «Server pass»): a record that exists
// but cannot be opened or read (an lfs error, a failed allocation) may be
// perfectly good, so the pin waits without a secret and reads it again, up
// to SECRET_UNREADABLE_MAX_TRIES reads in one boot.
enum SecretLoad {
  SECRET_LOADED,      // a valid record: the secret and the owned flag
  SECRET_ABSENT,      // no record file: a first boot
  SECRET_INVALID,     // read, and not a valid record (size, magic, version, check)
  SECRET_UNREADABLE,  // the file exists, but the open or the read failed
};

static inline bool secretLoadMints(SecretLoad load) {
  return load == SECRET_ABSENT || load == SECRET_INVALID;
}

// The wait for an unreadable record is bounded: setup() reads once, then
// loop() every 5 s, so 24 reads in a row is about 2 minutes. A passing lfs
// or allocation error is gone long before; a record that still does not
// read is damaged flash, and waiting longer would strand the pin (no pass,
// so no use, release or BLE update). Then it counts as invalid: a new
// secret, as before 0.4.5.
static const uint16_t SECRET_UNREADABLE_MAX_TRIES = 24;

// The count of unreadable reads in a row, this load included: any other
// result ends the run. Saturates, so it never wraps back to waiting.
static inline uint16_t secretUnreadableTriesAfter(SecretLoad load, uint16_t tries) {
  if (load != SECRET_UNREADABLE) return 0;
  return tries < 0xFFFF ? (uint16_t)(tries + 1) : tries;
}

// Whether this load mints, given `tries` from secretUnreadableTriesAfter().
static inline bool secretLoadMints(SecretLoad load, uint16_t tries) {
  if (load == SECRET_UNREADABLE) return tries >= SECRET_UNREADABLE_MAX_TRIES;
  return secretLoadMints(load);
}

// The normal paths are open, read, close as before; only an open that fails
// asks exists(), to tell a missing record from one that did not open. The
// buffer is wiped (it holds the secret).
template <class Fs>
static inline SecretLoad loadSecretRecord(Fs& fs, const char* path, SecretRecord& out) {
  typename Fs::File file(fs);
  if (!file.open(path, RECORD_O_READ)) return fs.exists(path) ? SECRET_UNREADABLE : SECRET_ABSENT;
  uint8_t buffer[sizeof(SecretRecord) + 1];
  const int length = file.read(buffer, sizeof(buffer));
  file.close();
  SecretLoad load = SECRET_UNREADABLE;
  if (length >= 0) load = readSecretRecord(buffer, (size_t)length, out) ? SECRET_LOADED : SECRET_INVALID;
  memset(buffer, 0, sizeof(buffer));
  return load;
}

// The admitted-phones record into `out` (decodeBondTable); `buffer` is the
// caller's, BOND_RECORD_LENGTH + 1 bytes, so a longer file is rejected.
template <class Fs>
static inline bool loadBondTableRecord(Fs& fs, const char* path, uint8_t* buffer, uint16_t capacity,
                                       BondTable& out) {
  size_t length = 0;
  if (!readRecordFile(fs, path, buffer, capacity, &length)) return false;
  return decodeBondTable(buffer, length, out);
}

// ---- Mascot byte: written in place, then read back ----

// One LittleFS close is atomic, so a power loss leaves the old record or
// the new one (or an empty new file, which reads as no record). Read back
// like the records above (since 0.4.5): a lost close or a short write is a
// failure, and the caller keeps the value pending (MascotPersist::written).
// A short write can still leave a torn record in place; it reads as no
// record (the default mascot) until the retry rewrites it.
template <class Fs>
static inline bool storeMascotRecord(Fs& fs, const char* dir, const char* path, uint8_t value) {
  ensureRecordDir(fs, dir);
  uint8_t rec[MASCOT_RECORD_LENGTH];
  mascotRecordEncode(value, rec);
  typename Fs::File file(fs);
  if (!file.open(path, RECORD_O_WRITE)) return false;
  file.seek(0);  // FILE_O_WRITE seeks to the end
  bool ok = file.write(rec, sizeof(rec)) == sizeof(rec) && file.truncate(sizeof(rec));
  file.close();
  // The file was truncated to the record, so a prefix match is the record.
  return ok && fileStartsWith(fs, path, rec, sizeof(rec));
}

template <class Fs>
static inline bool loadMascotRecord(Fs& fs, const char* path, uint8_t* value) {
  uint8_t buffer[MASCOT_RECORD_LENGTH + 1];
  size_t length = 0;
  if (!readRecordFile(fs, path, buffer, sizeof(buffer), &length)) return false;
  return mascotRecordDecode(buffer, length, value);
}

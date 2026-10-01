#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hmac_sha256.h"

// BLE bonding, stage 1 (docs/pins/ble-bonding.md). Bluefruit pairs any
// phone (LESC Just Works) and keeps one bond file per phone; this header
// holds what the pin adds on top: which bonds the Raily server admitted
// (an admit pass on that phone's encrypted link, D-2), the rule for a
// trusted link (D-3), which bond files to evict above the cap (D-5), the
// admitted table's flash record (/raily/bonds.bin, D-4) and the
// Service Changed bookkeeping after an update (D-7). Pure, host-tested in
// hardware/firmware/tests/test_bond_table.cpp.

static const size_t BOND_TABLE_MAX = 8;       // admitted phones, and bond files kept
static const size_t BOND_ADDR_LENGTH = 6;     // a BLE identity address
static const uint8_t LINK_KEY_SIZE_REQUIRED = 16;  // iOS always offers 16; Bluefruit accepts 7
static const uint8_t LINK_SECURITY_LEVEL_ENCRYPTED = 2;  // mode 1 level 2: Just Works

struct AdmittedPhone {
  uint8_t addr[BOND_ADDR_LENGTH];  // the bond's identity address, as Bluefruit names its file
  uint8_t addrType;                // kept for the record; matching uses the 6 bytes
  uint8_t reserved;
  uint32_t seq;           // admission order: the oldest is evicted first
  uint32_t firmwareHash;  // firmwareHash() of the build this phone last confirmed a Service Changed for
  uint32_t keyPrint;      // bondKeyFingerprint() of the admitted pairing's keys; 0 = not taken yet
} __attribute__((packed));

// The first 4 bytes of SHA-256 over a Bluefruit bond file's key field (its
// first field: a length byte, then the 80-byte bond_keys_t), never 0.
// 0 when the file does not start with a key field. An admission is bound to
// these keys: an entry whose bond file now holds other keys (a pairing the
// pin saved but never revoked, e.g. a power cut right after it) is dropped
// at boot (ble-bonding.md D-2).
static const size_t BOND_KEYS_LENGTH = 80;  // sizeof(bond_keys_t), utility/bonding.h
static inline uint32_t bondKeyFingerprint(const uint8_t* file, size_t length) {
  if (file == NULL || length < 1 + BOND_KEYS_LENGTH || file[0] != BOND_KEYS_LENGTH) return 0;
  uint8_t digest[32];
  sha256(file + 1, BOND_KEYS_LENGTH, digest);
  uint32_t print = (uint32_t)digest[0] | ((uint32_t)digest[1] << 8) | ((uint32_t)digest[2] << 16) |
                   ((uint32_t)digest[3] << 24);
  return print == 0 ? 1u : print;
}

// FNV-1a of the firmware version string: a new build = a new GATT table as
// far as a bonded phone's cache is concerned (D-7).
static inline uint32_t firmwareHash(const char* version) {
  uint32_t hash = 2166136261u;
  for (const char* p = version; p != NULL && *p; p++) {
    hash ^= (uint8_t)*p;
    hash *= 16777619u;
  }
  return hash;
}

struct BondTable {
  AdmittedPhone phones[BOND_TABLE_MAX];
  uint8_t count;
  uint32_t nextSeq;

  int find(const uint8_t addr[BOND_ADDR_LENGTH]) const {
    for (size_t i = 0; i < count; i++) {
      if (memcmp(phones[i].addr, addr, BOND_ADDR_LENGTH) == 0) return (int)i;
    }
    return -1;
  }

  bool contains(const uint8_t addr[BOND_ADDR_LENGTH]) const { return find(addr) >= 0; }

  // Admits a phone for this firmware. An admitted phone stays where it is
  // (idempotent). A full table evicts the oldest admission, copied to
  // *evicted so the caller removes its bond file too. Returns true when the
  // table changed.
  bool admit(const uint8_t addr[BOND_ADDR_LENGTH], uint8_t addrType, uint32_t fwHash,
             AdmittedPhone* evicted, bool* didEvict) {
    if (didEvict) *didEvict = false;
    if (contains(addr)) return false;
    if (count >= BOND_TABLE_MAX) {
      size_t oldest = 0;
      for (size_t i = 1; i < count; i++) {
        if (phones[i].seq < phones[oldest].seq) oldest = i;
      }
      if (evicted) *evicted = phones[oldest];
      if (didEvict) *didEvict = true;
      removeAt(oldest);
    }
    AdmittedPhone& phone = phones[count++];
    memset(&phone, 0, sizeof(phone));
    memcpy(phone.addr, addr, BOND_ADDR_LENGTH);
    phone.addrType = addrType;
    phone.seq = nextSeq++;
    phone.firmwareHash = fwHash;  // it just discovered this build's table
    phone.keyPrint = 0;           // taken by loop() from the bond file
    return true;
  }

  // The first admitted phone whose key fingerprint is not taken yet, or -1.
  int pendingKeyPrint() const {
    for (size_t i = 0; i < count; i++) {
      if (phones[i].keyPrint == 0) return (int)i;
    }
    return -1;
  }

  bool setKeyPrint(const uint8_t addr[BOND_ADDR_LENGTH], uint32_t print) {
    int at = find(addr);
    if (at < 0 || print == 0) return false;
    phones[at].keyPrint = print;
    return true;
  }

  // At boot: an entry stays only while its bond file holds the keys it was
  // admitted with. `print` is bondKeyFingerprint() of that file now (0: no
  // file, or unreadable). An entry that never got its fingerprint is dropped
  // too: its phone is admitted again by its app. Returns true on a drop.
  bool keepIfSameKeys(const uint8_t addr[BOND_ADDR_LENGTH], uint32_t print) {
    int at = find(addr);
    if (at < 0) return false;
    if (print != 0 && phones[at].keyPrint == print) return false;
    removeAt((size_t)at);
    return true;
  }

  // A fresh pairing, or its bond evicted: the phone must be admitted again.
  bool revoke(const uint8_t addr[BOND_ADDR_LENGTH]) {
    int at = find(addr);
    if (at < 0) return false;
    removeAt((size_t)at);
    return true;
  }

  void clear() {
    memset(phones, 0, sizeof(phones));
    count = 0;
    nextSeq = 0;
  }

  // Only admitted phones get a Service Changed indication, once per build.
  bool serviceChangedDue(const uint8_t addr[BOND_ADDR_LENGTH], uint32_t fwHash) const {
    int at = find(addr);
    return at >= 0 && phones[at].firmwareHash != fwHash;
  }

  // The phone confirmed the indication. Returns true when the table changed.
  bool markServiceChanged(const uint8_t addr[BOND_ADDR_LENGTH], uint32_t fwHash) {
    int at = find(addr);
    if (at < 0 || phones[at].firmwareHash == fwHash) return false;
    phones[at].firmwareHash = fwHash;
    return true;
  }

 private:
  void removeAt(size_t at) {
    for (size_t i = at; i + 1 < count; i++) phones[i] = phones[i + 1];
    count--;
    memset(&phones[count], 0, sizeof(phones[count]));
  }
};

// One link's security: mode 1 level (1 open, 2 encrypted by Just Works),
// the negotiated key size, and the bond's identity address when the link
// runs on stored bond keys. The sketch fills it from the SoftDevice.
struct LinkSecurity {
  uint8_t level;
  uint8_t keySize;
  bool bonded;
  uint8_t addrType;
  uint8_t addr[BOND_ADDR_LENGTH];
};

// Link rules (D-2, D-3). secLevel is the SoftDevice's security mode 1
// level, keySize the negotiated encryption key size, bonded whether the
// link runs on stored bond keys (Bluefruit's BLEConnection::bonded()).
static inline bool linkReadyForAdmission(uint8_t secLevel, uint8_t keySize, bool bonded) {
  return secLevel >= LINK_SECURITY_LEVEL_ENCRYPTED && keySize >= LINK_KEY_SIZE_REQUIRED && bonded;
}

static inline bool linkTrusted(bool owned, uint8_t secLevel, uint8_t keySize, bool bonded, bool admitted) {
  return owned && admitted && linkReadyForAdmission(secLevel, keySize, bonded);
}

// link_security (7B1E0010): version, flags, bonds stored, phones admitted.
static const size_t LINK_SECURITY_LENGTH = 4;
static const uint8_t LINK_SECURITY_VERSION = 1;
static const uint8_t LINK_FLAG_BONDED = 0x01;
static const uint8_t LINK_FLAG_TRUSTED = 0x02;  // this link is admitted: the app may send the full screen_state
static const uint8_t LINK_FLAG_OWNED = 0x04;

static inline void buildLinkSecurity(bool bonded, bool trusted, bool owned, size_t bonds, size_t admitted,
                                     uint8_t out[LINK_SECURITY_LENGTH]) {
  out[0] = LINK_SECURITY_VERSION;
  out[1] = (uint8_t)((bonded ? LINK_FLAG_BONDED : 0) | (trusted ? LINK_FLAG_TRUSTED : 0) |
                     (owned ? LINK_FLAG_OWNED : 0));
  out[2] = (uint8_t)(bonds > 255 ? 255 : bonds);
  out[3] = (uint8_t)(admitted > 255 ? 255 : admitted);
}

// A Bluefruit bond file name: the identity address as 12 upper-case hex
// digits, addr[0] first (utility/bonding.cpp get_fname).
static inline bool parseBondFileName(const char* name, uint8_t out[BOND_ADDR_LENGTH]) {
  if (name == NULL || strlen(name) != 2 * BOND_ADDR_LENGTH) return false;
  uint8_t parsed[BOND_ADDR_LENGTH] = {0};
  for (size_t i = 0; i < 2 * BOND_ADDR_LENGTH; i++) {
    char c = name[i];
    uint8_t nibble;
    if (c >= '0' && c <= '9') {
      nibble = (uint8_t)(c - '0');
    } else if (c >= 'A' && c <= 'F') {
      nibble = (uint8_t)(c - 'A' + 10);
    } else if (c >= 'a' && c <= 'f') {
      nibble = (uint8_t)(c - 'a' + 10);
    } else {
      return false;
    }
    parsed[i / 2] = (uint8_t)(parsed[i / 2] | (i % 2 == 0 ? nibble << 4 : nibble));
  }
  memcpy(out, parsed, BOND_ADDR_LENGTH);
  return true;
}

// Which bond files to delete so at most maxBonds stay (D-5): bonds nobody
// admitted first, in listing order, then admitted ones from the oldest
// admission. The `keep` address (the current link's bond, may be NULL) is
// never chosen. Writes indices into `files` to `out` and returns how many.
static inline size_t selectBondEvictions(const uint8_t (*files)[BOND_ADDR_LENGTH], size_t fileCount,
                                         const BondTable& table, const uint8_t* keep, size_t maxBonds,
                                         size_t* out, size_t outCap) {
  if (fileCount <= maxBonds) return 0;
  size_t excess = fileCount - maxBonds;
  size_t chosen = 0;
  for (size_t i = 0; i < fileCount && chosen < excess && chosen < outCap; i++) {
    if (keep != NULL && memcmp(files[i], keep, BOND_ADDR_LENGTH) == 0) continue;
    if (!table.contains(files[i])) out[chosen++] = i;
  }
  while (chosen < excess && chosen < outCap) {
    size_t best = fileCount;
    uint32_t bestSeq = 0;
    for (size_t i = 0; i < fileCount; i++) {
      if (keep != NULL && memcmp(files[i], keep, BOND_ADDR_LENGTH) == 0) continue;
      int at = table.find(files[i]);
      if (at < 0) continue;
      bool taken = false;
      for (size_t k = 0; k < chosen; k++) taken = taken || out[k] == i;
      if (taken) continue;
      if (best == fileCount || table.phones[at].seq < bestSeq) {
        best = i;
        bestSeq = table.phones[at].seq;
      }
    }
    if (best == fileCount) break;  // only the kept bond is left
    out[chosen++] = best;
  }
  return chosen;
}

// /raily/bonds.bin: {magic "RPB1", version, count, reserved[2], nextSeq,
// phones[8], check}. The check bytes are the first 4 bytes of SHA-256 over
// the fields before them, as in the secret record: integrity only. A
// torn or foreign record decodes to nothing, and the caller starts empty
// (every phone is admitted again by its app).
static const uint32_t BOND_RECORD_MAGIC = 0x31425052;  // "RPB1" little-endian
static const uint8_t BOND_RECORD_VERSION = 1;

struct BondRecord {
  uint32_t magic;
  uint8_t version;
  uint8_t count;
  uint8_t reserved[2];
  uint32_t nextSeq;
  AdmittedPhone phones[BOND_TABLE_MAX];
  uint8_t check[4];
} __attribute__((packed));

static const size_t BOND_RECORD_LENGTH = sizeof(BondRecord);

// Over the record's bytes in place: no copy of the 144-byte record on the
// stack (the loop task has 4 KB).
static inline void bondRecordCheck(const uint8_t* rec, uint8_t out[4]) {
  uint8_t digest[32];
  sha256(rec, offsetof(BondRecord, check), digest);
  memcpy(out, digest, 4);
}

static inline void encodeBondTable(const BondTable& table, uint8_t out[BOND_RECORD_LENGTH]) {
  memset(out, 0, BOND_RECORD_LENGTH);
  BondRecord* rec = (BondRecord*)out;  // packed: any alignment
  rec->magic = BOND_RECORD_MAGIC;
  rec->version = BOND_RECORD_VERSION;
  rec->count = table.count;
  rec->nextSeq = table.nextSeq;
  memcpy(rec->phones, table.phones, sizeof(rec->phones));
  bondRecordCheck(out, rec->check);
}

// `out` is written only for a complete, current, intact record.
static inline bool decodeBondTable(const uint8_t* data, size_t length, BondTable& out) {
  if (data == NULL || length != BOND_RECORD_LENGTH) return false;
  const BondRecord* rec = (const BondRecord*)data;
  if (rec->magic != BOND_RECORD_MAGIC || rec->version != BOND_RECORD_VERSION || rec->count > BOND_TABLE_MAX) {
    return false;
  }
  uint8_t expected[4];
  bondRecordCheck(data, expected);
  if (!constantTimeEqual(expected, rec->check, sizeof(expected))) return false;
  for (size_t i = 0; i < rec->count; i++) {
    if (rec->phones[i].seq >= rec->nextSeq) return false;  // a sequence from the future is not ours
    for (size_t k = 0; k < i; k++) {
      if (memcmp(rec->phones[k].addr, rec->phones[i].addr, BOND_ADDR_LENGTH) == 0) return false;  // one entry per phone
    }
  }
  memset(&out, 0, sizeof(out));
  memcpy(out.phones, rec->phones, (size_t)rec->count * sizeof(AdmittedPhone));
  out.count = rec->count;
  out.nextSeq = rec->nextSeq;
  return true;
}

// Serial `i`'s "bond" object (docs/pins/firmware.md, serial protocol):
// bond files, admitted phones, and the current link's encryption, level,
// key size and trust. No address is ever printed.
static inline int formatBondBenchJson(char* buf, size_t size, size_t bonds, size_t admitted, bool encrypted,
                                      uint8_t level, uint8_t keySize, bool trusted) {
  return snprintf(buf, size, "\"bond\":{\"n\":%u,\"adm\":%u,\"enc\":%u,\"lv\":%u,\"ks\":%u,\"trust\":%u}",
                  (unsigned)bonds, (unsigned)admitted, encrypted ? 1u : 0u, (unsigned)level,
                  (unsigned)keySize, trusted ? 1u : 0u);
}

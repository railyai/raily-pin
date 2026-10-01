#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "hmac_sha256.h"

// The pin's secret S and its owned flag, stored as one record in
// /raily/device_secret.bin. The check bytes are the first 4 bytes of
// SHA-256 over the fields before them: integrity only. A missing, torn or
// foreign record is treated as a fresh pin (a new S, unowned), and the
// owner's app re-keys it (docs/pins/security-bonding-secure-dfu.md §3.8).

static const uint32_t SECRET_RECORD_MAGIC = 0x31535052;  // "RPS1" little-endian
static const uint8_t SECRET_RECORD_VERSION = 1;
static const uint8_t SECRET_FLAG_OWNED = 0x01;
static const size_t SECRET_LENGTH = 16;

struct SecretRecord {
  uint32_t magic;
  uint8_t version;
  uint8_t flags;
  uint8_t reserved[2];
  uint8_t secret[SECRET_LENGTH];
  uint8_t check[4];
} __attribute__((packed));

static inline void secretRecordCheck(const SecretRecord& rec, uint8_t out[4]) {
  uint8_t digest[32];
  sha256((const uint8_t*)&rec, offsetof(SecretRecord, check), digest);
  memcpy(out, digest, 4);
}

static inline SecretRecord makeSecretRecord(const uint8_t secret[SECRET_LENGTH], bool owned) {
  SecretRecord rec;
  memset(&rec, 0, sizeof(rec));
  rec.magic = SECRET_RECORD_MAGIC;
  rec.version = SECRET_RECORD_VERSION;
  rec.flags = owned ? SECRET_FLAG_OWNED : 0;
  memcpy(rec.secret, secret, SECRET_LENGTH);
  secretRecordCheck(rec, rec.check);
  return rec;
}

// Returns true only for a complete, current, intact record.
static inline bool readSecretRecord(const uint8_t* data, size_t length, SecretRecord& out) {
  if (data == NULL || length != sizeof(SecretRecord)) return false;
  SecretRecord rec;
  memcpy(&rec, data, sizeof(rec));
  if (rec.magic != SECRET_RECORD_MAGIC || rec.version != SECRET_RECORD_VERSION) return false;
  if ((rec.flags & ~SECRET_FLAG_OWNED) != 0) return false;
  uint8_t expected[4];
  secretRecordCheck(rec, expected);
  if (!constantTimeEqual(expected, rec.check, sizeof(expected))) return false;
  out = rec;
  return true;
}

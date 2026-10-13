#pragma once

#include <stddef.h>
#include <stdint.h>

// The CCCD field of a Bluefruit bond file (docs/pins/ble-bonding.md D-5).
// A file is three fields, each a length byte and its bytes: the 80-byte
// keys, the peer name, and the SoftDevice's system attributes
// (sd_ble_gatts_sys_attr_get: entries of handle u16, length u16, value,
// then a CRC-16 over the entries, little-endian).
//
// Seeed nRF52 1.1.13 bond_save_cccd_dfr() writes a new field one byte late
// when its length differs from the stored one (it has already read the old
// length byte), so after a build with a different CCCD count the file holds
// the stale length byte, then the real field, then the stale tail.
// bond_load_cccd() reads the stale length, sd_ble_gatts_sys_attr_set()
// rejects the shifted bytes, and every CCCD reads 0 at reconnection until
// the phone pairs again. The pin finds the real field and writes it back in
// place before Bluefruit reads it. Pure, host-tested in
// hardware/firmware/tests/test_bond_cccd_file.cpp.

static const size_t BOND_FILE_KEYS_LENGTH = 80;  // sizeof(bond_keys_t), utility/bonding.h
static const size_t SYS_ATTR_ENTRY_HEADER = 4;   // handle u16, value length u16

// Nordic's crc16_compute (CRC-16/CCITT-FALSE), the CRC the SoftDevice
// appends to a system-attributes blob.
static inline uint16_t sysAttrCrc16(const uint8_t* data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; i++) {
    crc = (uint16_t)((crc >> 8) | (crc << 8));
    crc ^= data[i];
    crc ^= (uint16_t)((crc & 0xFF) >> 4);
    crc ^= (uint16_t)(crc << 12);
    crc ^= (uint16_t)((crc & 0xFF) << 5);
  }
  return crc;
}

// The CRC stored at the end of a blob (little-endian), 0 for a short one.
static inline uint16_t sysAttrStoredCrc(const uint8_t* blob, size_t length) {
  if (blob == NULL || length < 2) return 0;
  return (uint16_t)(blob[length - 2] | (blob[length - 1] << 8));
}

// At least one entry, entries that end exactly at the CRC, and the CRC.
static inline bool sysAttrBlobValid(const uint8_t* blob, size_t length) {
  if (blob == NULL || length < SYS_ATTR_ENTRY_HEADER + 2) return false;
  const size_t body = length - 2;
  size_t at = 0;
  while (at < body) {
    if (body - at < SYS_ATTR_ENTRY_HEADER) return false;
    const size_t valueLength = (size_t)blob[at + 2] | ((size_t)blob[at + 3] << 8);
    at += SYS_ATTR_ENTRY_HEADER;
    if (valueLength > body - at) return false;
    at += valueLength;
  }
  return sysAttrCrc16(blob, body) == sysAttrStoredCrc(blob, length);
}

enum BondCccdAction : uint8_t {
  BOND_CCCD_NONE = 0,     // no key and name fields, or no CCCD field yet: Bluefruit's own path
  BOND_CCCD_RESTORE = 1,  // a valid field: rewrite the file when `rewrite`
  BOND_CCCD_DROP = 2,     // CCCD bytes but no valid field: cut them off
};

struct BondCccdPlan {
  BondCccdAction action;
  bool rewrite;      // the field is not where (or not as long as) Bluefruit expects it
  size_t start;      // offset of the CCCD field's length byte: the file ends here after a drop
  size_t blob;       // RESTORE: offset of the valid blob
  uint8_t length;    // RESTORE: its length
};

// Where the CCCD field starts in `file`, or 0 when the key and name fields
// do not parse (then the pin does not touch the file).
static inline size_t bondCccdFieldStart(const uint8_t* file, size_t size) {
  if (file == NULL || size < 1 + BOND_FILE_KEYS_LENGTH + 1) return 0;
  if (file[0] != BOND_FILE_KEYS_LENGTH) return 0;
  const size_t name = 1 + BOND_FILE_KEYS_LENGTH;
  const size_t nameLength = file[name];
  if (nameLength == 0 || name + 1 + nameLength > size) return 0;
  return name + 1 + nameLength;
}

// bond_save_cccd_dfr() writes either right after the stale length byte or
// so that the field ends the file. With this GATT table (blobs of 20 bytes
// and more) each such write overlaps every older field, so at most one
// valid field is left; the scan takes the one nearest the end.
static inline BondCccdPlan planBondCccd(const uint8_t* file, size_t size) {
  BondCccdPlan plan = {BOND_CCCD_NONE, false, 0, 0, 0};
  const size_t start = bondCccdFieldStart(file, size);
  if (start == 0 || start == size) return plan;
  plan.start = start;
  // Clean: the field at its place, valid, ending the file.
  const size_t first = file[start];
  if (start + 1 + first == size && sysAttrBlobValid(file + start + 1, first)) {
    plan.action = BOND_CCCD_RESTORE;
    plan.blob = start + 1;
    plan.length = (uint8_t)first;
    return plan;
  }
  for (size_t at = size - 1; at + 1 > start; at--) {
    const size_t length = file[at];
    if (at + 1 + length > size || !sysAttrBlobValid(file + at + 1, length)) continue;
    plan.action = BOND_CCCD_RESTORE;
    plan.rewrite = true;
    plan.blob = at + 1;
    plan.length = (uint8_t)length;
    return plan;
  }
  plan.action = BOND_CCCD_DROP;
  plan.rewrite = true;
  return plan;
}

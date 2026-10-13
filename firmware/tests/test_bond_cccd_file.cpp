// Host tests for bond_cccd_file.h (docs/pins/ble-bonding.md D-5).
#include <stdio.h>
#include <string.h>

#include "../RailyPinsP1/bond_cccd_file.h"

static int failures = 0;
static void expect(bool condition, const char* name) {
  printf("%s %s\n", condition ? "PASS" : "FAIL", name);
  if (!condition) failures++;
}

// The bench pin's CCCD blob (rp1-aae1c86b9b4c74e9, Elena's iPhone,
// 2026-10-09): Service Changed 0x000D = 0x0002, button_event 0x001E and
// power_state 0x0031 = 0x0001, CRC 0x0813.
static const uint8_t BENCH_BLOB[] = {0x0d, 0x00, 0x02, 0x00, 0x02, 0x00, 0x1e, 0x00, 0x02, 0x00,
                                     0x01, 0x00, 0x31, 0x00, 0x02, 0x00, 0x01, 0x00, 0x13, 0x08};
// The same phone's file as the pin held it: the stale length byte 0x1A
// of an earlier 26-byte blob (a fourth CCCD, 0x0034), the real field, the
// stale tail (CRC 0x238F of the old blob).
static const uint8_t BENCH_FIELD[] = {0x1a, 0x14, 0x0d, 0x00, 0x02, 0x00, 0x02, 0x00, 0x1e, 0x00,
                                      0x02, 0x00, 0x01, 0x00, 0x31, 0x00, 0x02, 0x00, 0x01, 0x00,
                                      0x13, 0x08, 0x00, 0x00, 0x00, 0x8f, 0x23};

static const size_t NAME_LENGTH = 20;
static const size_t FIELD_START = 1 + 80 + 1 + NAME_LENGTH;  // 102

// keys (80) + name (20) + the given CCCD bytes, as Bluefruit lays it out.
static size_t bondFile(uint8_t* out, const uint8_t* cccd, size_t cccdLength) {
  size_t at = 0;
  out[at++] = 80;
  for (int i = 0; i < 80; i++) out[at++] = (uint8_t)(0xA0 + i);
  out[at++] = (uint8_t)NAME_LENGTH;
  for (size_t i = 0; i < NAME_LENGTH - 1; i++) out[at++] = (uint8_t)('a' + i);
  out[at++] = 0;
  if (cccdLength > 0) memcpy(out + at, cccd, cccdLength);
  return at + cccdLength;
}

static void testCrcAndBlob() {
  expect(sysAttrCrc16(BENCH_BLOB, 18) == 0x0813, "the SoftDevice CRC of the bench blob");
  expect(sysAttrBlobValid(BENCH_BLOB, sizeof(BENCH_BLOB)), "the bench blob is valid");
  expect(sysAttrStoredCrc(BENCH_BLOB, sizeof(BENCH_BLOB)) == 0x0813 && sysAttrStoredCrc(BENCH_BLOB, 1) == 0,
         "the stored CRC is read little-endian");
  expect(!sysAttrBlobValid(BENCH_FIELD + 1, 0x1a), "the stale 26-byte read is not");
  uint8_t flipped[sizeof(BENCH_BLOB)];
  memcpy(flipped, BENCH_BLOB, sizeof(flipped));
  flipped[4] ^= 0x01;
  expect(!sysAttrBlobValid(flipped, sizeof(flipped)), "a changed value fails the CRC");
  memcpy(flipped, BENCH_BLOB, sizeof(flipped));
  flipped[2] = 0x03;  // value length past the CRC
  expect(!sysAttrBlobValid(flipped, sizeof(flipped)), "an entry past the CRC fails");
  const uint8_t crcOnly[] = {0xff, 0xff};
  expect(!sysAttrBlobValid(crcOnly, sizeof(crcOnly)), "no entries is not a blob");
  expect(!sysAttrBlobValid(NULL, 20), "no bytes");
}

static void testBenchFile() {
  uint8_t file[256];
  const size_t size = bondFile(file, BENCH_FIELD, sizeof(BENCH_FIELD));
  expect(size == 129, "the bench file is 129 bytes");
  expect(bondCccdFieldStart(file, size) == FIELD_START, "the CCCD field starts after keys and name");
  const BondCccdPlan plan = planBondCccd(file, size);
  expect(plan.action == BOND_CCCD_RESTORE && plan.rewrite, "the bench file is restored and rewritten");
  expect(plan.start == FIELD_START && plan.blob == FIELD_START + 2 && plan.length == 20,
         "the real field is one byte late");
  expect(memcmp(file + plan.blob, BENCH_BLOB, sizeof(BENCH_BLOB)) == 0, "it is the bench blob");
}

static void testCleanFile() {
  uint8_t field[1 + sizeof(BENCH_BLOB)];
  field[0] = sizeof(BENCH_BLOB);
  memcpy(field + 1, BENCH_BLOB, sizeof(BENCH_BLOB));
  uint8_t file[256];
  const size_t size = bondFile(file, field, sizeof(field));
  const BondCccdPlan plan = planBondCccd(file, size);
  expect(plan.action == BOND_CCCD_RESTORE && !plan.rewrite && plan.blob == FIELD_START + 1 && plan.length == 20,
         "a clean file is set again, never rewritten");
  // A valid field with bytes after it: Bluefruit's next save of the same
  // length would seek to the wrong place, so the file is cut.
  uint8_t longer[256];
  memcpy(longer, file, size);
  longer[size] = 0x00;
  longer[size + 1] = 0x8f;
  const BondCccdPlan trailing = planBondCccd(longer, size + 2);
  expect(trailing.action == BOND_CCCD_RESTORE && trailing.rewrite && trailing.blob == FIELD_START + 1,
         "a valid field with a tail is rewritten");
}

static void testNoField() {
  uint8_t file[256];
  const size_t size = bondFile(file, NULL, 0);
  expect(planBondCccd(file, size).action == BOND_CCCD_NONE, "a new bond with no CCCD field is left alone");
  file[0] = 79;
  expect(bondCccdFieldStart(file, size) == 0 && planBondCccd(file, size).action == BOND_CCCD_NONE,
         "a file without the 80-byte key field is left alone");
  uint8_t shortFile[90] = {80};
  expect(bondCccdFieldStart(shortFile, sizeof(shortFile)) == 0, "a cut key field is left alone");
}

static void testGarbage() {
  const uint8_t garbage[] = {0x1a, 0x14, 0x0d, 0x00, 0x02};  // length bytes past the end
  uint8_t file[256];
  const size_t size = bondFile(file, garbage, sizeof(garbage));
  const BondCccdPlan plan = planBondCccd(file, size);
  expect(plan.action == BOND_CCCD_DROP && plan.rewrite && plan.start == FIELD_START,
         "CCCD bytes with no valid field are cut off");
}

// The other misplacement: a stale length byte equal to the new length
// sends Bluefruit's write to size - (len + 1), past the first real field.
static void testLatestWins() {
  uint8_t older[1 + 26];
  older[0] = 26;
  // A valid 26-byte blob: the bench entries plus 0x0034 = 0, CRC 0x238F.
  const uint8_t oldBlob[] = {0x0d, 0x00, 0x02, 0x00, 0x02, 0x00, 0x1e, 0x00, 0x02, 0x00, 0x01, 0x00, 0x31,
                             0x00, 0x02, 0x00, 0x01, 0x00, 0x34, 0x00, 0x02, 0x00, 0x00, 0x00, 0x8f, 0x23};
  expect(sysAttrBlobValid(oldBlob, sizeof(oldBlob)), "the bench pin's earlier 26-byte blob");
  memcpy(older + 1, oldBlob, sizeof(oldBlob));
  // [stale 20][26-byte field], then a 20-byte write at size - 21.
  uint8_t cccd[1 + sizeof(older)];
  cccd[0] = 20;
  memcpy(cccd + 1, older, sizeof(older));
  uint8_t file[256];
  size_t size = bondFile(file, cccd, sizeof(cccd));
  file[size - 21] = 20;
  memcpy(file + size - 20, BENCH_BLOB, sizeof(BENCH_BLOB));
  const BondCccdPlan plan = planBondCccd(file, size);
  expect(plan.action == BOND_CCCD_RESTORE && plan.rewrite && plan.blob == size - 20 && plan.length == 20,
         "the latest write wins over an older field");
}

int main() {
  testCrcAndBlob();
  testBenchFile();
  testCleanFile();
  testNoField();
  testGarbage();
  testLatestWins();
  if (failures) {
    printf("%d bond_cccd_file test(s) failed\n", failures);
    return 1;
  }
  printf("all bond_cccd_file tests passed\n");
  return 0;
}

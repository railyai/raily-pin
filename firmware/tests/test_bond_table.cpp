// Host tests for bond_table.h (docs/pins/ble-bonding.md D-2 to D-7).
#include <stdio.h>
#include <string.h>

#include "../RailyPinsP1/bond_table.h"

static int failures = 0;
static void expect(bool condition, const char* name) {
  printf("%s %s\n", condition ? "PASS" : "FAIL", name);
  if (!condition) failures++;
}

static void addr(uint8_t out[6], uint8_t tag) {
  for (int i = 0; i < 6; i++) out[i] = (uint8_t)(tag + i);
}

static BondTable emptyTable() {
  BondTable table;
  memset(&table, 0, sizeof(table));
  return table;
}

static void testAdmitRevoke() {
  BondTable table = emptyTable();
  uint8_t a[6], b[6];
  addr(a, 0x10);
  addr(b, 0x20);
  bool evicted = true;
  expect(table.admit(a, 1, 0xAAAA, NULL, &evicted) && !evicted && table.count == 1, "first admission");
  expect(!table.admit(a, 1, 0xBBBB, NULL, &evicted) && table.count == 1, "admitting again changes nothing");
  expect(table.phones[0].firmwareHash == 0xAAAA, "a repeat keeps the stored build hash");
  expect(table.contains(a) && !table.contains(b), "contains only the admitted address");
  expect(table.admit(b, 1, 0xAAAA, NULL, NULL) && table.phones[1].seq > table.phones[0].seq,
         "admissions are ordered");
  expect(table.revoke(a) && !table.contains(a) && table.contains(b) && table.count == 1,
         "a fresh pairing revokes only that phone");
  expect(!table.revoke(a), "revoking twice is a no-op");
  table.clear();
  expect(table.count == 0 && !table.contains(b), "release clears every admission");
}

static void testAdmitEvictsOldest() {
  BondTable table = emptyTable();
  uint8_t a[6];
  for (uint8_t i = 0; i < BOND_TABLE_MAX; i++) {
    addr(a, (uint8_t)(0x40 + 8 * i));
    table.admit(a, 1, 1, NULL, NULL);
  }
  uint8_t second[6];
  addr(second, 0x48);
  table.revoke(second);
  table.admit(second, 1, 1, NULL, NULL);  // now the newest
  uint8_t ninth[6];
  addr(ninth, 0xF0);
  AdmittedPhone gone = {};
  bool evicted = false;
  expect(table.admit(ninth, 1, 1, &gone, &evicted) && evicted && table.count == BOND_TABLE_MAX,
         "a ninth phone evicts one");
  uint8_t first[6];
  addr(first, 0x40);
  expect(memcmp(gone.addr, first, 6) == 0 && !table.contains(first), "the oldest admission goes");
  expect(table.contains(second) && table.contains(ninth), "a re-admitted phone counts as new");
}

static void testTrust() {
  expect(linkReadyForAdmission(2, 16, true), "Just Works, 16-byte key, bonded: ready");
  expect(linkReadyForAdmission(4, 16, true), "a higher level is ready too");
  expect(!linkReadyForAdmission(1, 16, true), "plaintext is not ready");
  expect(!linkReadyForAdmission(2, 7, true), "a short key is not ready");
  expect(!linkReadyForAdmission(2, 16, false), "encrypted without a bond is not ready");
  expect(linkTrusted(true, 2, 16, true, true), "owned, ready, admitted: trusted");
  expect(!linkTrusted(false, 2, 16, true, true), "an unowned pin trusts nobody");
  expect(!linkTrusted(true, 2, 16, true, false), "an encrypted stranger is not trusted");
  expect(!linkTrusted(true, 1, 16, true, true), "an admitted phone on plaintext is not trusted");

  uint8_t value[LINK_SECURITY_LENGTH];
  buildLinkSecurity(true, true, true, 3, 2, value);
  expect(value[0] == 1 && value[1] == 0x07 && value[2] == 3 && value[3] == 2, "link_security, everything set");
  buildLinkSecurity(true, false, true, 300, 0, value);
  expect(value[1] == (LINK_FLAG_BONDED | LINK_FLAG_OWNED) && value[2] == 255 && value[3] == 0,
         "link_security saturates and leaves reserved bits clear");
}

static void testFileNames() {
  uint8_t out[6] = {0};
  expect(parseBondFileName("0A1B2C3D4E5F", out) && out[0] == 0x0A && out[5] == 0x5F, "Bluefruit file name");
  expect(parseBondFileName("0a1b2c3d4e5f", out) && out[1] == 0x1B, "lower case too");
  uint8_t keep[6] = {1, 2, 3, 4, 5, 6};
  memcpy(out, keep, 6);
  expect(!parseBondFileName("0A1B2C3D4E5", out) && memcmp(out, keep, 6) == 0, "short name rejected, out untouched");
  expect(!parseBondFileName("0A1B2C3D4E5FF", out), "long name rejected");
  expect(!parseBondFileName("0A1B2C3D4E5G", out) && memcmp(out, keep, 6) == 0, "non-hex rejected, out untouched");
  expect(!parseBondFileName(NULL, out), "no name");
}

static void testEvictions() {
  BondTable table = emptyTable();
  uint8_t files[12][6];
  for (uint8_t i = 0; i < 12; i++) addr(files[i], (uint8_t)(0x10 * i));
  size_t out[12];
  expect(selectBondEvictions(files, 8, table, NULL, 8, out, 12) == 0, "at the cap nothing goes");

  // Owner phones 1 and 5 admitted, the rest strangers; the current link is 3.
  table.admit(files[5], 1, 1, NULL, NULL);
  table.admit(files[1], 1, 1, NULL, NULL);
  size_t n = selectBondEvictions(files, 11, table, files[3], 8, out, 12);
  expect(n == 3 && out[0] == 0 && out[1] == 2 && out[2] == 4, "strangers go first, the current link stays");

  // Eight admitted phones plus the current stranger link: a stranger pairing
  // again and again never evicts an owner's phone...
  BondTable eight = emptyTable();
  for (uint8_t i = 0; i < 8; i++) eight.admit(files[i], 1, 1, NULL, NULL);
  uint8_t nine[10][6];
  for (uint8_t i = 0; i < 10; i++) memcpy(nine[i], files[i], 6);
  n = selectBondEvictions(nine, 10, eight, nine[9], 8, out, 12);
  expect(n == 2 && out[0] == 8 && out[1] != 9, "the other stranger goes, then the oldest admission");
  expect(out[1] == 0, "the oldest admission is the second choice");

  // ...unless every other bond is admitted: then the oldest admission goes,
  // whatever the listing order.
  BondTable ordered = emptyTable();
  for (uint8_t i = 3; i < 11; i++) ordered.admit(files[i], 1, 1, NULL, NULL);  // 3 oldest, 10 newest
  uint8_t listed[9][6];
  memcpy(listed[0], files[11], 6);                                   // the current link, not admitted
  for (uint8_t i = 1; i < 9; i++) memcpy(listed[i], files[11 - i], 6);  // 10, 9, ... 3: newest first
  n = selectBondEvictions(listed, 9, ordered, files[11], 8, out, 12);
  expect(n == 1 && out[0] == 8, "all others admitted: the oldest admission goes");

  n = selectBondEvictions(files, 12, table, NULL, 0, out, 2);
  expect(n == 2, "never more than the output holds");
  uint8_t only[1][6];
  memcpy(only[0], files[0], 6);
  expect(selectBondEvictions(only, 1, table, files[0], 0, out, 12) == 0, "the kept bond is never removed");
}

static void testServiceChanged() {
  BondTable table = emptyTable();
  uint8_t a[6], b[6];
  addr(a, 0x70);
  addr(b, 0x80);
  const uint32_t oldBuild = firmwareHash("0.2.13-qa");
  const uint32_t newBuild = firmwareHash("0.2.14-qa");
  expect(oldBuild != newBuild, "every build has its own hash");
  expect(firmwareHash("") == 2166136261u, "FNV-1a offset basis");
  expect(firmwareHash("a") == 0xE40C292Cu, "FNV-1a reference value");
  table.admit(a, 1, oldBuild, NULL, NULL);
  expect(table.serviceChangedDue(a, newBuild), "an update is due for an admitted phone");
  expect(!table.serviceChangedDue(a, oldBuild), "nothing due on the same build");
  expect(!table.serviceChangedDue(b, newBuild), "never for a phone that is not admitted");
  expect(table.markServiceChanged(a, newBuild) && !table.serviceChangedDue(a, newBuild),
         "the phone's confirmation stores the build");
  expect(!table.markServiceChanged(a, newBuild), "a second confirmation changes nothing");
  expect(!table.markServiceChanged(b, newBuild), "a stranger's confirmation stores nothing");
  table.admit(b, 1, newBuild, NULL, NULL);
  expect(!table.serviceChangedDue(b, newBuild), "a phone admitted on this build already knows its table");
}

static void testRecord() {
  expect(BOND_RECORD_LENGTH == 176 && sizeof(AdmittedPhone) == 20, "documented record layout");
  BondTable table = emptyTable();
  uint8_t a[6], b[6];
  addr(a, 0x11);
  addr(b, 0x22);
  table.admit(a, 1, 7, NULL, NULL);
  table.admit(b, 0, 9, NULL, NULL);
  uint8_t bytes[BOND_RECORD_LENGTH];
  encodeBondTable(table, bytes);
  expect(bytes[0] == 'R' && bytes[1] == 'P' && bytes[2] == 'B' && bytes[3] == '1', "magic RPB1");
  BondTable back = emptyTable();
  expect(decodeBondTable(bytes, sizeof(bytes), back) && back.count == 2 && back.contains(a) && back.contains(b) &&
             back.nextSeq == table.nextSeq && back.phones[1].firmwareHash == 9,
         "round trip");
  BondTable untouched = emptyTable();
  untouched.count = 5;
  uint8_t torn[BOND_RECORD_LENGTH];
  memcpy(torn, bytes, sizeof(torn));
  torn[20] ^= 0x01;
  expect(!decodeBondTable(torn, sizeof(torn), untouched) && untouched.count == 5, "a flipped bit is refused");
  expect(!decodeBondTable(bytes, sizeof(bytes) - 1, untouched), "a short file is refused");
  expect(!decodeBondTable(NULL, sizeof(bytes), untouched), "no data");
  BondTable bad = table;
  bad.count = BOND_TABLE_MAX + 1;
  uint8_t tooMany[BOND_RECORD_LENGTH];
  encodeBondTable(bad, tooMany);
  expect(!decodeBondTable(tooMany, sizeof(tooMany), untouched), "a count above eight is refused");
  BondTable twice = table;
  memcpy(twice.phones[1].addr, twice.phones[0].addr, 6);
  uint8_t duplicated[BOND_RECORD_LENGTH];
  encodeBondTable(twice, duplicated);
  expect(!decodeBondTable(duplicated, sizeof(duplicated), untouched), "one phone twice is refused");
  BondTable future = table;
  future.nextSeq = 1;  // entry seq 1 is not below it
  uint8_t fromFuture[BOND_RECORD_LENGTH];
  encodeBondTable(future, fromFuture);
  expect(!decodeBondTable(fromFuture, sizeof(fromFuture), untouched), "a sequence from the future is refused");
}

static void testKeyPrint() {
  uint8_t file[1 + BOND_KEYS_LENGTH + 20];
  memset(file, 0x5A, sizeof(file));
  file[0] = BOND_KEYS_LENGTH;
  const uint32_t print = bondKeyFingerprint(file, sizeof(file));
  expect(print != 0, "a bond file has a fingerprint");
  file[1 + BOND_KEYS_LENGTH + 3] ^= 0xFF;  // the name and CCCD fields after the keys
  expect(bondKeyFingerprint(file, sizeof(file)) == print, "only the keys count");
  file[40] ^= 0x01;
  const uint32_t other = bondKeyFingerprint(file, sizeof(file));
  expect(other != 0 && other != print, "other keys, another fingerprint");
  file[0] = 12;  // a keyless file (a CCCD saved after an erase)
  expect(bondKeyFingerprint(file, sizeof(file)) == 0, "no key field, no fingerprint");
  expect(bondKeyFingerprint(file, BOND_KEYS_LENGTH) == 0, "short file, no fingerprint");
  expect(bondKeyFingerprint(NULL, 100) == 0, "no file");

  BondTable table = emptyTable();
  uint8_t a[6], b[6];
  addr(a, 0x31);
  addr(b, 0x41);
  table.admit(a, 1, 1, NULL, NULL);
  table.admit(b, 1, 1, NULL, NULL);
  expect(table.pendingKeyPrint() == 0, "a new admission waits for its fingerprint");
  expect(table.setKeyPrint(a, print) && table.pendingKeyPrint() == 1, "taken in admission order");
  expect(!table.setKeyPrint(a, 0), "zero is never a fingerprint");
  expect(!table.keepIfSameKeys(a, print) && table.contains(a), "same keys at boot: kept");
  expect(table.keepIfSameKeys(a, other) && !table.contains(a), "the bond now holds other keys: dropped");
  expect(table.keepIfSameKeys(b, other) && !table.contains(b), "never fingerprinted: dropped at boot");
  table.admit(a, 1, 1, NULL, NULL);
  table.setKeyPrint(a, print);
  expect(table.keepIfSameKeys(a, 0) && !table.contains(a), "the bond file is gone: dropped");
  expect(!table.keepIfSameKeys(b, print), "a phone not in the table: nothing to drop");
}

static void testBench() {
  char buf[128];
  int n = formatBondBenchJson(buf, sizeof(buf), 255, 8, true, 4, 16, true);
  expect(n > 0 && n < 96, "bench object fits its buffer at every field's top");
  expect(strcmp(buf, "\"bond\":{\"n\":255,\"adm\":8,\"enc\":1,\"lv\":4,\"ks\":16,\"trust\":1}") == 0,
         "bench object text");
}

int main() {
  testAdmitRevoke();
  testAdmitEvictsOldest();
  testTrust();
  testFileNames();
  testEvictions();
  testServiceChanged();
  testRecord();
  testKeyPrint();
  testBench();
  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("bond_table: all passed\n");
  return 0;
}

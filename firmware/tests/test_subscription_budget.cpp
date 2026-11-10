// Host tests for subscription_budget.h (docs/pins/ble-bonding.md D-5).
#include <stdio.h>
#include <stdlib.h>

#include "../RailyPinsP1/bond_cccd_file.h"
#include "../RailyPinsP1/subscription_budget.h"

static int failures = 0;

static void expect(bool ok, const char* what) {
  printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) failures++;
}

// A link's subscriptions as the SoftDevice's system-attributes blob holds
// them (the handles of the bench GATT dump, ble-bonding.md D-5, plus the
// battery level): entries of handle, length, value, then the CRC-16.
struct Subs {
  uint16_t serviceChanged;  // 0x000D, written by iOS itself
  uint16_t button;          // 0x001E
  uint16_t power;           // 0x0031
  uint16_t battery;         // 0x0040 (the Battery Service, on boards that have it)
};

static uint32_t printOf(const Subs& subs) {
  const uint16_t handles[4] = {0x000D, 0x001E, 0x0031, 0x0040};
  const uint16_t values[4] = {subs.serviceChanged, subs.button, subs.power, subs.battery};
  uint8_t blob[4 * 6 + 2];
  size_t at = 0;
  for (int i = 0; i < 4; i++) {
    blob[at++] = (uint8_t)(handles[i] & 0xFF);
    blob[at++] = (uint8_t)(handles[i] >> 8);
    blob[at++] = 2;
    blob[at++] = 0;
    blob[at++] = (uint8_t)(values[i] & 0xFF);
    blob[at++] = (uint8_t)(values[i] >> 8);
  }
  const uint16_t crc = sysAttrCrc16(blob, at);
  blob[at++] = (uint8_t)(crc & 0xFF);
  blob[at++] = (uint8_t)(crc >> 8);
  return subscriptionFingerprint(blob, at);
}

static const uint8_t PHONE_A[SUB_ADDR_LENGTH] = {0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xC6};
static const uint8_t PHONE_B[SUB_ADDR_LENGTH] = {0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xC6};
static const uint8_t PHONE_C[SUB_ADDR_LENGTH] = {0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6};
static const uint8_t NO_IDENTITY[SUB_ADDR_LENGTH] = {0, 0, 0, 0, 0, 0};

static uint8_t lastWhy = 0;
static SubVerdict write(SubscriptionBudget& budget, const uint8_t* addr, const Subs& subs, uint32_t now) {
  return budget.change(addr, printOf(subs), now, &lastWhy);
}

static void phoneAddr(uint8_t out[SUB_ADDR_LENGTH], uint8_t n) {
  const uint8_t addr[SUB_ADDR_LENGTH] = {0x10, 0x20, 0x30, 0x40, n, 0xC0};
  memcpy(out, addr, SUB_ADDR_LENGTH);
}

static void testFingerprint() {
  const Subs off = {0, 0, 0, 0};
  const Subs on = {2, 1, 1, 1};
  expect(printOf(off) == printOf(off), "the same state gives the same fingerprint");
  expect(printOf(off) != printOf(on), "another state gives another fingerprint");
  const Subs scOnly = {2, 0, 0, 0};
  const Subs buttonOnly = {0, 1, 0, 0};
  expect(printOf(scOnly) != printOf(buttonOnly), "which CCCD is on matters, not only how many");
  expect(subscriptionFingerprint(NULL, 4) == 0 && subscriptionFingerprint((const uint8_t*)"x", 0) == 0,
         "no blob: 0, unknown");
}

// The companion app (PinBLEClient.swift handleCharacteristics and
// handleBatteryCharacteristics) subscribes to button_event, power_state and
// the battery level on every link; iOS writes the Service Changed CCCD on
// every encrypted link. On the link that pairs, Bluefruit resets every
// CCCD at encryption and iOS writes them again.
static void testAppFirstLinkAndReconnects() {
  SubscriptionBudget budget = {};
  uint32_t now = 1000;
  // The open link before the pairing: Bluefruit saves nothing, the pin
  // sees nothing. After the pairing every CCCD reads 0 again.
  Subs link = {0, 0, 0, 0};
  int spent = 0, closed = 0;
  link.serviceChanged = 2;
  spent += write(budget, PHONE_A, link, now) == SUB_SPENT;
  link.button = 1;
  spent += write(budget, PHONE_A, link, now + 100) == SUB_SPENT;
  link.power = 1;
  spent += write(budget, PHONE_A, link, now + 200) == SUB_SPENT;
  link.battery = 1;
  spent += write(budget, PHONE_A, link, now + 300) == SUB_SPENT;
  expect(spent == 4, "the pairing link: 4 changes, each paid, none closes");
  expect(budget.blockedAtEncryption(PHONE_A, now + 400) == 0, "the next link of that phone is let on");

  // A reconnection that restores the CCCDs (D-5 repair): iOS writes the same
  // values again, in any order. Free, as often as it happens.
  int freeCount = 0;
  for (int i = 0; i < 50; i++) {
    now += 30 * 1000;
    const Subs restored = {2, 1, 1, 1};
    freeCount += write(budget, PHONE_A, restored, now) == SUB_FREE;
    freeCount += write(budget, PHONE_A, restored, now + 50) == SUB_FREE;
    freeCount += write(budget, PHONE_A, restored, now + 100) == SUB_FREE;
    freeCount += write(budget, PHONE_A, restored, now + 600) == SUB_FREE;
  }
  expect(freeCount == 200, "50 reconnections that restore the CCCDs cost nothing");

  // Reconnections whose restore misses (every CCCD reads 0 at encryption,
  // the bench of 2026-10-09): each of the four writes changes what the file
  // holds (Bluefruit saved the state before it), so each is paid. The 25
  // minutes of reconnections above refilled the bucket; two such links in a
  // row, then a third 8 minutes later, never close.
  for (int i = 0; i < 3; i++) {
    now += i == 2 ? 8 * 60 * 1000 : 1000;
    Subs zero = {0, 0, 0, 0};
    zero.serviceChanged = 2;
    closed += write(budget, PHONE_A, zero, now) == SUB_CLOSE;
    zero.button = 1;
    closed += write(budget, PHONE_A, zero, now + 10) == SUB_CLOSE;
    zero.power = 1;
    closed += write(budget, PHONE_A, zero, now + 20) == SUB_CLOSE;
    zero.battery = 1;
    closed += write(budget, PHONE_A, zero, now + 30) == SUB_CLOSE;
  }
  expect(closed == 0, "3 reconnections with no restore (12 changes in 8 min) never close");
  expect(budget.blockedAtEncryption(PHONE_A, now + 100) == 0, "still let on at encryption");
  expect(budget.find(PHONE_A)->bucket.tokens == SUB_PHONE_CAPACITY - 12 + 4, "4 tokens came back in the 8 minutes");

  // The worst case back to back: the pairing link and two links with no
  // restore within a minute, 12 changes, are still paid without a close.
  SubscriptionBudget fresh = {};
  uint32_t t = 1000;
  int cut = 0;
  for (int link = 0; link < 3; link++, t += 20 * 1000) {
    Subs s = {0, 0, 0, 0};
    s.serviceChanged = 2;
    cut += write(fresh, PHONE_B, s, t) == SUB_CLOSE;
    s.button = 1;
    cut += write(fresh, PHONE_B, s, t + 10) == SUB_CLOSE;
    s.power = 1;
    cut += write(fresh, PHONE_B, s, t + 20) == SUB_CLOSE;
    s.battery = 1;
    cut += write(fresh, PHONE_B, s, t + 30) == SUB_CLOSE;
  }
  expect(cut == 0 && fresh.find(PHONE_B)->bucket.tokens == SUB_PHONE_CAPACITY - 12,
         "pairing link plus two links with no restore in 1 minute: 12 changes, no close");
}

static void testManyChangesAreCut() {
  SubscriptionBudget budget = {};
  const uint32_t now = 5000;
  int spent = 0;
  SubVerdict verdict = SUB_FREE;
  int n = 0;
  for (; n < 100; n++) {
    const Subs subs = {0, (uint16_t)(n % 2), 0, 0};
    verdict = write(budget, PHONE_A, subs, now + (uint32_t)n);
    if (verdict != SUB_SPENT) break;
    spent++;
  }
  expect(verdict == SUB_CLOSE && lastWhy == SUB_CUT_PHONE, "a toggling phone is closed by its own bucket");
  expect(spent + 1 == SUB_PHONE_CAPACITY, "after exactly its capacity of changes (the last one closes)");
  expect(budget.shared.tokens == SUB_SHARED_CAPACITY - SUB_PHONE_CAPACITY, "each change took a shared token too");
  // A write already in flight when the link closes: saved anyway, counted,
  // nothing more taken.
  const Subs other = {2, 1, 1, 1};
  expect(write(budget, PHONE_A, other, now + 200) == SUB_CLOSE && lastWhy == SUB_CUT_PHONE,
         "a write in flight on the closing link closes again");
  expect(budget.shared.tokens == SUB_SHARED_CAPACITY - SUB_PHONE_CAPACITY, "and takes no shared token");
  expect(write(budget, PHONE_A, other, now + 300) == SUB_FREE, "the in-flight state is remembered as saved");
}

static void testClosedAtEncryptionUntilRefill() {
  SubscriptionBudget budget = {};
  const uint32_t start = 10000;
  for (int n = 0; n < (int)SUB_PHONE_CAPACITY; n++) {
    const Subs subs = {0, (uint16_t)(n % 2), 0, 0};
    write(budget, PHONE_A, subs, start);
  }
  expect(budget.blockedAtEncryption(PHONE_A, start + 1) == SUB_CUT_PHONE, "a closed phone is shut out at encryption");
  expect(budget.blockedAtEncryption(PHONE_B, start + 1) == 0, "another phone is not");
  expect(budget.blockedAtEncryption(NULL, start + 1) == 0, "nor a pairing that has not named its phone");
  // One token back is not enough: an app that keeps reconnecting would
  // spend each one at once and never gather enough for its admission.
  expect(budget.blockedAtEncryption(PHONE_A, start + SUB_PHONE_REFILL_MS) == SUB_CUT_PHONE,
         "one token back: still shut");
  const uint32_t halfFull = start + (SUB_PHONE_CAPACITY / 2) * SUB_PHONE_REFILL_MS;
  expect(budget.blockedAtEncryption(PHONE_A, halfFull - 1) == SUB_CUT_PHONE, "shut in the last millisecond");
  expect(budget.blockedAtEncryption(PHONE_A, halfFull) == 0, "let on again once half full (16 min)");
  // The pairing link's 4 changes fit again, so the app reaches its admission.
  Subs link = {2, 0, 0, 0};
  int spent = write(budget, PHONE_A, link, halfFull + 1) == SUB_SPENT;
  link.button = 1;
  spent += write(budget, PHONE_A, link, halfFull + 2) == SUB_SPENT;
  link.power = 1;
  spent += write(budget, PHONE_A, link, halfFull + 3) == SUB_SPENT;
  link.battery = 1;
  spent += write(budget, PHONE_A, link, halfFull + 4) == SUB_SPENT;
  expect(spent == 4, "after the wait a whole link of changes is paid without a close");
}

static void testSharedBucketCapsPhonesTogether() {
  SubscriptionBudget budget = {};
  const uint32_t now = 20000;
  int n = 0;
  // Two phones each stay under their own capacity: 15 + 15 = 30 shared.
  for (int i = 0; i < 15; i++) {
    const Subs subs = {0, (uint16_t)(i % 2), 0, (uint16_t)((i / 2) % 2)};
    n += write(budget, PHONE_A, subs, now) == SUB_SPENT;
    n += write(budget, PHONE_B, subs, now) == SUB_SPENT;
  }
  expect(n == 30, "two phones, 15 changes each, all paid");
  const Subs c1 = {2, 0, 0, 0};
  const Subs c2 = {2, 1, 0, 0};
  expect(write(budget, PHONE_C, c1, now) == SUB_SPENT, "a third phone's first change is paid");
  expect(write(budget, PHONE_C, c2, now) == SUB_CLOSE && lastWhy == SUB_CUT_SHARED,
         "its second takes the last shared token: closed, by the shared bucket");
  expect(budget.blockedAtEncryption(PHONE_A, now + 1) == SUB_CUT_SHARED, "every phone not admitted is shut out");
  expect(budget.blockedAtEncryption(NULL, now + 1) == SUB_CUT_SHARED, "a new pairing too");
  uint8_t fresh[SUB_ADDR_LENGTH];
  phoneAddr(fresh, 77);
  expect(budget.blockedAtEncryption(fresh, now + 1) == SUB_CUT_SHARED, "and a phone the table never saw");
  const uint32_t halfFull = now + (SUB_SHARED_CAPACITY / 2) * SUB_SHARED_REFILL_MS;
  expect(budget.blockedAtEncryption(fresh, halfFull - 1) == SUB_CUT_SHARED, "shut until half full");
  expect(budget.blockedAtEncryption(fresh, halfFull) == 0, "open again after 8 min");
}

static void testAdmissionClears() {
  SubscriptionBudget budget = {};
  const uint32_t now = 30000;
  for (int n = 0; n < (int)SUB_PHONE_CAPACITY; n++) {
    const Subs subs = {0, (uint16_t)(n % 2), 0, 0};
    write(budget, PHONE_A, subs, now);
  }
  expect(budget.blockedAtEncryption(PHONE_A, now + 1) == SUB_CUT_PHONE, "closed before the admission");
  budget.forget(PHONE_A);
  expect(budget.find(PHONE_A) == NULL, "an admission drops the phone's entry");
  expect(budget.blockedAtEncryption(PHONE_A, now + 2) == 0, "and its own bucket no longer shuts it out");
  budget.forget(PHONE_B);
  expect(budget.find(PHONE_B) == NULL, "forgetting a phone the table does not hold changes nothing");
  // An admitted phone never reaches change() (the sketch checks the
  // admitted table first); after a new pairing revokes it, it starts over
  // with an unknown state: its first change is paid.
  const Subs restored = {2, 1, 1, 1};
  expect(write(budget, PHONE_A, restored, now + 3) == SUB_SPENT, "after a revoke the first state is paid again");
}

static void testTableFull() {
  SubscriptionBudget budget = {};
  uint32_t now = 40000;
  uint8_t addrs[SUB_BUDGET_PHONES + 2][SUB_ADDR_LENGTH];
  for (size_t i = 0; i < SUB_BUDGET_PHONES + 2; i++) phoneAddr(addrs[i], (uint8_t)i);
  const Subs on = {2, 1, 0, 0};
  for (size_t i = 0; i < SUB_BUDGET_PHONES; i++) write(budget, addrs[i], on, now + (uint32_t)i);
  expect(budget.find(addrs[0]) != NULL && budget.find(addrs[SUB_BUDGET_PHONES - 1]) != NULL, "8 phones fill the table");
  // Phone 0 is closed by its own bucket; phone 1 is the least recently used
  // of the rest.
  for (int n = 0; n < (int)SUB_PHONE_CAPACITY; n++) {
    const Subs subs = {0, (uint16_t)(n % 2), 1, 0};
    write(budget, addrs[0], subs, now + 100);
  }
  expect(budget.blockedAtEncryption(addrs[0], now + 101) == SUB_CUT_PHONE, "phone 0 is shut");
  write(budget, addrs[SUB_BUDGET_PHONES], on, now + 200);
  expect(budget.find(addrs[SUB_BUDGET_PHONES]) != NULL, "a ninth phone gets an entry");
  expect(budget.find(addrs[1]) == NULL, "the least recently used open phone made room");
  expect(budget.find(addrs[0]) != NULL, "the shut phone kept its entry, so it stays shut");
  expect(budget.find(addrs[2]) != NULL, "the other phones stay");
  expect(write(budget, addrs[1], on, now + 300) == SUB_SPENT, "the evicted phone's next state is paid again");

  // With every entry shut, the least recently used of all goes.
  SubscriptionBudget shutAll = {};
  now = 50000;
  for (size_t i = 0; i < SUB_BUDGET_PHONES; i++) {
    shutAll.change(addrs[i], 1000u + (uint32_t)i, now + (uint32_t)i, NULL);
    SubPhone* phone = shutAll.find(addrs[i]);
    phone->bucket.tokens = 0;
    phone->bucket.shut = true;
  }
  shutAll.change(addrs[SUB_BUDGET_PHONES + 1], 7, now + 100, NULL);
  expect(shutAll.find(addrs[SUB_BUDGET_PHONES + 1]) != NULL && shutAll.find(addrs[0]) == NULL,
         "all shut: the least recently used of all made room");
}

static void testTimeWrapAndJump() {
  // millis() wrapping at 2^32: a refill across it counts the real time.
  SubscriptionBudget budget = {};
  const uint32_t nearWrap = 0xFFFFFFFFu - 10u * 1000u;
  for (int n = 0; n < (int)SUB_PHONE_CAPACITY; n++) {
    const Subs subs = {0, (uint16_t)(n % 2), 0, 0};
    write(budget, PHONE_A, subs, nearWrap);
  }
  expect(budget.blockedAtEncryption(PHONE_A, nearWrap + 1) == SUB_CUT_PHONE, "shut just before the wrap");
  const uint32_t halfFull = nearWrap + (SUB_PHONE_CAPACITY / 2) * SUB_PHONE_REFILL_MS;  // wraps past 0
  expect(halfFull < nearWrap, "the half-full moment is past the wrap");
  expect(budget.blockedAtEncryption(PHONE_A, halfFull - 1) == SUB_CUT_PHONE, "no early refill across the wrap");
  expect(budget.blockedAtEncryption(PHONE_A, halfFull) == 0, "refilled on time across the wrap");

  // This core's millis() jumps from about 4 194 304 000 back to 0 after
  // 48.55 days: an empty bucket must not wait for the clock to come back.
  SubscriptionBudget jump = {};
  const uint32_t beforeJump = 4194300000u;
  for (int n = 0; n < (int)SUB_PHONE_CAPACITY; n++) {
    const Subs subs = {0, (uint16_t)(n % 2), 0, 0};
    jump.change(PHONE_B, printOf(subs), beforeJump, NULL);
  }
  for (int n = 0; n < 40; n++) jump.change(PHONE_C, 100u + (uint32_t)n, beforeJump, NULL);
  expect(jump.blockedAtEncryption(PHONE_B, beforeJump + 1) == (SUB_CUT_PHONE | SUB_CUT_SHARED),
         "both buckets shut before the jump");
  expect(jump.blockedAtEncryption(PHONE_B, 5) == 0, "after the jump back to 0 both are full again");
  expect(jump.shared.tokens == SUB_SHARED_CAPACITY && !jump.shared.shut, "the shared bucket is full and open");
  // And the clock goes on from there normally.
  const Subs a = {2, 0, 0, 0};
  const Subs b = {2, 1, 0, 0};
  expect(jump.change(PHONE_B, printOf(a), 10, NULL) == SUB_SPENT && jump.change(PHONE_B, printOf(b), 20, NULL) == SUB_SPENT,
         "changes are paid normally after the jump");
}

static void testWhatCountsAsAChange() {
  SubscriptionBudget budget = {};
  const uint32_t now = 60000;
  const Subs saved = {2, 1, 1, 1};
  expect(write(budget, PHONE_A, saved, now) == SUB_SPENT, "the first state of a phone is paid (its file is unknown)");
  expect(write(budget, PHONE_A, saved, now + 1) == SUB_FREE, "the same state again is free");
  // Writes before encryption are not seen (Bluefruit does not save them by
  // themselves) but ride along with the next save: the first write on the
  // encrypted link carries them, even when it writes an unchanged value.
  const Subs afterOpenWrites = {2, 1, 0, 1};
  expect(write(budget, PHONE_A, afterOpenWrites, now + 2) == SUB_SPENT,
         "a change made on the open link is paid at the first encrypted write");
  // Service Changed is a CCCD of the SoftDevice's own service: its write
  // queues no save, but its value is in the next one. Toggling it between
  // unchanged button writes is paid each time.
  const Subs scOff = {0, 1, 0, 1};
  expect(write(budget, PHONE_A, scOff, now + 3) == SUB_SPENT, "a Service Changed toggle is paid");
  expect(write(budget, PHONE_A, scOff, now + 4) == SUB_FREE, "the unchanged button write after it is free");
  // An unreadable state (the blob did not fit) is always paid.
  expect(budget.change(PHONE_A, 0, now + 5, NULL) == SUB_SPENT, "an unknown state is paid");
  expect(budget.change(PHONE_A, 0, now + 6, NULL) == SUB_SPENT, "and paid again: unknown is never the same");
  expect(write(budget, PHONE_A, scOff, now + 7) == SUB_SPENT, "after an unknown state the next known one is paid");
  // A link encrypted by a pairing names its phone only at AUTH_STATUS: its
  // writes before that land on the no-identity entry, a separate bucket.
  const Subs pairing = {2, 0, 0, 0};
  expect(write(budget, NO_IDENTITY, pairing, now + 8) == SUB_SPENT, "a write before the pairing names its phone is paid");
  expect(budget.find(NO_IDENTITY) != NULL && budget.find(PHONE_B) == NULL, "on the no-identity entry, not on a phone");
  expect(write(budget, PHONE_B, pairing, now + 9) == SUB_SPENT,
         "the same state under the phone's identity is paid again (another file)");
}

static void testBenchFormat() {
  char buf[64];
  int n = formatSubBudgetBenchJson(buf, sizeof(buf), 17, 2);
  expect(n > 0 && strcmp(buf, "\"sub\":{\"chg\":17,\"cut\":2}") == 0, "bench object");
  n = formatSubBudgetBenchJson(buf, sizeof(buf), 0xFFFFFFFFu, 0xFFFFFFFFu);
  expect(n == 41, "bench object at its widest: 41 bytes, 42 with its comma (firmware.md)");
}

int main() {
  testFingerprint();
  testAppFirstLinkAndReconnects();
  testManyChangesAreCut();
  testClosedAtEncryptionUntilRefill();
  testSharedBucketCapsPhonesTogether();
  testAdmissionClears();
  testTableFull();
  testTimeWrapAndJump();
  testWhatCountsAsAChange();
  testBenchFormat();
  if (failures) {
    printf("%d subscription budget test(s) FAILED\n", failures);
    return 1;
  }
  printf("subscription budget tests passed\n");
  return 0;
}

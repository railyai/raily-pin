#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// The subscription budget (docs/pins/ble-bonding.md D-5, since 0.4.5).
// Bluefruit saves a phone's subscriptions (the SoftDevice's system
// attributes) into its bond file after every CCCD write on an encrypted
// link (BLEGatt.cpp saveCccd, utility/bonding.cpp bond_save_cccd_dfr), for
// any paired phone, admitted or not. So a phone that paired but was never
// admitted could make the pin commit to its 28 KB volume, which also holds
// the counter, the secret and the admitted table, as often as it likes. The
// sketch cannot cancel that save, so it limits the phone instead: a change
// of the subscription state costs a token from that phone's bucket and one
// from a bucket that all phones that are not admitted share. The change
// that empties a bucket closes the link, and a bucket that ran dry keeps
// its phone (the shared one: every phone that is not admitted) off
// encrypted links until it is half full again. Admitted phones are never
// counted. RAM only: a reboot starts full. Pure, host-tested in
// hardware/firmware/tests/test_subscription_budget.cpp.
//
// A change is judged on the link's whole state as the SoftDevice holds it
// (subscriptionFingerprint() of sd_ble_gatts_sys_attr_get(), the very blob
// Bluefruit saves), read at every CCCD write on an encrypted link, the
// Service Changed CCCD included. So writes made before encryption, which
// Bluefruit does not save by themselves but which ride along with the next
// save, are paid for at that save, and a phone that writes the same values
// on every link (iOS) costs nothing once its state is known.

static const size_t SUB_BUDGET_PHONES = 8;
static const size_t SUB_ADDR_LENGTH = 6;  // the bond's identity address, as Bluefruit names its file
// The companion app makes at most 4 changes on a link before its admission:
// the Service Changed CCCD (iOS), button_event, power_state and the battery
// level, all on the very link that pairs (iOS writes them again after the
// pairing). 16 is four such links per phone; it refills in 32 min.
static const uint16_t SUB_PHONE_CAPACITY = 16;
static const uint32_t SUB_PHONE_REFILL_MS = 2UL * 60UL * 1000UL;
// Eight such links for all phones that are not admitted together. One token
// every 30 s: at most 32 + 2880 changes a day, a fifteenth of the press
// counter's accepted one write per 2 s.
static const uint16_t SUB_SHARED_CAPACITY = 32;
static const uint32_t SUB_SHARED_REFILL_MS = 30UL * 1000UL;

// Why a link was closed (bits).
static const uint8_t SUB_CUT_PHONE = 0x01;
static const uint8_t SUB_CUT_SHARED = 0x02;

enum SubVerdict : uint8_t {
  SUB_FREE = 0,   // the state is the one already saved: nothing to pay
  SUB_SPENT = 1,  // a change, paid for; the link stays
  SUB_CLOSE = 2,  // a change that emptied a bucket or found one shut: close the link
};

// FNV-1a over the system-attributes blob. 0 means "unknown" (a blob the
// sketch could not read), so a real hash of 0 becomes 1.
static inline uint32_t subscriptionFingerprint(const uint8_t* blob, size_t length) {
  if (blob == NULL || length == 0) return 0;
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < length; i++) {
    hash ^= blob[i];
    hash *= 16777619u;
  }
  return hash == 0 ? 1u : hash;
}

struct SubBucket {
  uint16_t tokens;
  bool shut;        // ran dry: closed until it is half full again
  uint32_t lastMs;  // when `tokens` was last brought up to date

  void fill(uint16_t capacity, uint32_t now) {
    tokens = capacity;
    shut = false;
    lastMs = now;
  }

  // One token back per period. The time since lastMs is an unsigned
  // difference, so millis() wrapping, or this core's jump back to 0 after
  // 48.55 days (docs/pins/firmware.md «Sharp edges»), reads as a long wait
  // and fills the bucket: it can never stay empty past either.
  void refill(uint32_t now, uint16_t capacity, uint32_t periodMs) {
    if (tokens >= capacity) {
      tokens = capacity;
      lastMs = now;
    } else {
      const uint32_t elapsed = now - lastMs;
      const uint32_t missing = (uint32_t)(capacity - tokens);
      const uint32_t periods = elapsed / periodMs;
      if (periods >= missing) {
        tokens = capacity;
        lastMs = now;
      } else {
        tokens = (uint16_t)(tokens + periods);
        lastMs += periods * periodMs;  // the part of a period already waited counts
      }
    }
    if (shut && tokens >= capacity / 2) shut = false;
  }

  bool open() const { return !shut && tokens > 0; }
};

struct SubPhone {
  bool used;
  bool known;  // `print` is the state the pin last let Bluefruit save
  uint8_t addr[SUB_ADDR_LENGTH];
  uint32_t print;
  uint32_t lastUse;  // the least recently used goes first when the table is full
  SubBucket bucket;
};

// Owned by the sketch's BLE task, under passMutex with the admitted table.
// A zero-initialised budget is a full one: it fills on first use.
struct SubscriptionBudget {
  SubPhone phones[SUB_BUDGET_PHONES];
  SubBucket shared;
  uint32_t useSeq;
  bool started;

  // At encryption, and again once a pairing names its phone: 0 lets the
  // link on, else the SUB_CUT_* bits of the shut buckets. `addr` NULL: a
  // pairing that has not named its phone yet, only the shared bucket counts.
  uint8_t blockedAtEncryption(const uint8_t* addr, uint32_t now) {
    start(now);
    shared.refill(now, SUB_SHARED_CAPACITY, SUB_SHARED_REFILL_MS);
    uint8_t why = shared.open() ? 0 : SUB_CUT_SHARED;
    SubPhone* phone = addr != NULL ? find(addr) : NULL;
    if (phone != NULL) {
      phone->lastUse = ++useSeq;
      phone->bucket.refill(now, SUB_PHONE_CAPACITY, SUB_PHONE_REFILL_MS);
      if (!phone->bucket.open()) why |= SUB_CUT_PHONE;
    }
    return why;
  }

  // A CCCD write on an encrypted link of a phone that is not admitted.
  // `print` is the link's state after the write (0: unreadable, always a
  // change). The save Bluefruit queued happens whatever this returns, so the
  // state is remembered as saved in every case.
  SubVerdict change(const uint8_t addr[SUB_ADDR_LENGTH], uint32_t print, uint32_t now, uint8_t* why) {
    if (why != NULL) *why = 0;
    start(now);
    shared.refill(now, SUB_SHARED_CAPACITY, SUB_SHARED_REFILL_MS);
    SubPhone* phone = find(addr);
    if (phone != NULL && phone->known && print != 0 && phone->print == print) {
      phone->lastUse = ++useSeq;
      return SUB_FREE;
    }
    if (phone == NULL) phone = claim(addr, now);
    phone->lastUse = ++useSeq;
    phone->bucket.refill(now, SUB_PHONE_CAPACITY, SUB_PHONE_REFILL_MS);
    phone->print = print;
    phone->known = print != 0;
    uint8_t cut = 0;
    if (!phone->bucket.open() || !shared.open()) {
      // A write already in flight on a link the pin is closing, or one past
      // a bucket that shut meanwhile: saved anyway, nothing left to take.
      if (!phone->bucket.open()) cut |= SUB_CUT_PHONE;
      if (!shared.open()) cut |= SUB_CUT_SHARED;
    } else {
      phone->bucket.tokens--;
      shared.tokens--;
      // The change that takes the last token closes the link, so no later
      // change gets saved on it.
      if (phone->bucket.tokens == 0) {
        phone->bucket.shut = true;
        cut |= SUB_CUT_PHONE;
      }
      if (shared.tokens == 0) {
        shared.shut = true;
        cut |= SUB_CUT_SHARED;
      }
    }
    if (why != NULL) *why = cut;
    return cut != 0 ? SUB_CLOSE : SUB_SPENT;
  }

  // An admission: the phone is never counted again, and its entry goes.
  void forget(const uint8_t addr[SUB_ADDR_LENGTH]) {
    SubPhone* phone = find(addr);
    if (phone != NULL) memset(phone, 0, sizeof(*phone));
  }

  SubPhone* find(const uint8_t addr[SUB_ADDR_LENGTH]) {
    for (size_t i = 0; i < SUB_BUDGET_PHONES; i++) {
      if (phones[i].used && memcmp(phones[i].addr, addr, SUB_ADDR_LENGTH) == 0) return &phones[i];
    }
    return NULL;
  }

 private:
  void start(uint32_t now) {
    if (started) return;
    memset(phones, 0, sizeof(phones));
    shared.fill(SUB_SHARED_CAPACITY, now);
    useSeq = 0;
    started = true;
  }

  // A free entry, else the least recently used one whose bucket is not
  // shut (a phone the pin closed stays closed while others come and go),
  // else the least recently used of all. A phone that loses its entry only
  // gets a full bucket of its own: every change still takes a token from
  // the shared bucket, which bounds the total.
  SubPhone* claim(const uint8_t addr[SUB_ADDR_LENGTH], uint32_t now) {
    SubPhone* pick = NULL;
    for (size_t i = 0; i < SUB_BUDGET_PHONES && pick == NULL; i++) {
      if (!phones[i].used) pick = &phones[i];
    }
    for (int pass = 0; pass < 2 && pick == NULL; pass++) {
      for (size_t i = 0; i < SUB_BUDGET_PHONES; i++) {
        if (pass == 0 && phones[i].bucket.shut) continue;
        if (pick == NULL || phones[i].lastUse < pick->lastUse) pick = &phones[i];
      }
    }
    memset(pick, 0, sizeof(*pick));
    pick->used = true;
    memcpy(pick->addr, addr, SUB_ADDR_LENGTH);
    pick->bucket.fill(SUB_PHONE_CAPACITY, now);
    return pick;
  }
};

// Serial `i`'s "sub" object (docs/pins/firmware.md, serial protocol): the
// subscription changes of phones that are not admitted counted since boot,
// and the links the budget closed. No address is ever printed.
static inline int formatSubBudgetBenchJson(char* buf, size_t size, uint32_t changes, uint32_t cuts) {
  return snprintf(buf, size, "\"sub\":{\"chg\":%lu,\"cut\":%lu}", (unsigned long)changes, (unsigned long)cuts);
}

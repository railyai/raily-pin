#pragma once

#include <stdint.h>

// screen_state (7B1E0014) payload, version 1: docs/pins/keyring-oled.md §10.
// Pure: no Arduino, no radio. The GATT callback hands the raw write here;
// only a payload this returns as ok may reach the screen. Host-tested in
// hardware/firmware/tests/test_screen_payload.cpp; the app's encoder
// (PinScreenState.swift) shares its test vectors.

static const uint8_t SCREEN_PAYLOAD_VERSION = 1;
static const uint16_t SCREEN_PAYLOAD_LENGTH = 9;

// Byte 1 `agent`.
static const uint8_t SCREEN_AGENT_UNKNOWN = 0;
static const uint8_t SCREEN_AGENT_WATCH = 1;
static const uint8_t SCREEN_AGENT_SCANNING = 2;
static const uint8_t SCREEN_AGENT_PAUSED = 3;
static const uint8_t SCREEN_AGENT_SET_UP = 4;
static const uint8_t SCREEN_AGENT_STALE = 5;

// Bytes 2, 3 (`found`, `looking`) and 5 (`notify_count`).
static const uint8_t SCREEN_COUNT_MAX = 99;
static const uint8_t SCREEN_COUNT_WITHHELD = 0xFF;  // found/looking only

// Byte 4 `notify`: 0 none … 7 agent question.
static const uint8_t SCREEN_NOTIFY_MAX = 7;
// Byte 4 kinds (spec §10 «Notification mapping»); 0 none.
static const uint8_t SCREEN_NOTIFY_NEW_FINDING = 1;
static const uint8_t SCREEN_NOTIFY_REQUEST_RECEIVED = 2;
static const uint8_t SCREEN_NOTIFY_REQUEST_ACCEPTED = 3;
static const uint8_t SCREEN_NOTIFY_AGENT_REPORT = 4;
static const uint8_t SCREEN_NOTIFY_NEGOTIATION_REPORT = 5;
static const uint8_t SCREEN_NOTIFY_DOOR_CLEAR = 6;
static const uint8_t SCREEN_NOTIFY_AGENT_QUESTION = 7;

// Byte 6 `flags`.
static const uint8_t SCREEN_FLAG_WAKE_NOW = 1u << 0;
static const uint8_t SCREEN_FLAG_QUIET_HOURS = 1u << 1;
static const uint8_t SCREEN_FLAG_MODEST = 1u << 2;
static const uint8_t SCREEN_FLAG_DISABLED = 1u << 3;
static const uint8_t SCREEN_FLAG_ALWAYS_ON_USB = 1u << 4;
static const uint8_t SCREEN_FLAG_AGENT_SPEAKING = 1u << 5;
static const uint8_t SCREEN_FLAGS_KNOWN = 0x3F;  // bits 6-7 reserved
// Bits only a trusted (encrypted) write may carry.
static const uint8_t SCREEN_FLAGS_TRUSTED_ONLY =
    SCREEN_FLAG_WAKE_NOW | SCREEN_FLAG_QUIET_HOURS | SCREEN_FLAG_ALWAYS_ON_USB;

// Byte 7 `mascot`: high nibble shape, low nibble material, in the order of
// frontend-visualization/src/lib/agent-mascot.ts.
static const uint8_t SCREEN_SHAPE_COUNT = 6;     // pebble cube lens drop loop fold
static const uint8_t SCREEN_MATERIAL_COUNT = 3;  // satin jelly glass

// Byte 8 `locale`: which pre-rendered word strips to draw.
static const uint8_t SCREEN_LOCALE_NONE = 0;  // icons only
static const uint8_t SCREEN_LOCALE_RU = 1;
static const uint8_t SCREEN_LOCALE_EN = 2;
static const uint8_t SCREEN_LOCALE_ES = 3;
static const uint8_t SCREEN_LOCALE_PT = 4;  // pt-BR
static const uint8_t SCREEN_LOCALE_AR = 5;  // icons only: the pin never draws Arabic
static const uint8_t SCREEN_LOCALE_MAX = 5;

struct ScreenPayload {
  uint8_t version;
  uint8_t agent;
  uint8_t found;
  uint8_t looking;
  uint8_t notify;
  uint8_t notifyCount;
  uint8_t flags;
  uint8_t mascot;
  uint8_t locale;

  uint8_t shape() const { return mascot >> 4; }
  uint8_t material() const { return mascot & 0x0F; }
  bool hasFlag(uint8_t flag) const { return (flags & flag) != 0; }
  // Word strips exist for ru, en, es and pt; none and ar draw icons only.
  bool drawsWords() const { return locale >= SCREEN_LOCALE_RU && locale <= SCREEN_LOCALE_PT; }
};

enum class ScreenPayloadError : uint8_t {
  ok = 0,
  noBuffer,
  badLength,
  badVersion,
  badAgent,
  badFound,
  badLooking,
  badNotify,
  badNotifyCount,
  notifyCountMismatch,
  reservedFlag,
  badShape,
  badMaterial,
  badLocale,
};

static inline bool screenCountValid(uint8_t value) {
  return value <= SCREEN_COUNT_MAX || value == SCREEN_COUNT_WITHHELD;
}

// Validates the whole write first; any bad byte rejects it, nothing is
// clamped and `out` is not touched. Then, on a link that is not encrypted,
// clears bytes 2-5 and the trusted-only flag bits before handing it out, so
// before bonding no write carries counts, notification kinds, wake, quiet
// hours or always-on (agent, mascot, locale, modest, disabled and speaking
// pass).
static inline ScreenPayloadError parseScreenPayload(const uint8_t* data, uint16_t len,
                                                    bool encrypted, ScreenPayload* out) {
  if (data == nullptr || out == nullptr) return ScreenPayloadError::noBuffer;
  if (len != SCREEN_PAYLOAD_LENGTH) return ScreenPayloadError::badLength;
  if (data[0] != SCREEN_PAYLOAD_VERSION) return ScreenPayloadError::badVersion;
  if (data[1] > SCREEN_AGENT_STALE) return ScreenPayloadError::badAgent;
  if (!screenCountValid(data[2])) return ScreenPayloadError::badFound;
  if (!screenCountValid(data[3])) return ScreenPayloadError::badLooking;
  if (data[4] > SCREEN_NOTIFY_MAX) return ScreenPayloadError::badNotify;
  if (data[5] > SCREEN_COUNT_MAX) return ScreenPayloadError::badNotifyCount;
  if ((data[4] == 0) != (data[5] == 0)) return ScreenPayloadError::notifyCountMismatch;
  if ((data[6] & ~SCREEN_FLAGS_KNOWN) != 0) return ScreenPayloadError::reservedFlag;
  if ((data[7] >> 4) >= SCREEN_SHAPE_COUNT) return ScreenPayloadError::badShape;
  if ((data[7] & 0x0F) >= SCREEN_MATERIAL_COUNT) return ScreenPayloadError::badMaterial;
  if (data[8] > SCREEN_LOCALE_MAX) return ScreenPayloadError::badLocale;

  ScreenPayload payload;
  payload.version = data[0];
  payload.agent = data[1];
  payload.found = data[2];
  payload.looking = data[3];
  payload.notify = data[4];
  payload.notifyCount = data[5];
  payload.flags = data[6];
  payload.mascot = data[7];
  payload.locale = data[8];
  if (!encrypted) {
    payload.found = 0;
    payload.looking = 0;
    payload.notify = 0;
    payload.notifyCount = 0;
    payload.flags &= (uint8_t)~SCREEN_FLAGS_TRUSTED_ONLY;
  }
  *out = payload;
  return ScreenPayloadError::ok;
}

// The write-authorize verdict of the characteristic (RailyPinsP1.ino,
// onScreenStateAuthorize): 0 accepts and fills `out`; anything else is the
// ATT error the pin answers (SoftDevice status 0x0180 + n goes on air as
// 0x80 + n; the server pass owns 0x80-0x83, docs/pins/firmware.md):
//   0x84 not one 9-byte write request at offset 0 (a prepared or long
//        write, or a wrong length);
//   0x85 a byte out of range (version, enum, count, flag; spec §10);
//   0x86 the pin is not owned: an unbound or released pin refuses every
//        write until a new bind (spec §10 «Only an owned pin listens»).
// The format is checked before ownership, and ownership before the bytes,
// so an unowned pin never tells a stranger which of its bytes were wrong.
static const uint8_t SCREEN_ATT_BAD_FORMAT = 0x84;
static const uint8_t SCREEN_ATT_BAD_VALUE = 0x85;
static const uint8_t SCREEN_ATT_NOT_OWNED = 0x86;

static inline uint8_t screenWriteVerdict(bool writeRequest, uint16_t offset, const uint8_t* data, uint16_t len,
                                         bool owned, bool encrypted, ScreenPayload* out) {
  if (!writeRequest || offset != 0 || data == nullptr || len != SCREEN_PAYLOAD_LENGTH) {
    return SCREEN_ATT_BAD_FORMAT;
  }
  if (!owned) return SCREEN_ATT_NOT_OWNED;
  return parseScreenPayload(data, len, encrypted, out) == ScreenPayloadError::ok ? 0 : SCREEN_ATT_BAD_VALUE;
}

// Flag bit 5 `agent_speaking` (spec §10 «Agent speaking»): the phone's voice
// agent is talking. It may arrive over a plaintext link, so a stranger can
// set it; its wakes are bounded on every link by three limits:
//   - only a rising edge (not speaking → speaking) wakes, at most once per
//     SPEAKING_WAKE_INTERVAL_MS counted from the previous speaking wake;
//   - a speaking wake holds the screen at most SPEAKING_HOLD_MAX_MS;
//   - speaking wakes keep the screen awake at most SPEAKING_BUDGET_MS in
//     any rolling SPEAKING_BUDGET_WINDOW_MS. Each wake is charged its hold
//     plus SPEAKING_WAKE_TAIL_MS (the 6 s idle and 1 s fade that follow it,
//     §13); a wake is refused when the tail no longer fits, and its hold is
//     cut where the budget ends. A wake stays charged for the window plus
//     the longest a wake lasts (SPEAKING_WAKE_KEEP_MS), so a wake that began
//     just before an hour still counts against the awake time inside it.
// A refused speaking flag is still applied: it changes only what the next
// wake shows, like any unencrypted write. Unsigned subtraction keeps every
// limit right across the millis() wrap.
// Not thread-safe: call every method from loop() only. The GATT write
// callback runs in the BLE task, so it must hand the accepted payload to
// loop() (a mailbox, as event_ack does with pendingFeedback) and never call
// onAcceptedWrite itself.
static const uint32_t SPEAKING_WAKE_INTERVAL_MS = 60000;
static const uint32_t SPEAKING_HOLD_MAX_MS = 60000;
static const uint32_t SPEAKING_WAKE_TAIL_MS = 7000;
static const uint32_t SPEAKING_BUDGET_MS = 300000;
static const uint32_t SPEAKING_BUDGET_WINDOW_MS = 3600000;
static const uint32_t SPEAKING_WAKE_KEEP_MS =
    SPEAKING_BUDGET_WINDOW_MS + SPEAKING_HOLD_MAX_MS + SPEAKING_WAKE_TAIL_MS;
// Every charged wake costs at least the tail, so no more than
// SPEAKING_BUDGET_MS / SPEAKING_WAKE_TAIL_MS (42) fit; one spare slot.
static const uint8_t SPEAKING_WAKE_SLOTS = SPEAKING_BUDGET_MS / SPEAKING_WAKE_TAIL_MS + 1;
// settle() drops a hold together with its wake, which is safe only while a
// hold always ends long before its wake stops being charged.
static_assert(SPEAKING_HOLD_MAX_MS + SPEAKING_WAKE_TAIL_MS < SPEAKING_WAKE_KEEP_MS,
              "a running hold must end before its wake leaves the window");

struct SpeakingWake {
  struct Wake {
    uint32_t startMs;
    uint32_t holdMs;  // final once the hold ended, so far while it runs
  };

  Wake wakes[SPEAKING_WAKE_SLOTS];  // oldest first, all still charged
  uint8_t count;
  uint32_t holdLimitMs;  // the running hold's cap: 60 s or the budget left
  bool holding;
  bool speaking;

  // Feed every accepted payload's bit 5. True when this write wakes the screen.
  bool onAcceptedWrite(uint32_t now, bool speakingNow) {
    settle(now);
    const bool rising = speakingNow && !speaking;
    speaking = speakingNow;
    if (!speakingNow) endHold(now);
    if (!rising) return false;
    if (count != 0 && (uint32_t)(now - wakes[count - 1].startMs) < SPEAKING_WAKE_INTERVAL_MS) {
      return false;
    }
    const uint32_t used = usedMs();
    // `>=` on purpose: a wake needs some hold beyond its tail, so a budget
    // that fits only the tail refuses it.
    if (used + SPEAKING_WAKE_TAIL_MS >= SPEAKING_BUDGET_MS || count == SPEAKING_WAKE_SLOTS) {
      return false;
    }
    const uint32_t left = SPEAKING_BUDGET_MS - used - SPEAKING_WAKE_TAIL_MS;
    wakes[count].startMs = now;
    wakes[count].holdMs = 0;
    count++;
    holdLimitMs = left < SPEAKING_HOLD_MAX_MS ? left : SPEAKING_HOLD_MAX_MS;
    holding = true;
    return true;
  }

  // Whether speaking still holds the screen awake (past the 6 s idle timer).
  // Call it from loop(): it also ends a hold that reached its cap.
  bool holdsAwake(uint32_t now) {
    settle(now);
    return holding;
  }

  // Disconnect, stale reset, unbind: speaking ends; the limits stay.
  void stop(uint32_t now) {
    settle(now);
    speaking = false;
    endHold(now);
  }

  // Awake time charged to the speaking wakes still kept.
  uint32_t usedMs() const {
    uint32_t used = 0;
    for (uint8_t i = 0; i < count; i++) used += wakes[i].holdMs + SPEAKING_WAKE_TAIL_MS;
    return used;
  }

 private:
  // Drops wakes no longer charged and brings the running hold up to now.
  void settle(uint32_t now) {
    uint8_t drop = 0;
    while (drop < count && (uint32_t)(now - wakes[drop].startMs) >= SPEAKING_WAKE_KEEP_MS) drop++;
    if (drop == count) holding = false;  // the running hold is at most 60 s old
    for (uint8_t i = drop; i < count; i++) wakes[i - drop] = wakes[i];
    count -= drop;
    if (!holding || count == 0) return;
    const uint32_t elapsed = now - wakes[count - 1].startMs;
    if (elapsed >= holdLimitMs) {
      wakes[count - 1].holdMs = holdLimitMs;
      holding = false;
    } else {
      wakes[count - 1].holdMs = elapsed;
    }
  }

  void endHold(uint32_t now) {
    if (!holding || count == 0) return;
    const uint32_t elapsed = now - wakes[count - 1].startMs;
    wakes[count - 1].holdMs = elapsed < holdLimitMs ? elapsed : holdLimitMs;
    holding = false;
  }
};

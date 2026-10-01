// Host tests for screen_payload.h (docs/pins/keyring-oled.md §10).
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../RailyPinsP1/screen_payload.h"

static int failures = 0;

static void expect(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    failures++;
  }
}

static const ScreenPayload SENTINEL = {0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE};

static bool untouched(const ScreenPayload& p) {
  return std::memcmp(&p, &SENTINEL, sizeof p) == 0;
}

static bool sameBytes(const ScreenPayload& p, const uint8_t* bytes) {
  const uint8_t fields[SCREEN_PAYLOAD_LENGTH] = {p.version, p.agent, p.found, p.looking, p.notify,
                                                p.notifyCount, p.flags, p.mascot, p.locale};
  return std::memcmp(fields, bytes, SCREEN_PAYLOAD_LENGTH) == 0;
}

// Shared test vectors. They MUST stay byte-identical to the ones in
// apps/raily-pin-companion/Tests/RailyPinCompanionTests/PinScreenStateTests.swift:
// the app encodes `full` from the named fields and redacts it to `plain`;
// the pin accepts `full` as is on an encrypted link and strips it to `plain`
// on a plaintext one.
struct SharedVector {
  const char* name;
  uint8_t full[SCREEN_PAYLOAD_LENGTH];
  uint8_t plain[SCREEN_PAYLOAD_LENGTH];
};

static const SharedVector SHARED[] = {
    // Default: agent unknown, pebble/satin, icons only.
    {"default", {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
     {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    // Watch, found 3, looking withheld, cube/glass, en.
    {"watch", {0x01, 0x01, 0x03, 0xFF, 0x00, 0x00, 0x00, 0x12, 0x02},
     {0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x02}},
    // Scanning, 99/99, agent question ×99, wake + always-on, fold/jelly, ru.
    {"scanning", {0x01, 0x02, 0x63, 0x63, 0x07, 0x63, 0x11, 0x51, 0x01},
     {0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x51, 0x01}},
    // Paused, request received ×1, quiet + modest + disabled, lens/satin, pt.
    {"paused", {0x01, 0x03, 0x00, 0x00, 0x02, 0x01, 0x0E, 0x20, 0x04},
     {0x01, 0x03, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x20, 0x04}},
    // Set-up needed, agent speaking, drop/jelly, ar.
    {"setup", {0x01, 0x04, 0x00, 0x00, 0x00, 0x00, 0x20, 0x31, 0x05},
     {0x01, 0x04, 0x00, 0x00, 0x00, 0x00, 0x20, 0x31, 0x05}},
    // Stale, found 12, new finding ×12, every flag, loop/glass, es.
    {"stale", {0x01, 0x05, 0x0C, 0x00, 0x01, 0x0C, 0x3F, 0x42, 0x03},
     {0x01, 0x05, 0x00, 0x00, 0x00, 0x00, 0x2C, 0x42, 0x03}},
};

static const uint8_t VALID[SCREEN_PAYLOAD_LENGTH] = {0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// One byte of VALID changed; must be rejected with `error` on both links.
static void expectRejectedAt(uint8_t index, uint8_t value, ScreenPayloadError error, const char* what) {
  uint8_t bytes[SCREEN_PAYLOAD_LENGTH];
  std::memcpy(bytes, VALID, sizeof bytes);
  bytes[index] = value;
  for (int encrypted = 0; encrypted < 2; encrypted++) {
    ScreenPayload out = SENTINEL;
    expect(parseScreenPayload(bytes, SCREEN_PAYLOAD_LENGTH, encrypted != 0, &out) == error, what);
    expect(untouched(out), "a rejected write leaves out untouched");
  }
}

static void expectAcceptedAt(uint8_t index, uint8_t value, const char* what) {
  uint8_t bytes[SCREEN_PAYLOAD_LENGTH];
  std::memcpy(bytes, VALID, sizeof bytes);
  bytes[index] = value;
  ScreenPayload out = SENTINEL;
  expect(parseScreenPayload(bytes, SCREEN_PAYLOAD_LENGTH, true, &out) == ScreenPayloadError::ok, what);
  expect(sameBytes(out, bytes), what);
}

static void sharedVectors() {
  for (const SharedVector& v : SHARED) {
    ScreenPayload trusted = SENTINEL;
    expect(parseScreenPayload(v.full, SCREEN_PAYLOAD_LENGTH, true, &trusted) == ScreenPayloadError::ok,
           v.name);
    expect(sameBytes(trusted, v.full), "an encrypted link applies the payload as sent");
    ScreenPayload plain = SENTINEL;
    expect(parseScreenPayload(v.full, SCREEN_PAYLOAD_LENGTH, false, &plain) == ScreenPayloadError::ok,
           v.name);
    expect(sameBytes(plain, v.plain), "a plaintext link strips to the shared plain vector");
    // What the app sends before bonding is already stripped: stripping is idempotent.
    ScreenPayload again = SENTINEL;
    expect(parseScreenPayload(v.plain, SCREEN_PAYLOAD_LENGTH, false, &again) == ScreenPayloadError::ok,
           v.name);
    expect(sameBytes(again, v.plain), "the plain vector passes a plaintext link unchanged");
  }
}

static void accessors() {
  ScreenPayload p = SENTINEL;
  expect(parseScreenPayload(SHARED[2].full, SCREEN_PAYLOAD_LENGTH, true, &p) == ScreenPayloadError::ok,
         "scanning parses");
  expect(p.shape() == 5 && p.material() == 1, "0x51 is fold/jelly");
  expect(p.hasFlag(SCREEN_FLAG_WAKE_NOW) && p.hasFlag(SCREEN_FLAG_ALWAYS_ON_USB), "flags 0x11");
  expect(!p.hasFlag(SCREEN_FLAG_AGENT_SPEAKING), "no speaking");
  expect(p.locale == SCREEN_LOCALE_RU && p.drawsWords(), "ru draws words");
  const uint8_t locales[] = {SCREEN_LOCALE_NONE, SCREEN_LOCALE_RU, SCREEN_LOCALE_EN,
                             SCREEN_LOCALE_ES, SCREEN_LOCALE_PT, SCREEN_LOCALE_AR};
  const bool words[] = {false, true, true, true, true, false};
  for (int i = 0; i < 6; i++) {
    ScreenPayload q = {};
    q.locale = locales[i];
    expect(q.drawsWords() == words[i], "words only for ru, en, es, pt");
  }
  expect(SCREEN_FLAGS_TRUSTED_ONLY == 0x13, "trusted-only flags are bits 0, 1, 4");
}

static void acceptedBoundaries() {
  expectAcceptedAt(1, SCREEN_AGENT_STALE, "agent 5 is the last");
  expectAcceptedAt(2, 99, "found 99");
  expectAcceptedAt(2, 0xFF, "found withheld");
  expectAcceptedAt(3, 99, "looking 99");
  expectAcceptedAt(3, 0xFF, "looking withheld");
  expectAcceptedAt(6, 0x3F, "every known flag");
  expectAcceptedAt(7, 0x52, "fold/glass is the last look");
  expectAcceptedAt(8, SCREEN_LOCALE_AR, "locale 5 is the last");
  for (uint8_t shape = 0; shape < SCREEN_SHAPE_COUNT; shape++) {
    for (uint8_t material = 0; material < SCREEN_MATERIAL_COUNT; material++) {
      expectAcceptedAt(7, (uint8_t)((shape << 4) | material), "all 18 looks");
    }
  }
  uint8_t notify[SCREEN_PAYLOAD_LENGTH];
  std::memcpy(notify, VALID, sizeof notify);
  notify[4] = SCREEN_NOTIFY_MAX;
  notify[5] = 99;
  ScreenPayload out = SENTINEL;
  expect(parseScreenPayload(notify, SCREEN_PAYLOAD_LENGTH, true, &out) == ScreenPayloadError::ok,
         "notify 7 with 99 unseen");
}

static void rejections() {
  // Length and buffers.
  uint8_t longer[SCREEN_PAYLOAD_LENGTH + 1] = {0x01, 0x01};
  const uint16_t badLengths[] = {0, 1, 8, 10};
  for (uint16_t len : badLengths) {
    ScreenPayload out = SENTINEL;
    expect(parseScreenPayload(longer, len, true, &out) == ScreenPayloadError::badLength, "wrong length");
    expect(untouched(out), "wrong length leaves out untouched");
  }
  ScreenPayload out = SENTINEL;
  expect(parseScreenPayload(nullptr, SCREEN_PAYLOAD_LENGTH, true, &out) == ScreenPayloadError::noBuffer,
         "no data");
  expect(untouched(out), "no data leaves out untouched");
  expect(parseScreenPayload(VALID, SCREEN_PAYLOAD_LENGTH, true, nullptr) == ScreenPayloadError::noBuffer,
         "no out");

  expectRejectedAt(0, 0, ScreenPayloadError::badVersion, "version 0");
  expectRejectedAt(0, 2, ScreenPayloadError::badVersion, "version 2");
  expectRejectedAt(1, 6, ScreenPayloadError::badAgent, "agent 6");
  expectRejectedAt(1, 0xFF, ScreenPayloadError::badAgent, "agent 255");
  expectRejectedAt(2, 100, ScreenPayloadError::badFound, "found 100");
  expectRejectedAt(2, 254, ScreenPayloadError::badFound, "found 254");
  expectRejectedAt(3, 100, ScreenPayloadError::badLooking, "looking 100");
  expectRejectedAt(3, 254, ScreenPayloadError::badLooking, "looking 254");
  expectRejectedAt(4, 8, ScreenPayloadError::badNotify, "notify 8");
  expectRejectedAt(6, 0x40, ScreenPayloadError::reservedFlag, "flag bit 6");
  expectRejectedAt(6, 0x80, ScreenPayloadError::reservedFlag, "flag bit 7");
  expectRejectedAt(6, 0xFF, ScreenPayloadError::reservedFlag, "all flag bits");
  expectRejectedAt(7, 0x60, ScreenPayloadError::badShape, "shape 6");
  expectRejectedAt(7, 0xF0, ScreenPayloadError::badShape, "shape 15");
  expectRejectedAt(7, 0x03, ScreenPayloadError::badMaterial, "material 3");
  expectRejectedAt(7, 0x0F, ScreenPayloadError::badMaterial, "material 15");
  expectRejectedAt(8, 6, ScreenPayloadError::badLocale, "locale 6");
  expectRejectedAt(8, 0xFF, ScreenPayloadError::badLocale, "locale 255");

  // notify_count has no «withheld» value, and it agrees with notify about zero.
  uint8_t bytes[SCREEN_PAYLOAD_LENGTH];
  const struct {
    uint8_t notify, count;
    ScreenPayloadError error;
    const char* what;
  } notifyCases[] = {
      {1, 100, ScreenPayloadError::badNotifyCount, "notify_count 100"},
      {1, 0xFF, ScreenPayloadError::badNotifyCount, "notify_count 255"},
      {0, 1, ScreenPayloadError::notifyCountMismatch, "count without a kind"},
      {3, 0, ScreenPayloadError::notifyCountMismatch, "kind without a count"},
  };
  for (const auto& c : notifyCases) {
    std::memcpy(bytes, VALID, sizeof bytes);
    bytes[4] = c.notify;
    bytes[5] = c.count;
    for (int encrypted = 0; encrypted < 2; encrypted++) {
      ScreenPayload o = SENTINEL;
      expect(parseScreenPayload(bytes, SCREEN_PAYLOAD_LENGTH, encrypted != 0, &o) == c.error, c.what);
      expect(untouched(o), "a rejected notify leaves out untouched");
    }
  }

  // Validation runs before the strip: a plaintext link does not launder a
  // bad count into zero.
  std::memcpy(bytes, SHARED[1].full, sizeof bytes);
  bytes[2] = 100;
  out = SENTINEL;
  expect(parseScreenPayload(bytes, SCREEN_PAYLOAD_LENGTH, false, &out) == ScreenPayloadError::badFound,
         "plaintext still validates found before stripping it");
  expect(untouched(out), "and applies nothing");
}

// Toggles speaking off and on at `t`: a fresh rising edge.
static bool restart(SpeakingWake& s, uint32_t t) {
  s.onAcceptedWrite(t, false);
  return s.onAcceptedWrite(t, true);
}

static void speakingWake() {
  // Limit 1 and 2: rising edge, once a minute, 60 s hold.
  SpeakingWake s = {};
  expect(!s.holdsAwake(0), "silent pin holds nothing");
  expect(s.onAcceptedWrite(1000, true), "first speaking wakes");
  expect(s.holdsAwake(1000 + 59999), "held until the cap");
  expect(!s.holdsAwake(1000 + 60000), "released at 60 s even while speaking");
  expect(!s.onAcceptedWrite(1000 + 60001, true), "a repeat while speaking is not a new wake");

  SpeakingWake edge = {};
  expect(edge.onAcceptedWrite(1000, true), "wakes");
  expect(!edge.onAcceptedWrite(2000, true), "a repeat is not an edge");
  expect(!edge.onAcceptedWrite(3000, false), "stopping never wakes");
  expect(!edge.holdsAwake(3000), "stopped speaking holds nothing");
  expect(edge.usedMs() == 2000 + 7000, "a 2 s hold is charged 2 s plus the 7 s tail");
  expect(!edge.onAcceptedWrite(4000, true), "a second start within a minute does not wake");
  expect(!edge.holdsAwake(4000), "and holds nothing");
  expect(!edge.onAcceptedWrite(1000 + 59999, false), "stop");
  expect(!edge.onAcceptedWrite(1000 + 59999, true), "59.999 s after the last wake: still refused");
  expect(restart(edge, 1000 + 60000), "a start a minute after the last wake wakes again");

  SpeakingWake toggling = {};
  int wakes = 0;
  for (uint32_t t = 0; t < 10 * 60000u; t += 500) {
    if (toggling.onAcceptedWrite(t, (t / 500) % 2 == 0)) wakes++;
  }
  expect(wakes == 10, "a stranger toggling every 500 ms gets one wake a minute");

  SpeakingWake stopped = {};
  expect(stopped.onAcceptedWrite(5000, true), "wakes");
  stopped.stop(5001);
  expect(!stopped.holdsAwake(5001), "a disconnect ends the hold");
  expect(!stopped.onAcceptedWrite(6000, true), "and does not reset the rate limit");

  // Limit 3: 5 min awake per rolling hour, each wake charged hold + 7 s.
  SpeakingWake budget = {};
  for (uint32_t k = 0; k < 4; k++) {
    expect(restart(budget, k * 60000), "full-minute wakes 1-4 fit the budget");
    expect(budget.holdsAwake(k * 60000 + 59999), "each holds the full minute");
  }
  expect(!budget.holdsAwake(4 * 60000), "the fourth hold ended at its cap");
  expect(budget.usedMs() == 4 * 67000, "four full holds cost 4 x 67 s");
  expect(restart(budget, 4 * 60000), "the fifth wake still fits");
  expect(budget.holdsAwake(4 * 60000 + 24999), "but holds only the 25 s left");
  expect(!budget.holdsAwake(4 * 60000 + 25000), "cut where the budget ends");
  expect(budget.usedMs() == 300000, "exactly 5 min spent");
  for (uint32_t k = 5; k < 60; k++) {
    expect(!restart(budget, k * 60000), "no more speaking wakes this hour");
  }
  expect(budget.usedMs() == 300000, "refusals cost nothing");
  expect(!restart(budget, 3600000 + 66999), "the first wake counts for an hour plus its 67 s");
  expect(restart(budget, 3600000 + 67000), "then it leaves the budget");
  expect(budget.holdsAwake(3600000 + 67000 + 59999), "and the full minute is back");

  // No stranger pattern keeps the screen awake more than 5 min in any
  // rolling hour. Awake = a speaking hold or the 7 s tail after it.
  static bool awake[3 * 14400];  // 250 ms ticks over 3 hours
  SpeakingWake stranger = {};
  bool wasHolding = false;
  uint32_t tailUntil = 0;
  bool tail = false;
  for (uint32_t tick = 0; tick < 3 * 14400u; tick++) {
    const uint32_t t = tick * 250;
    const bool on = tick % 40 != 0;  // off for 250 ms every 10 s
    stranger.onAcceptedWrite(t, on);
    const bool holding = stranger.holdsAwake(t);
    if (wasHolding && !holding) {
      tail = true;
      tailUntil = t + 7000;
    }
    if (tail && t >= tailUntil) tail = false;
    wasHolding = holding;
    awake[tick] = holding || tail;
    expect(stranger.usedMs() <= SPEAKING_BUDGET_MS, "charged time never exceeds the budget");
  }
  uint32_t inWindow = 0;
  uint32_t worst = 0;
  for (uint32_t tick = 0; tick < 3 * 14400u; tick++) {
    inWindow += awake[tick] ? 250 : 0;
    if (tick >= 14400 && awake[tick - 14400]) inWindow -= 250;
    if (inWindow > worst) worst = inWindow;
  }
  expect(worst <= 300000, "awake at most 5 min in any rolling hour");
  expect(worst >= 240000, "and speaking does use the budget");

  // The rolling hour across the millis() wrap: start 30 min before it.
  const uint32_t base = 0xFFFFFFFFu - 1800000u;
  SpeakingWake wrapping = {};
  for (uint32_t k = 0; k < 5; k++) expect(restart(wrapping, base + k * 60000), "five wakes before the wrap");
  expect(!wrapping.holdsAwake(base + 4 * 60000 + 25000), "the fifth hold is cut at 25 s");
  expect(wrapping.usedMs() == 300000, "budget spent before the wrap");
  expect(!restart(wrapping, base + 2400000), "still refused 40 min in, after the wrap");
  expect(!restart(wrapping, base + 3600000 + 66999), "and until the first wake leaves the budget");
  expect(restart(wrapping, base + 3600000 + 67000), "then a wake, across the wrap");
  expect(wrapping.holdsAwake(base + 3600000 + 67000 + 59999), "holding the full minute");
  expect(!restart(wrapping, base + 3600000 + 67000 + 59999),
         "and the once-a-minute limit holds across the wrap");
}

// The characteristic's write-authorize verdict: format, then ownership,
// then the bytes; the ATT codes stay clear of the pass's 0x80-0x83.
static void writeVerdicts() {
  const uint8_t good[SCREEN_PAYLOAD_LENGTH] = {0x01, 0x01, 0x03, 0xFF, 0x02, 0x01, 0x07, 0x12, 0x02};
  const uint8_t bad[SCREEN_PAYLOAD_LENGTH] = {0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  ScreenPayload out = SENTINEL;
  expect(screenWriteVerdict(true, 0, good, 9, true, true, &out) == 0, "owned, well formed: accepted");
  expect(sameBytes(out, good), "an accepted trusted write is handed out whole");
  out = SENTINEL;
  expect(screenWriteVerdict(true, 0, good, 9, true, false, &out) == 0 && out.found == 0 && out.notify == 0 &&
             out.flags == SCREEN_FLAG_MODEST,
         "a plaintext write is accepted stripped");
  out = SENTINEL;
  expect(screenWriteVerdict(true, 0, good, 9, false, true, &out) == SCREEN_ATT_NOT_OWNED && untouched(out),
         "an unowned pin refuses a good write, even encrypted");
  expect(screenWriteVerdict(true, 0, bad, 9, false, false, &out) == SCREEN_ATT_NOT_OWNED && untouched(out),
         "and tells nothing about the bytes");
  expect(screenWriteVerdict(true, 0, bad, 9, true, false, &out) == SCREEN_ATT_BAD_VALUE && untouched(out),
         "a bad version is a bad value");
  expect(screenWriteVerdict(true, 0, good, 8, true, false, &out) == SCREEN_ATT_BAD_FORMAT && untouched(out),
         "a short write is bad format");
  expect(screenWriteVerdict(true, 0, good, 8, false, false, &out) == SCREEN_ATT_BAD_FORMAT,
         "format first, even on an unowned pin");
  expect(screenWriteVerdict(false, 0, good, 9, true, false, &out) == SCREEN_ATT_BAD_FORMAT && untouched(out),
         "a prepared (queued) write is bad format");
  expect(screenWriteVerdict(true, 1, good, 9, true, false, &out) == SCREEN_ATT_BAD_FORMAT && untouched(out),
         "an offset write is bad format");
  expect(screenWriteVerdict(true, 0, nullptr, 9, true, false, &out) == SCREEN_ATT_BAD_FORMAT, "no data is bad format");
  expect(SCREEN_ATT_BAD_FORMAT >= 0x84 && SCREEN_ATT_BAD_VALUE >= 0x84 && SCREEN_ATT_NOT_OWNED >= 0x84 &&
             SCREEN_ATT_BAD_FORMAT != SCREEN_ATT_BAD_VALUE && SCREEN_ATT_BAD_VALUE != SCREEN_ATT_NOT_OWNED &&
             SCREEN_ATT_BAD_FORMAT != SCREEN_ATT_NOT_OWNED,
         "three distinct codes after the pass's 0x80-0x83");
}

int main() {
  sharedVectors();
  accessors();
  acceptedBoundaries();
  rejections();
  speakingWake();
  writeVerdicts();
  if (failures) return EXIT_FAILURE;
  std::printf("screen_payload: all tests passed\n");
  return EXIT_SUCCESS;
}

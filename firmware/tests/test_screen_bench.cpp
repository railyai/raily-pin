// Host tests for screen_bench.h: serial `i`'s "scr" object and the line
// loop() prints when it applies a screen_state write.
#include <stdio.h>
#include <string.h>
#include <string>
#include "../RailyPinsP1/screen_bench.h"

static int failures = 0;
static void expect(bool condition, const char* name) {
  printf("%s %s\n", condition ? "PASS" : "FAIL", name);
  if (!condition) failures++;
}

static std::string json(const ScreenBenchApplied& last, const ScreenBenchCounts& counts, uint32_t now) {
  char buf[256];
  int n = formatScreenBenchJson(buf, sizeof(buf), last, counts, now);
  if (n <= 0 || n >= (int)sizeof(buf)) return "<overflow>";
  return std::string(buf, (size_t)n);
}

static ScreenPayload payload(uint8_t agent, uint8_t flags, uint8_t mascot, uint8_t locale) {
  ScreenPayload p = {};
  p.version = SCREEN_PAYLOAD_VERSION;
  p.agent = agent;
  p.found = 42;
  p.looking = 17;
  p.notify = 7;
  p.notifyCount = 99;
  p.flags = flags;
  p.mascot = mascot;
  p.locale = locale;
  return p;
}

static void formats() {
  ScreenBenchApplied last = {};
  ScreenBenchCounts counts = {};
  expect(json(last, counts, 5000) == "\"scr\":null", "null before any write reached the pin");

  counts.notOwned = 2;
  counts.badFormat = 1;
  expect(json(last, counts, 5000) == "\"scr\":{\"n\":0,\"rej\":{\"84\":1,\"85\":0,\"86\":2}}",
         "refused writes alone: the counts, no last write");

  counts = {};
  counts.accepted = 1;
  expect(json(last, counts, 5000) == "\"scr\":{\"n\":1,\"rej\":{\"84\":0,\"85\":0,\"86\":0}}",
         "accepted, not applied by loop() yet: the counts only");

  last.record(payload(SCREEN_AGENT_WATCH, SCREEN_FLAG_MODEST | SCREEN_FLAG_AGENT_SPEAKING, 0x21, SCREEN_LOCALE_EN),
              false, 1000);
  counts.accepted = 3;
  counts.badValue = 1;
  const std::string full = json(last, counts, 13999);
  expect(full ==
             "\"scr\":{\"n\":3,\"rej\":{\"84\":0,\"85\":1,\"86\":0},\"age_s\":12,\"agent\":1,\"flags\":36,"
             "\"mascot\":\"0x21\",\"loc\":2,\"enc\":0}",
         "the last applied write: agent, flags, mascot, locale, link, whole seconds since");
  expect(full.find("42") == std::string::npos && full.find("17") == std::string::npos &&
             full.find("99") == std::string::npos,
         "never the counts or the notify count");

  last.record(payload(SCREEN_AGENT_PAUSED, 0, 0x00, SCREEN_LOCALE_NONE), true, UINT32_MAX - 499);
  expect(json(last, counts, 1500).find("\"age_s\":2,") != std::string::npos, "age holds across the millis wrap");
  expect(json(last, counts, 1500).find("\"enc\":1}") != std::string::npos, "an encrypted link reads enc 1");

  // Release/unbind: the sketch clears the record and zeroes the counters
  // in replaceSecret(); nothing of the former owner's screen is left.
  last.clear();
  counts = {};
  expect(!last.valid && last.agent == 0 && last.flags == 0 && last.mascot == 0 && last.locale == 0 &&
             !last.encrypted && last.appliedMs == 0,
         "clear() forgets every field of the last write");
  expect(json(last, counts, 1500) == "\"scr\":null", "after a release `i` reads null again");
  counts.notOwned = 1;
  expect(json(last, counts, 1500) == "\"scr\":{\"n\":0,\"rej\":{\"84\":0,\"85\":0,\"86\":1}}",
         "and a write refused after it is counted from zero, without the old write");
}

static void worstCase() {
  ScreenBenchApplied last = {};
  last.valid = true;
  last.agent = 0xFF;
  last.flags = 0xFF;
  last.mascot = 0xFF;
  last.locale = 0xFF;
  last.encrypted = true;
  last.appliedMs = 1;
  ScreenBenchCounts counts = {UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX};
  char buf[256];
  int n = formatScreenBenchJson(buf, sizeof(buf), last, counts, 0);  // age: UINT32_MAX ms
  printf("worst-case scr: %d bytes: %s\n", n, buf);
  expect(n > 0 && (size_t)n <= SCREEN_BENCH_JSON_MAX, "every field at its top fits SCREEN_BENCH_JSON_MAX");
  // The sketch prints `,` + this + `}` from the extended line's 256-byte buffer.
  expect(SCREEN_BENCH_JSON_MAX < 256, "and the 256-byte line buffer holds it with room to spare");
}

static void appliedLine() {
  char buf[96];
  int n = snprintf(buf, sizeof(buf), SCREEN_APPLIED_LINE_FORMAT, 1u, 0x24u, 0x21u, 2u, 0u);
  expect(n > 0 && strcmp(buf, "screen_state: agent=1 flags=0x24 mascot=0x21 loc=2 enc=0\n") == 0,
         "the apply line reads agent, flags and mascot in hex, locale, link");
  n = snprintf(buf, sizeof(buf), SCREEN_APPLIED_LINE_FORMAT, 255u, 255u, 255u, 255u, 1u);
  expect(n > 0 && n < 64, "the apply line stays short at every field's top");
}

int main() {
  formats();
  worstCase();
  appliedLine();
  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("screen_bench: all tests passed\n");
  return 0;
}

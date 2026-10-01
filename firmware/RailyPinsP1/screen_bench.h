#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "screen_payload.h"

// Bench proof of what the phone wrote to screen_state (7B1E0014): serial
// `i` gains a "scr" object, and every applied write prints one line
// (docs/pins/firmware.md «Serial protocol»). Pure, host-tested in
// tests/test_screen_bench.cpp. Nothing here identifies the owner: never
// the counts or the notify kind (bytes 2-5), only what the screen shows
// and whether the link was encrypted.

// The last accepted write as loop() applied it (loop task only): flags
// after the plaintext strip, and the uptime of the apply.
struct ScreenBenchApplied {
  bool valid;
  uint8_t agent;
  uint8_t flags;
  uint8_t mascot;
  uint8_t locale;
  bool encrypted;
  uint32_t appliedMs;

  void record(const ScreenPayload& p, bool linkEncrypted, uint32_t now) {
    valid = true;
    agent = p.agent;
    flags = p.flags;
    mascot = p.mascot;
    locale = p.locale;
    encrypted = linkEncrypted;
    appliedMs = now;
  }

  // Release/unbind (spec §15: nothing screen-related survives a change of
  // owner, USB included): the sketch clears it with the counters.
  void clear() {
    valid = false;
    agent = 0;
    flags = 0;
    mascot = 0;
    locale = 0;
    encrypted = false;
    appliedMs = 0;
  }
};

// Write verdicts the BLE task counted (atomics in the sketch, read once
// per `i`): accepted, and refused by ATT code.
struct ScreenBenchCounts {
  uint32_t accepted;
  uint32_t badFormat;  // 0x84
  uint32_t badValue;   // 0x85
  uint32_t notOwned;   // 0x86
};

// The line loop() prints when it applies a write. A macro, so the
// compiler checks the arguments of Serial.printf against it.
#define SCREEN_APPLIED_LINE_FORMAT "screen_state: agent=%u flags=0x%02x mascot=0x%02x loc=%u enc=%u\n"

// The longest "scr" value formatScreenBenchJson can write, every field at
// the top of its type (tests/test_screen_bench.cpp formats that case and
// checks this bound). The sketch prints it after the extended device-info
// line, reusing that line's 256-byte buffer.
static const size_t SCREEN_BENCH_JSON_MAX = 160;

// `"scr":null` before any write reached the pin; otherwise the counts, and
// the last applied write once there is one:
//   "scr":{"n":3,"rej":{"84":0,"85":0,"86":1},"age_s":12,"agent":1,"flags":0,"mascot":"0x00","loc":2,"enc":0}
// Returns snprintf's result: the caller prints it only when 0 < n < size.
static inline int formatScreenBenchJson(char* buf, size_t size, const ScreenBenchApplied& last,
                                        const ScreenBenchCounts& c, uint32_t now) {
  if (!last.valid && c.accepted == 0 && c.badFormat == 0 && c.badValue == 0 && c.notOwned == 0) {
    return snprintf(buf, size, "\"scr\":null");
  }
  if (!last.valid) {
    return snprintf(buf, size, "\"scr\":{\"n\":%lu,\"rej\":{\"84\":%lu,\"85\":%lu,\"86\":%lu}}",
                    (unsigned long)c.accepted, (unsigned long)c.badFormat, (unsigned long)c.badValue,
                    (unsigned long)c.notOwned);
  }
  return snprintf(buf, size,
                  "\"scr\":{\"n\":%lu,\"rej\":{\"84\":%lu,\"85\":%lu,\"86\":%lu},\"age_s\":%lu,"
                  "\"agent\":%u,\"flags\":%u,\"mascot\":\"0x%02x\",\"loc\":%u,\"enc\":%u}",
                  (unsigned long)c.accepted, (unsigned long)c.badFormat, (unsigned long)c.badValue,
                  (unsigned long)c.notOwned, (unsigned long)((uint32_t)(now - last.appliedMs) / 1000u),
                  (unsigned)last.agent, (unsigned)last.flags, (unsigned)last.mascot, (unsigned)last.locale,
                  last.encrypted ? 1u : 0u);
}

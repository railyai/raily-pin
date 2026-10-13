// Host tests for voice_bench.h: the voice bench serial commands and the
// hex lines hardware/tools/voice_capture.py reads.
#include <stdio.h>
#include <string.h>

#include "../RailyPinsP1/voice_bench.h"

static int failures = 0;

static void expect(bool ok, const char* what) {
  printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) failures++;
}

static VoiceBenchCommand feedAll(VoiceBench& bench, const char* text, bool* lastConsumed = 0) {
  VoiceBenchCommand last = {VOICE_BENCH_NONE, 0};
  for (const char* c = text; *c; c++) {
    VoiceBenchCommand out;
    const bool consumed = bench.feed(*c, &out);
    if (lastConsumed) *lastConsumed = consumed;
    if (out.action != VOICE_BENCH_NONE) last = out;
  }
  return last;
}

static void testCommands() {
  VoiceBench bench = {};
  VoiceBenchCommand out;
  expect(bench.feed('v', &out) && out.action == VOICE_BENCH_HOLD_BEGIN, "v begins the fake hold");
  expect(bench.feed('V', &out) && out.action == VOICE_BENCH_HOLD_END, "V ends it (case matters)");
  expect(bench.feed('m', &out) && out.action == VOICE_BENCH_RAW_TOGGLE, "m toggles raw PCM");
  expect(bench.feed('d', &out) && out.action == VOICE_BENCH_DRIVE_TOGGLE, "d toggles the mic power drive");
  expect(bench.feed('A', &out) && out.action == VOICE_BENCH_GATT_DUMP, "A dumps the GATT table");
  expect(!bench.feed('p', &out) && out.action == VOICE_BENCH_NONE, "p is not ours: falls through");
  expect(!bench.feed('g', &out), "lowercase g stays gestures-spec's");
  expect(!bench.feed('i', &out) && !bench.feed('\n', &out), "i and newline fall through");
}

static void testGain() {
  VoiceBench bench = {};
  VoiceBenchCommand out = feedAll(bench, "G50");
  expect(out.action == VOICE_BENCH_GAIN && out.gain == 0x50, "G50: +20 dB");
  out = feedAll(bench, "G28");
  expect(out.action == VOICE_BENCH_GAIN && out.gain == 0x28, "G28: 0 dB");
  out = feedAll(bench, "G3c");
  expect(out.action == VOICE_BENCH_GAIN && out.gain == 0x3C, "lowercase hex digits");
  out = feedAll(bench, "G00");
  expect(out.action == VOICE_BENCH_GAIN && out.gain == 0, "G00: -20 dB");
  out = feedAll(bench, "G51");
  expect(out.action == VOICE_BENCH_GAIN_BAD, "above 0x50 is refused");
  bool consumed = true;
  out = feedAll(bench, "Gp", &consumed);
  expect(out.action == VOICE_BENCH_GAIN_BAD && !consumed, "G + a non-hex digit: refused, the digit falls through");
  out = feedAll(bench, "v");
  expect(out.action == VOICE_BENCH_HOLD_BEGIN, "after a refused G the parser is back to commands");
  out = feedAll(bench, "G5v", &consumed);
  expect(out.action == VOICE_BENCH_GAIN_BAD && !consumed, "G5 + v: refused, the v falls through (no hold)");
}

static void testHexLine() {
  char line[2 * 168 + 8];
  const uint8_t data[] = {0x02, 0x07, 0x01, 0x00, 0xAB, 0xFF};
  const uint16_t n = voiceHexLine(line, "VF", data, sizeof(data));
  expect(strcmp(line, "VF 02070100ABFF\n") == 0 && n == strlen(line), "VF line: prefix, space, upper hex, newline");
  static uint8_t audio[168];
  for (int i = 0; i < 168; i++) audio[i] = (uint8_t)i;
  const uint16_t m = voiceHexLine(line, "VF", audio, 168);
  expect(m == 3 + 336 + 1 && line[m - 1] == '\n' && line[m] == '\0', "a whole AUDIO frame fits 2 * 168 + 5 bytes");
}

int main() {
  testCommands();
  testGain();
  testHexLine();
  if (failures) {
    printf("voice_bench: %d failure(s)\n", failures);
    return 1;
  }
  printf("voice_bench: all passed\n");
  return 0;
}

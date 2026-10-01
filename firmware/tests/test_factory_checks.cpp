// Host tests for the Keyring Air factory test's pure half
// (RailyAirFactoryTest/factory_checks.h, docs/pins/air-factory-test.md).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../RailyAirFactoryTest/factory_checks.h"

static int failures = 0;

static void expect(bool ok, const char* what) {
  printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) failures++;
}

static void testBattery() {
  expect(vbatMillivolts(0) == 0, "0 counts is 0 V");
  // 4.2 V through 1 M / 510 k is 1.4185 V at the pin: 1936 counts of 3.0 V.
  const uint32_t full = vbatMillivolts(1936);
  expect(full >= 4195 && full <= 4205, "a full cell reads 4.2 V");
  const uint32_t low = vbatMillivolts(1475);
  expect(low >= 3195 && low <= 3205, "a flat cell reads 3.2 V");
  expect(vbatMillivolts(4095) > 8800 && vbatMillivolts(4095) < 8900, "full scale (8.88 V) stays in range, no overflow");
  expect(vbatMillivolts(1960) > VBAT_MAX_MV, "the 3.0 V range still sees an overcharged 4.25 V+ cell");
  expect(vbatInRange(3500) && vbatInRange(4250), "3.5 V and 4.25 V pass");
  expect(!vbatInRange(3499) && !vbatInRange(4251), "outside the OLED pump's range fails");
}

static void testHapticRegisters() {
  expect(da7280VoltageStep(2106) == 90, "2.106 V nominal is 90 steps of 23.4 mV");
  expect(da7280VoltageStep(2260) == 96, "2.26 V absolute is 96 steps");
  expect(da7280VoltageStep(10000) == 0xFF, "the voltage register saturates");
  expect(da7280CurrentStep(165400) == 19, "165.4 mA is step 19 (28.6 mA + 7.2 mA a step)");
  expect(da7280CurrentStep(10000) == 0 && da7280CurrentStep(1000000) == 0x1F, "the current step is clamped to 5 bits");
  expect(da7280V2iFactor(13800, 19) == 197, "V2I factor 13.8 ohm at step 19 is 197 (SparkFun reads 197.09)");
  const LraPeriod p = da7280LraPeriod(170);
  const uint32_t period = ((uint32_t)p.high << 7) | p.low;
  expect(period == 4412, "170 Hz is a period of 4412 x 1333.32 ns");
  expect(p.high == 34 && p.low == 60, "split as bits 14:7 and 6:0");
  expect(p.low < 0x80, "the low register keeps its top bit clear");
  const LraPeriod q = da7280LraPeriod(235);
  expect((((uint32_t)q.high << 7) | q.low) == 3192, "235 Hz is a period of 3192");
  expect(BUZZ_AMPLITUDE < 0x80 && BUZZ_MS <= 300, "the factory buzz is short and below half scale");
}

static void testResults() {
  FactoryResults results = {};
  expect(!results.allPass() && results.failedCount() == CHECK_COUNT, "nothing run: not proven, every check counts");
  for (int i = 0; i < CHECK_COUNT; i++) results.set((FactoryCheck)i, true);
  expect(results.allPass() && results.failedCount() == 0, "all checks passed: the board passes");
  results.set(CHECK_BUTTON, false);
  expect(!results.allPass() && results.failedCount() == 1, "one failure fails the board");
}

static void testLine() {
  char line[160];
  const int n = formatCheckLine(line, sizeof(line), CHECK_HAPTIC, true, "chip 0xBA, irq 0x00");
  expect(n > 0 && strcmp(line, "{\"ft\":\"haptic\",\"ok\":true,\"detail\":\"chip 0xBA, irq 0x00\"}") == 0,
         "one JSON line per check");
  formatCheckLine(line, sizeof(line), CHECK_SCREEN, false, "no \"ack\" at 0x3C\\\n");
  expect(strcmp(line, "{\"ft\":\"screen\",\"ok\":false,\"detail\":\"no ack at 0x3C\"}") == 0,
         "quotes, backslashes and control bytes never break the line");
  char tiny[16];
  expect(formatCheckLine(tiny, sizeof(tiny), CHECK_BLE, true, "advertising") == -1, "a line that does not fit says so");
  bool human = true;
  for (int i = 0; i < CHECK_COUNT; i++) human = human && strchr(kCheckLabels[i], '_') == NULL;
  expect(human, "the operator's labels are words, not log keys");
}

int main() {
  testBattery();
  testHapticRegisters();
  testResults();
  testLine();
  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  return 0;
}

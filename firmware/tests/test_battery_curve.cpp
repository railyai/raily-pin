// Host tests for battery_curve.h (docs/pins/battery-telemetry-design.md):
// the divider's inverse gain (a full cell reads ~4200 mV, never ~500), no
// 32-bit overflow at full scale, the LiPo curve at and between its points,
// the power byte, the «notify only on change» mark and the reading schedule.
#include <stdint.h>
#include <stdio.h>

#include "../RailyPinsP1/battery_curve.h"

static int failures = 0;
static void expect(bool condition, const char* name) {
  printf("%s %s\n", condition ? "PASS" : "FAIL", name);
  if (!condition) failures++;
}

// The raw 12-bit count a VBAT gives at the pin, as the board would read it.
static uint32_t rawFor(uint32_t vbatMv) {
  const double pinMv = vbatMv * 510.0 / 1510.0;
  return (uint32_t)(pinMv / 2400.0 * 4096.0 + 0.5);
}

static bool near(uint32_t got, uint32_t want, uint32_t tolerance) {
  return got + tolerance >= want && got <= want + tolerance;
}

int main() {
  // Inverse gain: 8 samples of a full cell come back as ~4200 mV.
  expect(near(batteryMillivolts(rawFor(4200) * 8, 8), 4200, 3), "full cell 4200 mV reads ~4200 mV");
  expect(near(batteryMillivolts(rawFor(3700) * 8, 8), 3700, 3), "3700 mV reads ~3700 mV");
  expect(batteryMillivolts(rawFor(4200) * 8, 8) > 4000, "not the ~1/3 ratio (a full cell is not ~500 mV)");
  expect(near(batteryMillivolts(rawFor(4200), 1), 4200, 3), "one sample works too");
  // Full scale: 8 × 4095 × 2400 × 1510 overflows 32 bits; the result is
  // 4095/4096 × 2400 × 1510/510 ≈ 7104 mV.
  expect(near(batteryMillivolts(4095u * 8, 8), 7104, 1), "full scale 4095 x 8 does not overflow");
  expect(batteryMillivolts(0, 8) == 0, "0 counts is 0 mV");
  expect(batteryMillivolts(1234, 0) == 0, "no samples is 0 mV, not a division by zero");
  // Averaging: mixed samples average, not sum.
  expect(near(batteryMillivolts(rawFor(4000) * 4 + rawFor(3800) * 4, 8), 3900, 3), "8 samples average");

  // The curve at its own points.
  const uint32_t points[][2] = {{4200, 100}, {4100, 90}, {4000, 80}, {3900, 65}, {3800, 50}, {3750, 40},
                                {3700, 30},  {3650, 20}, {3600, 10}, {3500, 5},  {3300, 0}};
  bool exact = true;
  for (const auto& p : points) exact = exact && batteryPercentFromMv(p[0]) == p[1];
  expect(exact, "every table point maps to its percent");
  // Clamped ends.
  expect(batteryPercentFromMv(4350) == 100, "above 4200 mV is 100 %");
  expect(batteryPercentFromMv(7104) == 100, "full scale is 100 %");
  expect(batteryPercentFromMv(3000) == 0, "below 3300 mV is 0 %");
  expect(batteryPercentFromMv(0) == 0, "0 mV is 0 %");
  // Interpolation, rounded to the nearest percent.
  expect(batteryPercentFromMv(4150) == 95, "4150 mV is 95 %");
  expect(batteryPercentFromMv(3950) == 73, "3950 mV is 72.5 -> 73 %");
  expect(batteryPercentFromMv(3725) == 35, "3725 mV is 35 %");
  expect(batteryPercentFromMv(3400) == 3, "3400 mV is 2.5 -> 3 %");
  expect(batteryPercentFromMv(3301) == 0, "3301 mV is still 0 %");
  // Monotone, never above 100.
  bool monotone = true;
  uint8_t last = 0;
  for (uint32_t mv = 2500; mv <= 4500; mv++) {
    const uint8_t pct = batteryPercentFromMv(mv);
    if (pct < last || pct > 100) monotone = false;
    last = pct;
  }
  expect(monotone, "curve is non-decreasing and within 0-100");

  // The power byte.
  expect(batteryPowerByte(false, false) == 0x00, "off USB, not charging = 0x00");
  expect(batteryPowerByte(true, false) == 0x01, "USB = bit 0");
  expect(batteryPowerByte(false, true) == 0x02, "charging = bit 1");
  expect(batteryPowerByte(true, true) == 0x03, "USB and charging = 0x03");

  // Notify only on change.
  BatteryNotifyMark mark = {};
  expect(mark.percentDue(50) && mark.powerDue(0), "the first reading always notifies");
  mark.percentNotified(50);
  mark.powerNotified(0);
  expect(!mark.percentDue(50), "the same percent does not notify");
  expect(mark.percentDue(49) && mark.percentDue(51), "a 1 % move notifies");
  expect(!mark.powerDue(0), "the same power byte does not notify");
  expect(mark.powerDue(0x01) && mark.powerDue(0x02), "a power bit flip notifies");
  // A notify that failed does not move the mark: the next reading resends.
  expect(mark.percentDue(48), "48 % due");
  expect(mark.percentDue(48), "still due when the 48 % notify did not go out");
  mark.percentNotified(48);
  expect(!mark.percentDue(48), "not due once it went out");

  // The schedule: every 60 s, and at once on a power change.
  expect(!batteryReadingDue(1000, 61000, 0, 0), "not due before 60 s");
  expect(batteryReadingDue(61000, 61000, 0, 0), "due at 60 s");
  expect(batteryReadingDue(1000, 61000, 0x01, 0), "due at once when USB appears");
  expect(batteryReadingDue(1000, 61000, 0x00, 0x03), "due at once when USB goes");
  expect(!batteryReadingDue(0xFFFFFF00u, 0xFFFFFF00u + BATTERY_SAMPLE_PERIOD_MS, 0, 0),
         "millis rollover: not due early");
  expect(batteryReadingDue(0xFFFFFF00u + BATTERY_SAMPLE_PERIOD_MS, 0xFFFFFF00u + BATTERY_SAMPLE_PERIOD_MS, 0, 0),
         "millis rollover: due on time");

  // Low and critical from VDD.
  expect(BATTERY_POWER_LOW == 0x04 && BATTERY_POWER_CRITICAL == 0x08, "low = bit 2, critical = bit 3");
  {
    BatteryLowWatch w = {};
    w.reading(3290, false);
    w.reading(3290, false);
    expect(w.level == 0 && w.powerBits() == 0, "two readings under 3300 mV are not low yet");
    w.reading(3290, false);
    expect(w.level == 1 && w.powerBits() == BATTERY_POWER_LOW, "the third reading in a row is low");
    w.reading(3400, false);
    expect(w.level == 1, "a recovering cell stays low off USB");
    w.reading(3100, false);
    w.reading(3100, false);
    w.reading(3320, false);
    w.reading(3100, false);
    w.reading(3100, false);
    expect(w.level == 1, "a reading back above breaks the run toward critical");
    w.reading(3100, false);
    expect(w.level == 2 && w.powerBits() == BATTERY_POWER_CRITICAL, "three in a row under 3150 mV is critical");
    w.reading(3375, true);
    expect(w.level == 0 && w.powerBits() == 0, "USB clears the warning");
  }
  {
    BatteryLowWatch w = {};
    w.reading(3290, false);
    w.reading(3290, false);
    w.reading(3375, true);
    w.reading(3290, false);
    expect(w.level == 0, "USB in between restarts the count");
    w.reading(3000, false);
    w.reading(3000, false);
    expect(w.level == 1, "three under 3300 mV, only two of them under 3150 mV: low, not critical");
    w.reading(3000, false);
    expect(w.level == 2, "the third reading under 3150 mV is critical");
    BatteryLowWatch m = {};
    m.reading(3290, false);
    m.reading(3290, false);
    m.reading(3100, false);
    expect(m.level == 1, "one reading under 3150 mV after two lows does not make critical");
    BatteryLowWatch v = {};
    v.reading(3000, false);
    v.reading(3000, false);
    v.reading(3000, false);
    expect(v.level == 2, "three readings under 3150 mV from fine are critical at once");
    BatteryLowWatch u = {};
    u.reading(3150, false);
    u.reading(3150, false);
    u.reading(3150, false);
    expect(u.level == 1, "3150 mV itself is low, not critical");
    BatteryLowWatch t = {};
    for (int i = 0; i < 10; i++) t.reading(3300, false);
    expect(t.level == 0, "3300 mV itself is fine");
  }
  // The real 2026-10-07 discharge, one record a minute: low comes about
  // 5 h before the pin went off, critical about 2 h before.
  {
    FILE* f = fopen("fixtures/vdd_discharge_2026-10-07.csv", "r");
    expect(f != nullptr, "the discharge fixture opens");
    if (f) {
      char line[64];
      BatteryLowWatch w = {};
      unsigned long t = 0, end = 0, lowAt = 0, criticalAt = 0;
      unsigned vdd = 0, p031 = 0, usb = 0;
      int records = 0;
      while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "%lu,%u,%u,%u", &t, &vdd, &p031, &usb) != 4) continue;
        records++;
        w.reading(vdd, usb != 0);
        if (w.level >= 1 && !lowAt) lowAt = t;
        if (w.level >= 2 && !criticalAt) criticalAt = t;
        end = t;
      }
      fclose(f);
      expect(records == 732, "732 records");
      const double lowH = (end - lowAt) / 3600.0, criticalH = (end - criticalAt) / 3600.0;
      printf("discharge replay: low %.2f h, critical %.2f h before the last record\n", lowH, criticalH);
      expect(lowAt && lowH > 4.5 && lowH < 6.0, "low 4.5-6 h before the end");
      expect(criticalAt && criticalH > 1.3 && criticalH < 2.2, "critical 1.3-2.2 h before the end");
    }
  }

  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("all battery_curve tests passed\n");
  return 0;
}

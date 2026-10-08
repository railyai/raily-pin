#ifndef RAILY_BATTERY_CURVE_H
#define RAILY_BATTERY_CURVE_H

// Battery telemetry, the pure part (docs/pins/battery-telemetry-design.md):
// the ADC average undone through the XIAO's VBAT divider, the LiPo curve,
// the power-state byte and the «notify only on change» rule. No Arduino
// dependency, so tests/test_battery_curve.cpp runs the same code on the host.
// The sketch owns the pins, the ADC and the GATT writes.

#include <stdint.h>

// The onboard divider: VBAT → 1 MΩ → P0.31 → 510 kΩ → P0.14 (driven LOW).
// P0.31 sees VBAT × 510k / 1510k, so the inverse gain is 1510 / 510 (≈ ×2.96).
// Multiplying by the ~1/3 ratio instead would read a full cell as ~0.5 V
// («Traps» 4). Not yet checked against the board's schematic: calibrate.
static const uint32_t BATTERY_DIVIDER_TOP_KOHM = 1000;
static const uint32_t BATTERY_DIVIDER_BOTTOM_KOHM = 510;
// The sketch reads 12-bit samples against the internal 0.6 V reference
// with gain 1/4 (AR_INTERNAL_2_4): full scale 2400 mV. A 4.35 V cell gives
// 1.47 V at the pin, well inside it.
static const uint32_t BATTERY_ADC_FULL_SCALE_MV = 2400;
static const uint32_t BATTERY_ADC_COUNTS = 4096;
// «Traps» 5: at least 8 samples per reading.
static const uint32_t BATTERY_SAMPLES = 8;

// VBAT in millivolts from the sum of `samples` raw 12-bit readings. 64-bit
// on purpose: 8 × 4095 × 2400 × 1510 does not fit 32 bits. 0 when there are
// no samples.
static inline uint32_t batteryMillivolts(uint32_t rawSum, uint32_t samples) {
  if (samples == 0) return 0;
  const uint64_t numerator = (uint64_t)rawSum * BATTERY_ADC_FULL_SCALE_MV *
                             (BATTERY_DIVIDER_TOP_KOHM + BATTERY_DIVIDER_BOTTOM_KOHM);
  const uint64_t denominator = (uint64_t)samples * BATTERY_ADC_COUNTS * BATTERY_DIVIDER_BOTTOM_KOHM;
  return (uint32_t)((numerator + denominator / 2) / denominator);
}

// The starting LiPo curve («Traps» 6), descending; calibrate on the real cell.
struct BatteryCurvePoint {
  uint16_t mv;
  uint8_t percent;
};
static const BatteryCurvePoint BATTERY_CURVE[] = {
    {4200, 100}, {4100, 90}, {4000, 80}, {3900, 65}, {3800, 50}, {3750, 40},
    {3700, 30},  {3650, 20}, {3600, 10}, {3500, 5},  {3300, 0},
};
static const uint32_t BATTERY_CURVE_POINTS = sizeof(BATTERY_CURVE) / sizeof(BATTERY_CURVE[0]);

// 0–100 %, linear between the table's points, rounded to the nearest
// percent; clamped at both ends.
static inline uint8_t batteryPercentFromMv(uint32_t mv) {
  if (mv >= BATTERY_CURVE[0].mv) return BATTERY_CURVE[0].percent;
  for (uint32_t i = 1; i < BATTERY_CURVE_POINTS; i++) {
    const BatteryCurvePoint hi = BATTERY_CURVE[i - 1];
    const BatteryCurvePoint lo = BATTERY_CURVE[i];
    if (mv >= lo.mv) {
      const uint32_t span = (uint32_t)(hi.mv - lo.mv);
      const uint32_t rise = (uint32_t)(hi.percent - lo.percent);
      return (uint8_t)(lo.percent + ((mv - lo.mv) * rise + span / 2) / span);
    }
  }
  return BATTERY_CURVE[BATTERY_CURVE_POINTS - 1].percent;
}

// The power-state characteristic (7B1E0007): bit 0 USB power present, bit 1
// charging, bit 2 battery low, bit 3 battery critical (BatteryLowWatch);
// every other bit 0.
static const uint8_t BATTERY_POWER_USB = 0x01;
static const uint8_t BATTERY_POWER_CHARGING = 0x02;
static const uint8_t BATTERY_POWER_LOW = 0x04;
static const uint8_t BATTERY_POWER_CRITICAL = 0x08;
static inline uint8_t batteryPowerByte(bool usbPresent, bool charging) {
  return (uint8_t)((usbPresent ? BATTERY_POWER_USB : 0) | (charging ? BATTERY_POWER_CHARGING : 0));
}

// «Low» and «critical» from the nRF's own VDD, for the Expansion Board kit,
// whose cell never reaches a XIAO pin (battery-telemetry-design.md
// «Expansion Board kit»). Its buck holds VDD at ~3.38 V while the cell is
// full enough and lets it sag once the cell runs out. The 2026-10-07
// discharge (≈95 h, BLE linked, LED load on) crossed 3300 mV 5.4 h and
// 3150 mV 1.75 h before the pin went off: one run, so the hours are a guide.
static const uint32_t BATTERY_LOW_VDD_MV = 3300;
static const uint32_t BATTERY_CRITICAL_VDD_MV = 3150;
// Readings in a row (60 s apart) under a threshold before the level moves,
// each threshold on its own, so a motor pulse or one noisy sample cannot
// raise a warning.
static const uint8_t BATTERY_LOW_CONFIRM = 3;

// The level only goes down off USB and clears on USB: a cell that recovers
// a little at rest does not flap the warning, and plugging in ends it.
// Each threshold keeps its own run, so «critical» needs three readings under
// 3150 mV itself, not a run that only ended there.
struct BatteryLowWatch {
  uint8_t level;          // 0 fine, 1 low, 2 critical
  uint8_t belowLow;       // readings in a row under 3300 mV
  uint8_t belowCritical;  // readings in a row under 3150 mV

  void reading(uint32_t vddMv, bool usbPresent) {
    if (usbPresent) {
      level = 0;
      belowLow = 0;
      belowCritical = 0;
      return;
    }
    belowLow = vddMv < BATTERY_LOW_VDD_MV ? (uint8_t)(belowLow < 255 ? belowLow + 1 : 255) : 0;
    belowCritical = vddMv < BATTERY_CRITICAL_VDD_MV ? (uint8_t)(belowCritical < 255 ? belowCritical + 1 : 255) : 0;
    if (belowCritical >= BATTERY_LOW_CONFIRM && level < 2) level = 2;
    if (belowLow >= BATTERY_LOW_CONFIRM && level < 1) level = 1;
  }
  uint8_t powerBits() const {
    return level >= 2 ? BATTERY_POWER_CRITICAL : level == 1 ? BATTERY_POWER_LOW : 0;
  }
};

// The last value each characteristic notified («Traps» 7: notify only when
// the percent moved by 1 % or more, or a power bit flipped). The sketch
// writes both values on every reading, so a read is always current; only a
// notify that went out moves the mark, so a dropped notify is sent again on
// the next reading.
struct BatteryNotifyMark {
  bool percentSent;
  uint8_t percent;
  bool powerSent;
  uint8_t power;

  bool percentDue(uint8_t now) const { return !percentSent || now != percent; }
  bool powerDue(uint8_t now) const { return !powerSent || now != power; }
  void percentNotified(uint8_t now) {
    percentSent = true;
    percent = now;
  }
  void powerNotified(uint8_t now) {
    powerSent = true;
    power = now;
  }
};

// When readings happen: every 60 s from loop(), and at once when the power
// byte changes (polled each second; in the sleep build the 1 s backstop
// already wakes loop() that often).
static const uint32_t BATTERY_SAMPLE_PERIOD_MS = 60000;
static const uint32_t BATTERY_POWER_POLL_MS = 1000;

// The next reading is due: the period ran out (unsigned difference, so it
// survives the millis() rollover) or the power byte changed since the last
// reading.
static inline bool batteryReadingDue(uint32_t now, uint32_t nextAtMs, uint8_t powerNow, uint8_t powerAtLastReading) {
  return (int32_t)(now - nextAtMs) >= 0 || powerNow != powerAtLastReading;
}

#endif

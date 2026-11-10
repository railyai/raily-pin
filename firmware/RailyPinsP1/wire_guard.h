#pragma once

#include <stdint.h>

// When the hardware Wire bus (D4 SDA, D5 SCL: the OLED and, on the Keyring
// Air, the DA7280) may carry a transfer at runtime. Pure: host tested in
// tests/test_da7280.cpp; the sketch's wireBusReady() reads the two lines and
// runs the recovery.
//
// The core's TWIM driver (libraries/Wire/Wire_nRF52.cpp) busy-waits on the
// TWIM events with no timeout, so a slave that holds SDA or SCL low (a
// brownout or reset in the middle of a byte) would hang loop() inside Wire.
// Before each batch of transfers (one OLED frame, one DA7280 register) both
// lines must read high. A low line skips the batch; at most once every
// WIRE_RECOVERY_INTERVAL_MS the bus is clocked free instead (TWIM off, up to
// 9 clocks and a STOP, soft_i2c.h SoftI2c::idle()), and the batch runs only
// when both lines are high afterwards. A line that goes low in the middle of
// a transfer still blocks inside the core: that is the watchdog's case
// (docs/pins/firmware.md, Watchdog).

// One recovery a second at most: it costs a few milliseconds (a clock may
// wait SOFT_I2C_STRETCH_LIMIT half periods), and the panel asks every frame
// (83 ms) while the motor retries every 20 ms.
static const uint32_t WIRE_RECOVERY_INTERVAL_MS = 1000;

enum WireStep : uint8_t {
  WIRE_GO = 0,       // both lines high: run the transfers
  WIRE_RECOVER = 1,  // a line is low: clock the bus free now, then ask afterRecovery()
  WIRE_SKIP = 2,     // a line is low and a recovery ran less than the interval ago
};

struct WireGuard {
  bool stuck;             // the last look found a line low
  bool tried;             // a recovery has run since boot (lastTryMs is valid)
  bool freedNow;          // before() found a stuck bus high again: report it once
  uint16_t episodeTries;  // recoveries in this stuck episode (1: the first, report it)
  uint32_t lastTryMs;
  uint32_t skipped;     // batches skipped since boot
  uint32_t recoveries;  // recoveries run since boot

  // idle: SDA and SCL both read high now.
  WireStep before(bool idle, uint32_t now) {
    freedNow = false;
    if (idle) {
      if (stuck) {
        stuck = false;
        freedNow = true;
        episodeTries = 0;
      }
      return WIRE_GO;
    }
    stuck = true;
    // Unsigned difference: right across the millis() wrap.
    if (!tried || (uint32_t)(now - lastTryMs) >= WIRE_RECOVERY_INTERVAL_MS) {
      tried = true;
      lastTryMs = now;
      recoveries++;
      if (episodeTries < 0xFFFF) episodeTries++;
      return WIRE_RECOVER;
    }
    skipped++;
    return WIRE_SKIP;
  }

  // After the recovery; idle as in before(). True: run the transfers.
  bool afterRecovery(bool idle) {
    if (idle) {
      stuck = false;
      episodeTries = 0;
      return true;
    }
    skipped++;
    return false;
  }
};

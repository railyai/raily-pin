#pragma once

#include <stdint.h>

// The nRF52840 watchdog (WDT) for the loop task (docs/pins/firmware.md,
// Watchdog). Pure numbers: host tested in tests/test_idle_sleep.cpp; the
// sketch starts it in setup() and feeds it once per loop() pass.
//
// -DRAILY_WATCHDOG=0 builds a bench image that never starts it. Such an
// image still feeds a watchdog a previous image left running: a soft reset
// (a restart pass, the end of a BLE update) does not stop the WDT, and its
// configuration stays locked until a reset that does.
#ifndef RAILY_WATCHDOG
#define RAILY_WATCHDOG 1
#endif

// 16 s. The longest legitimate stretch without a feed:
// - in the app, the bounded seal wait (SEAL_WAIT_MS, 5 s) at boot or for a
//   new link; setup() feeds between its flash steps (a LittleFS page erase
//   is at most 85 ms on the nRF52840) and every 10 ms of its 4 s serial
//   wait; the idle sleep blocks a pass at most IDLE_BACKSTOP_MS (1 s);
// - in the bootloader, which feeds only from its event loop
//   (wait_for_events()): a USB serial DFU erases the whole new image in one
//   go, 85 ms a page at most: about 5 s for the 238-248 KB images of
//   2026-11 (59-61 pages); 16 s covers about 180 pages (720 KB).
// A wedged pin then resets within 16 s; nothing in the product needs a
// faster reset.
static const uint32_t WATCHDOG_TIMEOUT_MS = 16000;

// The WDT counts the 32.768 kHz clock: CRV ticks to the timeout.
static const uint32_t WATCHDOG_TICK_HZ = 32768;
// RR[n] takes only this value as a reload request (nRF52840 PS, WDT, RR).
static const uint32_t WATCHDOG_RELOAD_VALUE = 0x6E524635u;
// The smallest CRV the WDT accepts (nRF52840 PS, WDT, CRV).
static const uint32_t WATCHDOG_CRV_MIN = 0xF;

static inline uint32_t watchdogCrv(uint32_t ms) {
  const uint64_t ticks = ((uint64_t)ms * WATCHDOG_TICK_HZ) / 1000u;
  if (ticks < WATCHDOG_CRV_MIN) return WATCHDOG_CRV_MIN;
  return ticks > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)ticks;
}

// What a running WDT's CRV means in ms (it may come from an earlier image).
static inline uint32_t watchdogCrvMs(uint32_t crv) {
  return (uint32_t)(((uint64_t)crv * 1000u) / WATCHDOG_TICK_HZ);
}

// The seal (ECDH + AES-CCM on the CryptoCell, on its own task): how long
// the loop task waits for it before that seal fails. No seal time is
// measured in the docs yet (serial prints `seal: N ms` for the bench); the
// app waits 3 s for device_secret (companion-app.md), so a slower seal is
// useless to it anyway, and 5 s stays well inside the watchdog.
static const uint32_t SEAL_WAIT_MS = 5000;

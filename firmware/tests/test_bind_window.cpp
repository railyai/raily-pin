// Host tests for bind_window.h (docs/pins/diy-claimless-bind.md §4.1).
#include <cstdio>
#include <cstdlib>

#include "../RailyPinsP1/bind_window.h"
#include "../RailyPinsP1/server_pass.h"

static int failures = 0;

static void expect(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    failures++;
  }
}

int main() {
  // Who was at the pin for this boot.
  expect(bindWindowOpensAtBoot(0, false), "power-on (no reason bit) opens");
  expect(bindWindowOpensAtBoot(RESETREAS_RESETPIN, false), "RESET pin opens");
  expect(bindWindowOpensAtBoot(RESETREAS_RESETPIN | RESETREAS_SREQ, false),
         "RESET pin then the bootloader's soft reset still opens");
  expect(!bindWindowOpensAtBoot(RESETREAS_SREQ, false), "soft reset (restart pass, release, DFU end) stays closed");
  expect(!bindWindowOpensAtBoot(RESETREAS_DOG, false), "watchdog stays closed");
  expect(!bindWindowOpensAtBoot(RESETREAS_LOCKUP, false), "lockup stays closed");
  expect(!bindWindowOpensAtBoot(RESETREAS_SREQ | RESETREAS_DOG | RESETREAS_LOCKUP, false),
         "software reasons together stay closed");
  expect(!bindWindowOpensAtBoot(RESETREAS_VBUS, false), "VBUS wake from system OFF stays closed");
  expect(bindWindowOpensAtBoot(RESETREAS_SREQ, true), "a boot that minted a secret opens");

  // The window's clock.
  BindWindow closed = {};
  expect(!closed.isOpen(0) && closed.remainingMs(5) == 0, "a window never opened is closed");
  BindWindow window = {};
  window.openAt(1000);
  expect(window.isOpen(1000) && window.remainingMs(1000) == BIND_WINDOW_MS, "open for 180 s from the start");
  expect(window.isOpen(1000 + BIND_WINDOW_MS - 1), "open until the last millisecond");
  expect(!window.isOpen(1000 + BIND_WINDOW_MS), "closed at 180 s");
  expect(!window.isOpen(1000 + 10 * BIND_WINDOW_MS), "stays closed");
  BindWindow wrapping = {};
  wrapping.openAt(0xFFFFFF00u);
  expect(wrapping.isOpen(0x00000100u), "open across the millis() wrap");
  expect(wrapping.remainingMs(0x00000100u) == BIND_WINDOW_MS - 0x200, "remaining across the wrap");

  // What the pin serves.
  expect(bindWindowServes(true, false, true), "unowned pin in its window serves");
  expect(!bindWindowServes(true, true, true), "an owned pin never serves");
  expect(!bindWindowServes(true, false, false), "a closed window does not serve");
  expect(!bindWindowServes(false, false, true), "no secret, nothing to serve");

  // device_info bind_s.
  expect(bindWindowSeconds(true, BIND_WINDOW_MS) == 180, "full window reads 180");
  expect(bindWindowSeconds(true, 1) == 1, "the last millisecond still reads 1");
  expect(bindWindowSeconds(true, 1500) == 2, "rounded up");
  expect(bindWindowSeconds(false, BIND_WINDOW_MS) == 0, "not serving reads 0");

  // Challenge flag bit 2 is new and distinct.
  expect(PASS_FLAG_BIND_WINDOW == 0x04, "bit 2");
  expect((PASS_FLAG_BIND_WINDOW & (PASS_FLAG_OWNED | PASS_FLAG_NO_SECRET)) == 0, "distinct from the other flags");

  if (failures) return EXIT_FAILURE;
  std::printf("bind_window: all tests passed\n");
  return EXIT_SUCCESS;
}

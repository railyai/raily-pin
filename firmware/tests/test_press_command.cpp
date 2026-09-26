#include <stdio.h>
#include "../RailyPinsP1/press_command_gate.h"

static int failures = 0;
static void expect(bool condition, const char* name) {
  printf("%s %s\n", condition ? "PASS" : "FAIL", name);
  if (!condition) failures++;
}

int main() {
  PressCommandGate gate = {};
  expect(gate.allows(0), "first press allowed at boot");
  gate.record(0);
  expect(!gate.allows(1), "second press held during cooldown");
  expect(!gate.allows(1999), "cooldown holds through final millisecond");
  expect(gate.allows(2000), "press allowed at cooldown boundary");
  gate.record(2000);
  expect(!gate.allows(2001), "accepted press restarts cooldown");

  PressCommandGate wrapped = {};
  wrapped.record(0xFFFFFF00u);
  expect(!wrapped.allows(100), "cooldown survives millis wrap");
  expect(wrapped.allows(1744), "cooldown expires after millis wrap");
  return failures ? 1 : 0;
}

#pragma once

#include <stdint.h>

// No cooldown applies before the first command. Unsigned subtraction keeps
// the interval correct across the 32-bit millis() rollover.
struct PressCommandGate {
  static const uint32_t MIN_INTERVAL_MS = 2000;
  uint32_t lastAcceptedMs;
  bool hasAccepted;

  bool allows(uint32_t now) const {
    return !hasAccepted || (uint32_t)(now - lastAcceptedMs) >= MIN_INTERVAL_MS;
  }

  void record(uint32_t now) {
    lastAcceptedMs = now;
    hasAccepted = true;
  }
};

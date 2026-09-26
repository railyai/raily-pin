#pragma once

#include <stdint.h>

struct FeedbackLights {
  bool red;
  bool green;
  bool blue;
};

// Pure, bounded LED state machine. BLE callbacks only publish the latest
// pattern; all GPIO writes happen in loop(), never in the radio task.
struct FeedbackState {
  uint32_t edgeAtMs;
  uint32_t lastBlinkMs;
  uint32_t lastPatternAtMs;
  uint8_t flashesLeft;
  uint8_t queuedPattern;
  bool active;
  bool hasStartedPattern;
  bool redOn;
  bool blueOn;
  bool wasConnected;
  static const uint32_t MIN_PATTERN_INTERVAL_MS = 800;

  FeedbackLights tick(uint32_t now, bool connected, uint8_t pattern) {
    if (wasConnected != connected) {
      blueOn = false;
      lastBlinkMs = now;
      wasConnected = connected;
    }
    if (pattern != 0) {
      if (pattern > 4) pattern = 4;
      queuedPattern = pattern;  // bounded latest-wins, no unbounded queue
    }
    // At most two four-flash patterns can be advanced in one tick. Unsigned
    // subtraction preserves deadlines through the 32-bit millis rollover.
    while (active && (uint32_t)(now - edgeAtMs) >= 80) {
      edgeAtMs += 80;
      if (redOn) {
        redOn = false;
        flashesLeft--;
      } else if (flashesLeft != 0) {
        redOn = true;
      } else {
        active = false;
      }
    }
    // Rate-limit the effect of open event_ack writes without dropping the
    // latest status: at most one LED pattern starts in each 800 ms window.
    if (!active && queuedPattern != 0
        && (!hasStartedPattern
            || (uint32_t)(now - lastPatternAtMs) >= MIN_PATTERN_INTERVAL_MS)) {
      const uint8_t next = queuedPattern;
      queuedPattern = 0;
      start(next, now);
    }
    if (active) return {redOn, connected, false};
    if (connected) {
      blueOn = false;
      lastBlinkMs = now;
      return {false, true, false};
    }
    if ((uint32_t)(now - lastBlinkMs) >= 400) {
      lastBlinkMs = now;
      blueOn = !blueOn;
    }
    return {false, false, blueOn};
  }

 private:
  void start(uint8_t flashes, uint32_t now) {
    flashesLeft = flashes;
    edgeAtMs = now;
    lastPatternAtMs = now;
    hasStartedPattern = true;
    redOn = true;
    active = true;
  }
};

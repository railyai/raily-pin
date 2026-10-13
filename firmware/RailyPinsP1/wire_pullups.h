#pragma once

// The hardware Wire bus (D4 SDA, D5 SCL) before anyone starts it: the
// core's Wire busy-waits, so a bus with no pull-ups could hang the loop
// task. Used by the OLED probe (oled_render.cpp) and the DA7280 probe on
// Wire (the sketch's HapticWire, Keyring Air). Arduino only.

#include <Arduino.h>

static inline void releaseWirePins() {
  nrf_gpio_cfg_default(g_ADigitalPinMap[PIN_WIRE_SDA]);
  nrf_gpio_cfg_default(g_ADigitalPinMap[PIN_WIRE_SCL]);
}

// The Expansion Board and the Air carrier pull SDA and SCL up; a bare XIAO
// leaves them floating. Discharge both lines through the internal
// pull-downs, let go, and read: external pull-ups bring them high within
// microseconds, a floating line stays low. (Reading with the ~13 kΩ
// pull-downs still on would form a divider with a 10 kΩ pull-up that can
// read low.) Never call it while Wire runs: it takes the pins over.
static inline bool wireBusPulledUp() {
  pinMode(PIN_WIRE_SDA, INPUT_PULLDOWN);
  pinMode(PIN_WIRE_SCL, INPUT_PULLDOWN);
  delayMicroseconds(100);
  pinMode(PIN_WIRE_SDA, INPUT);
  pinMode(PIN_WIRE_SCL, INPUT);
  delayMicroseconds(50);
  bool high = digitalRead(PIN_WIRE_SDA) == HIGH && digitalRead(PIN_WIRE_SCL) == HIGH;
  releaseWirePins();
  return high;
}

#pragma once

#include <stdint.h>

// A bit-banged I2C master for the DA7280 haptic driver on D6/D7
// (docs/pins/firmware.md «Vibration motor»). Both hardware TWIMs are taken
// (Wire = OLED 0x3C + RTC 0x51, Wire1 = the Sense IMU), so this bus is
// software. Pure: the pin access is a template parameter. The sketch gives
// it the nRF GPIO in S0D1 mode (a line is only ever pulled low or released,
// the hub's and the DA7280 board's pull-ups raise it; never driven high);
// tests/test_da7280.cpp gives it a fake slave on two wires.
//
// Pins must provide:
//   void sda(bool release);  void scl(bool release);  // false: pull low
//   bool readSda();          bool readScl();           // the line level
//   void halfPeriod();                                 // 5 us: ~100 kHz
//
// 7-bit addresses, one-byte register reads and writes. Every wait is
// bounded: a slave may stretch the clock for SOFT_I2C_STRETCH_LIMIT half
// periods (1 ms at 5 us), then the transfer fails. So a missing, unplugged
// or stuck device costs a transfer a few milliseconds at most and never
// hangs loop() or the radio (the SoftDevice preempts the bit-bang as it
// likes; I2C has no minimum clock rate).

// The core builds with loop unrolling: inlined, each register access grew
// by kilobytes. One copy of each step is plenty at 100 kHz.
#define SOFT_I2C_NOINLINE __attribute__((noinline))

static const uint16_t SOFT_I2C_STRETCH_LIMIT = 200;
// A slave cut off mid-byte may hold SDA low: up to 9 clocks free it.
static const uint8_t SOFT_I2C_RECOVERY_CLOCKS = 9;

template <class Pins>
class SoftI2c {
 public:
  explicit SoftI2c(Pins& pins) : pins_(pins) {}

  SOFT_I2C_NOINLINE bool writeRegister(uint8_t address, uint8_t reg, uint8_t value) {
    if (!idle()) return false;
    start();
    const bool ok = writeByte((uint8_t)(address << 1)) && writeByte(reg) && writeByte(value);
    const bool stopped = stop();
    return ok && stopped;
  }

  // Write the register pointer, repeated START, read one byte, NACK, STOP.
  SOFT_I2C_NOINLINE bool readRegister(uint8_t address, uint8_t reg, uint8_t* value) {
    if (!idle()) return false;
    start();
    bool ok = writeByte((uint8_t)(address << 1)) && writeByte(reg);
    if (ok) ok = repeatedStart() && writeByte((uint8_t)((address << 1) | 1));
    uint8_t byte = 0;
    if (ok) ok = readByte(&byte);
    const bool stopped = stop();
    if (ok && stopped) *value = byte;
    return ok && stopped;
  }

  // Both lines released and high. A slave holding SDA low is clocked free
  // and the bus closed with a STOP; false when it stays low or SCL never
  // rises (no pull-ups, nothing connected, a short).
  SOFT_I2C_NOINLINE bool idle() {
    pins_.sda(true);
    if (!releaseScl()) return false;
    pins_.halfPeriod();
    if (pins_.readSda()) return true;
    for (uint8_t i = 0; i < SOFT_I2C_RECOVERY_CLOCKS && !pins_.readSda(); i++) {
      pins_.scl(false);
      pins_.halfPeriod();
      if (!releaseScl()) return false;
      pins_.halfPeriod();
    }
    if (!pins_.readSda()) return false;
    return stop() && pins_.readSda();
  }

 private:
  Pins& pins_;

  SOFT_I2C_NOINLINE bool releaseScl() {
    pins_.scl(true);
    for (uint16_t i = 0; i < SOFT_I2C_STRETCH_LIMIT; i++) {
      if (pins_.readScl()) return true;
      pins_.halfPeriod();
    }
    return pins_.readScl();
  }

  // From idle (both high): SDA falls while SCL is high.
  void start() {
    pins_.sda(false);
    pins_.halfPeriod();
    pins_.scl(false);
    pins_.halfPeriod();
  }

  // From SCL low after an ACK.
  bool repeatedStart() {
    pins_.sda(true);
    pins_.halfPeriod();
    if (!releaseScl()) return false;
    pins_.halfPeriod();
    pins_.sda(false);
    pins_.halfPeriod();
    pins_.scl(false);
    pins_.halfPeriod();
    return true;
  }

  // SDA rises while SCL is high. Safe from any state: SCL goes low first.
  SOFT_I2C_NOINLINE bool stop() {
    pins_.scl(false);
    pins_.sda(false);
    pins_.halfPeriod();
    const bool clock = releaseScl();
    pins_.halfPeriod();
    pins_.sda(true);
    pins_.halfPeriod();
    return clock && pins_.readSda();
  }

  // MSB first; true when the slave pulled SDA low on the ninth clock.
  SOFT_I2C_NOINLINE bool writeByte(uint8_t byte) {
    for (uint8_t bit = 0; bit < 8; bit++) {
      pins_.sda((byte & 0x80) != 0);
      pins_.halfPeriod();
      if (!releaseScl()) return false;
      pins_.halfPeriod();
      pins_.scl(false);
      byte = (uint8_t)(byte << 1);
    }
    pins_.sda(true);
    pins_.halfPeriod();
    if (!releaseScl()) return false;
    const bool ack = !pins_.readSda();
    pins_.halfPeriod();
    pins_.scl(false);
    return ack;
  }

  // One byte, answered with a NACK (the last byte of a read).
  SOFT_I2C_NOINLINE bool readByte(uint8_t* out) {
    uint8_t byte = 0;
    pins_.sda(true);
    for (uint8_t bit = 0; bit < 8; bit++) {
      pins_.halfPeriod();
      if (!releaseScl()) return false;
      byte = (uint8_t)((byte << 1) | (pins_.readSda() ? 1 : 0));
      pins_.halfPeriod();
      pins_.scl(false);
    }
    pins_.sda(true);  // NACK
    pins_.halfPeriod();
    if (!releaseScl()) return false;
    pins_.halfPeriod();
    pins_.scl(false);
    *out = byte;
    return true;
  }
};

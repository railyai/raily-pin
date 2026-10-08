#pragma once

#include <stdint.h>

#include "soft_i2c.h"

// The vibration motor: SparkFun Qwiic Haptic Driver DA7280 (ROB-17590) on
// software I2C, D6/D7, address 0x4A (docs/pins/firmware.md «Vibration
// motor», hardware/raily-pin-public/case/motor-options.md «Update
// 2026-09-30»). When it does not answer at boot, the motor is the old Grove
// module on D0, a digital level, so old builds keep vibrating. Pure: host
// tested in tests/test_da7280.cpp against a fake slave on two wires.
//
// Only the actuator changes: rhythm.h still decides when the motor is on;
// this file turns each on/off edge into DA7280 register writes.

namespace da7280 {

// Register map from the DA7280 datasheet (Renesas, formerly Dialog), as
// the SparkFun library (SparkFun_Qwiic_Haptic_Driver_DA7280_Arduino_Library,
// src/Haptic_Driver.h) and the Linux driver (drivers/input/misc/da7280.c)
// use it. The same values as the Keyring Air factory test
// (RailyAirFactoryTest/factory_checks.h); tests/test_da7280.cpp checks that
// both headers agree.
static const uint8_t ADDRESS = 0x4A;
static const uint8_t REG_CHIP_REV = 0x00;
static const uint8_t CHIP_REV = 0xBA;  // the CHIP_REV reset value: the chip's id
static const uint8_t REG_IRQ_EVENT1 = 0x03;
static const uint8_t REG_FRQ_LRA_PER_H = 0x0A;  // LRA period bits 14:7
static const uint8_t REG_FRQ_LRA_PER_L = 0x0B;  // LRA period bits 6:0 (bit 7 kept)
static const uint8_t REG_ACTUATOR1 = 0x0C;      // nominal max voltage, 23.4 mV a step
static const uint8_t REG_ACTUATOR2 = 0x0D;      // absolute max voltage, 23.4 mV a step
static const uint8_t REG_ACTUATOR3 = 0x0E;      // bits 4:0 max current, 28.6 mA + 7.2 mA a step
static const uint8_t REG_CALIB_V2I_H = 0x0F;
static const uint8_t REG_CALIB_V2I_L = 0x10;
static const uint8_t REG_TOP_CFG1 = 0x13;
static const uint8_t REG_TOP_CTL1 = 0x22;  // bits 2:0 operation mode
static const uint8_t REG_TOP_CTL2 = 0x23;  // the DRO amplitude (override value)

static const uint8_t CFG1_ERM = 0x20;           // bit 5 actuator type: 0 = LRA
static const uint8_t CFG1_ACCELERATION = 0x04;  // bit 2, on at reset; off: the override spans 0..0xFF
static const uint8_t ACTUATOR3_IMAX_MASK = 0x1F;
static const uint8_t LRA_PER_L_MASK = 0x7F;
static const uint8_t CTL1_MODE_MASK = 0x07;
static const uint8_t MODE_INACTIVE = 0;  // no drive; the chip idles
static const uint8_t MODE_DRO = 1;       // direct register override: TOP_CTL2 is the drive

// The LRA SparkFun ships on ROB-17590 (Ø10 mm). Values from the SparkFun
// library's Haptic_Driver::defaultMotor(): nomVolt 2.106 V, absVolt 2.26 V,
// currMax 165.4 mA, impedance 13.8 Ω, lraFreq 170 Hz. The same values as
// the factory test. UNVERIFIED against the LRA's own datasheet (SparkFun
// does not name the part): the bench checks the feel on a spare board.
static const uint32_t LRA_NOMINAL_MV = 2106;
static const uint32_t LRA_ABSOLUTE_MV = 2260;
static const uint32_t LRA_MAX_CURRENT_UA = 165400;
static const uint32_t LRA_IMPEDANCE_MOHM = 13800;
static const uint32_t LRA_FREQUENCY_HZ = 170;

// Datasheet formulas, integer: the SparkFun library computes the same in
// floats but splits the V2I low byte and the LRA period low byte wrongly
// (setActuatorImpedance, setActuatorLRAfreq), so they are done here.
static inline uint8_t voltageStep(uint32_t mv) {
  const uint32_t step = mv * 10 / 234;  // 23.4 mV a step
  return (uint8_t)(step > 0xFF ? 0xFF : step);
}
static inline uint8_t currentStep(uint32_t ua) {
  if (ua <= 28600) return 0;
  const uint32_t step = (ua - 28600) / 7200;
  return (uint8_t)(step > 0x1F ? 0x1F : step);
}
// V2I factor = Z * (IMAX + 4) / 1.6104.
static inline uint16_t v2iFactor(uint32_t impedanceMilliohm, uint8_t imaxStep) {
  return (uint16_t)((uint64_t)impedanceMilliohm * (imaxStep + 4u) * 10u / 16104u);
}
// LRA period = 1 / (f * 1333.32 ns), rounded.
static inline uint16_t lraPeriod(uint32_t hz) {
  return (uint16_t)((100000000000ull / 133332u + hz / 2) / hz);
}

}  // namespace da7280

// The drive while the motor is on. With acceleration off, TOP_CTL2 is a
// signed proportion of ACTUATOR_ABSMAX: 0x01..0x7F is +1..+100%, while
// 0x80..0xFF is a negative (180-degree phase) drive. 0x7F is the absolute
// actuator maximum; about 0x76 matches the 2.106 V nominal setting. The
// factory test uses 0x60, which is the safe starting value for the bench.
#ifndef RAILY_HAPTIC_AMPLITUDE
#define RAILY_HAPTIC_AMPLITUDE 0x60
#endif
static_assert(RAILY_HAPTIC_AMPLITUDE >= 0x01 && RAILY_HAPTIC_AMPLITUDE <= 0x7F,
              "DA7280 OVERRIDE_VAL is signed with acceleration off: use 0x01..0x7F");
static const uint8_t HAPTIC_AMPLITUDE = RAILY_HAPTIC_AMPLITUDE;

// A failed off write can leave the driver vibrating, so it never receives
// the long backoff used for repeated on-write failures.
static inline uint32_t hapticRetryDelayMs(bool desiredOn, uint8_t failureStreak,
                                           uint32_t fastMs, uint32_t slowMs,
                                           uint8_t fastFailureCount) {
  return !desiredOn || failureStreak < fastFailureCount ? fastMs : slowMs;
}

static inline bool hapticOffSupersedesRetry(bool retryPending, bool retryDesiredOn,
                                             bool desiredOn) {
  return retryPending && retryDesiredOn && !desiredOn;
}

// One DA7280 on a register bus: bool write(reg, value), bool read(reg, &value).
template <class Bus>
class Da7280 {
 public:
  explicit Da7280(Bus& bus) : bus_(bus), ctl1_(0), chipRev_(0), ready_(false) {}

  // CHIP_REV must read 0xBA: an ACK alone proves nothing (a floating SDA
  // can read as one). Then the drive is stopped first (a warm reset may
  // have left it in DRO mid-pulse), the LRA configured, the latched events
  // cleared, and the chip left inactive.
  bool begin() {
    ready_ = false;
    uint8_t rev = 0;
    if (!bus_.read(da7280::REG_CHIP_REV, &rev)) return false;
    chipRev_ = rev;
    if (rev != da7280::CHIP_REV) return false;
    uint8_t ctl1 = 0;
    if (!bus_.read(da7280::REG_TOP_CTL1, &ctl1)) return false;
    ctl1_ = (uint8_t)(ctl1 & ~da7280::CTL1_MODE_MASK);
    const uint8_t imax = da7280::currentStep(da7280::LRA_MAX_CURRENT_UA);
    const uint16_t v2i = da7280::v2iFactor(da7280::LRA_IMPEDANCE_MOHM, imax);
    const uint16_t period = da7280::lraPeriod(da7280::LRA_FREQUENCY_HZ);
    // Both stop writes are safety-critical after a warm reset. Do not let a
    // lost ACK on the amplitude write short-circuit the inactive-mode write.
    const bool stopAmplitude = bus_.write(da7280::REG_TOP_CTL2, 0);
    const bool stopMode = bus_.write(da7280::REG_TOP_CTL1, (uint8_t)(ctl1_ | da7280::MODE_INACTIVE));
    const bool ok = stopAmplitude && stopMode &&
        // LRA, acceleration off: the override then spans 0..0xFF.
        writeBits(da7280::REG_TOP_CFG1, da7280::CFG1_ERM | da7280::CFG1_ACCELERATION, 0) &&
        bus_.write(da7280::REG_ACTUATOR1, da7280::voltageStep(da7280::LRA_NOMINAL_MV)) &&
        bus_.write(da7280::REG_ACTUATOR2, da7280::voltageStep(da7280::LRA_ABSOLUTE_MV)) &&
        writeBits(da7280::REG_ACTUATOR3, da7280::ACTUATOR3_IMAX_MASK, imax) &&
        bus_.write(da7280::REG_CALIB_V2I_H, (uint8_t)(v2i >> 8)) &&
        bus_.write(da7280::REG_CALIB_V2I_L, (uint8_t)(v2i & 0xFF)) &&
        bus_.write(da7280::REG_FRQ_LRA_PER_H, (uint8_t)((period >> 7) & 0xFF)) &&
        writeBits(da7280::REG_FRQ_LRA_PER_L, da7280::LRA_PER_L_MASK, (uint8_t)(period & 0x7F)) &&
        bus_.write(da7280::REG_IRQ_EVENT1, 0xFF);  // clear what power-up latched
    ready_ = ok;
    return ok;
  }

  // On: the amplitude, then DRO. Off: amplitude 0, then inactive, both
  // tried even if the first fails (a zero override alone stops the drive).
  // Between pulses the chip sits inactive; TOP_CTL1's other bits
  // (STANDBY_EN among them) keep the value read at begin().
  bool drive(bool on) {
    if (!ready_) return false;
    if (on) {
      return bus_.write(da7280::REG_TOP_CTL2, HAPTIC_AMPLITUDE) &&
             bus_.write(da7280::REG_TOP_CTL1, (uint8_t)(ctl1_ | da7280::MODE_DRO));
    }
    const bool zero = bus_.write(da7280::REG_TOP_CTL2, 0);
    const bool inactive = bus_.write(da7280::REG_TOP_CTL1, (uint8_t)(ctl1_ | da7280::MODE_INACTIVE));
    return zero && inactive;
  }

  uint8_t chipRev() const { return chipRev_; }
  bool ready() const { return ready_; }

 private:
  Bus& bus_;
  uint8_t ctl1_;
  uint8_t chipRev_;
  bool ready_;

  bool writeBits(uint8_t reg, uint8_t mask, uint8_t bits) {
    uint8_t value = 0;
    if (!bus_.read(reg, &value)) return false;
    return bus_.write(reg, (uint8_t)((value & ~mask) | (bits & mask)));
  }
};

// Where the motor's on/off goes, chosen once at boot.
enum HapticBackend : uint8_t {
  HAPTIC_BACKEND_NONE = 0,    // before begin()
  HAPTIC_BACKEND_GPIO = 1,    // the old Grove module on D0
  HAPTIC_BACKEND_DA7280 = 2,  // the DA7280 on D6/D7
};

// The motor: the DA7280 when it answers at boot, else the GPIO fallback.
//
// Pins: the SoftI2c pins plus void begin(bool swapped) (lines released;
// swapped exchanges SDA and SCL) and void end() (both back to the reset
// default). Gpio: void begin() (the off level) and void drive(bool on).
//
// The wiring is documented as SDA = D6, SCL = D7 (the sketch's
// RAILY_HAPTIC_SDA_PIN). begin() also tries the swapped order once, so a
// reversed Grove-to-Qwiic cable still works and the boot line says so;
// the first order answers on a correct board, and the swapped try only
// ever pulls lines low (open drain), so it cannot fight a driver.
//
// At run time a failed write is not a fallback: drive() returns false and
// the caller retries (a missed «off» would leave the LRA buzzing). D0 is
// never touched once the DA7280 answered.
template <class Pins, class Gpio>
class HapticMotor {
 public:
  HapticMotor(Pins& pins, Gpio& gpio)
      : pins_(pins), gpio_(gpio), i2c_(pins), bus_(i2c_), chip_(bus_),
        backend_(HAPTIC_BACKEND_NONE), swapped_(false), errors_(0) {}

  HapticBackend begin() {
    for (uint8_t attempt = 0; attempt < 2; attempt++) {
      swapped_ = attempt == 1;
      pins_.begin(swapped_);
      if (chip_.begin()) {
        backend_ = HAPTIC_BACKEND_DA7280;
        return backend_;
      }
      pins_.end();
    }
    swapped_ = false;
    gpio_.begin();
    backend_ = HAPTIC_BACKEND_GPIO;
    return backend_;
  }

  // True when the motor took the new state.
  bool drive(bool on) {
    switch (backend_) {
      case HAPTIC_BACKEND_DA7280:
        if (chip_.drive(on)) return true;
        if (errors_ < 0xFFFFFFFFu) errors_++;
        return false;
      case HAPTIC_BACKEND_GPIO:
        gpio_.drive(on);
        return true;
      case HAPTIC_BACKEND_NONE:
        break;
    }
    return false;
  }

  HapticBackend backend() const { return backend_; }
  bool swapped() const { return swapped_; }
  // CHIP_REV as read at boot (0 when nothing answered).
  uint8_t chipRev() const { return chip_.chipRev(); }
  // DA7280 writes that failed at run time, since boot.
  uint32_t errors() const { return errors_; }

 private:
  struct RegisterBus {
    explicit RegisterBus(SoftI2c<Pins>& i2c) : i2c_(i2c) {}
    bool write(uint8_t reg, uint8_t value) { return i2c_.writeRegister(da7280::ADDRESS, reg, value); }
    bool read(uint8_t reg, uint8_t* value) { return i2c_.readRegister(da7280::ADDRESS, reg, value); }
    SoftI2c<Pins>& i2c_;
  };

  Pins& pins_;
  Gpio& gpio_;
  SoftI2c<Pins> i2c_;
  RegisterBus bus_;
  Da7280<RegisterBus> chip_;
  HapticBackend backend_;
  bool swapped_;
  uint32_t errors_;
};

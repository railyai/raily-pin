// Host tests for soft_i2c.h and da7280.h: the DA7280 haptic driver on
// bit-banged I2C (D6/D7) and the D0 fallback. A fake DA7280 sits on two
// open-drain wires and decodes START, address, ACK, data, repeated START
// and STOP bit by bit, so the tests see the exact register writes the pin
// would put on the bus.
#include <stdint.h>
#include <stdio.h>

#include <vector>

#include "../RailyPinsP1/da7280.h"
#include "../RailyAirFactoryTest/factory_checks.h"

static int failures = 0;
static void expect(bool condition, const char* name) {
  printf("%s %s\n", condition ? "PASS" : "FAIL", name);
  if (!condition) failures++;
}

struct Write {
  uint8_t reg;
  uint8_t value;
};

struct StopFailureBus {
  std::vector<Write> writes;
  int writeCalls = 0;

  bool read(uint8_t reg, uint8_t* value) {
    *value = reg == da7280::REG_CHIP_REV ? da7280::CHIP_REV : 0;
    return true;
  }

  bool write(uint8_t reg, uint8_t value) {
    writes.push_back({reg, value});
    return writeCalls++ != 0;
  }
};

// Two physical lines (D6 = 0, D7 = 1) with pull-ups, a master and a slave
// that can each only pull a line low.
struct Wires {
  bool pulledUp = true;
  bool masterRelease[2] = {true, true};
  bool slaveLow[2] = {false, false};
  bool level(int line) const { return pulledUp && masterRelease[line] && !slaveLow[line]; }
};

// The DA7280 as I2C sees it: a 256-byte register file behind 0x4A.
struct FakeDa7280 {
  Wires* wires = nullptr;
  int sdaLine = 0;  // the board's wiring: SDA on D6 (0) or D7 (1)
  bool present = true;
  bool holdScl = false;  // a stuck clock line
  bool holdSda = false;  // a stuck data line
  uint8_t address = 0x4A;
  uint8_t regs[256] = {};
  std::vector<Write> writes;

  enum Phase { IDLE, RX, ACK_OUT, TX, ACK_IN };
  enum Role { ADDR, REG, DATA };
  Phase phase = IDLE;
  Role role = ADDR;
  uint8_t shift = 0;
  uint8_t bits = 0;
  bool reading = false;
  bool masterAck = false;
  uint8_t pointer = 0;
  uint8_t tx = 0;
  bool prevScl = true;
  bool prevSda = true;

  int sclLine() const { return 1 - sdaLine; }
  bool scl() const { return wires->level(sclLine()); }
  bool sda() const { return wires->level(sdaLine); }
  void driveSda(bool low) { wires->slaveLow[sdaLine] = low || holdSda; }

  void apply() {
    wires->slaveLow[sclLine()] = holdScl;
    if (holdSda) wires->slaveLow[sdaLine] = true;
  }

  // Called after every master pin change.
  void step() {
    apply();
    const bool c = scl();
    const bool d = sda();
    if (!present) {
      prevScl = c;
      prevSda = d;
      return;
    }
    if (c && prevScl && prevSda && !d) {  // START or repeated START
      phase = RX;
      role = ADDR;
      bits = 0;
      shift = 0;
      driveSda(false);
    } else if (c && prevScl && !prevSda && d) {  // STOP
      phase = IDLE;
      driveSda(false);
    } else if (c && !prevScl) {  // SCL rises: sample
      if (phase == RX) {
        shift = (uint8_t)((shift << 1) | (d ? 1 : 0));
        bits++;
      } else if (phase == ACK_IN) {
        masterAck = !d;
      }
    } else if (!c && prevScl) {  // SCL falls: the slave's turn to move SDA
      fall();
    }
    prevScl = scl();
    prevSda = sda();
  }

  void fall() {
    switch (phase) {
      case RX:
        if (bits < 8) return;
        bits = 0;
        if (role == ADDR) {
          if ((shift >> 1) != address) {
            phase = IDLE;
            return;
          }
          reading = (shift & 1) != 0;
        } else if (role == REG) {
          pointer = shift;
        } else {
          regs[pointer] = shift;
          writes.push_back(Write{pointer, shift});
          pointer++;
        }
        driveSda(true);  // ACK on the ninth clock
        phase = ACK_OUT;
        return;
      case ACK_OUT:
        driveSda(false);
        if (role == ADDR && reading) {
          tx = regs[pointer];
          bits = 0;
          phase = TX;
          driveSda((tx & 0x80) == 0);
        } else {
          role = role == ADDR ? REG : DATA;
          phase = RX;
          shift = 0;
        }
        return;
      case TX:
        bits++;
        if (bits < 8) {
          driveSda((tx & (0x80 >> bits)) == 0);
        } else {
          driveSda(false);
          phase = ACK_IN;
        }
        return;
      case ACK_IN:
        if (masterAck) {
          pointer++;
          tx = regs[pointer];
          bits = 0;
          phase = TX;
          driveSda((tx & 0x80) == 0);
        } else {
          phase = IDLE;
        }
        return;
      case IDLE:
        return;
    }
  }
};

// The sketch's pins as the driver sees them, on the fake wires.
struct FakePins {
  Wires* wires = nullptr;
  FakeDa7280* slave = nullptr;
  bool swapped = false;
  bool open = false;
  uint32_t halfPeriods = 0;
  int ends = 0;

  int sdaLine() const { return swapped ? 1 : 0; }
  int sclLine() const { return swapped ? 0 : 1; }
  void set(int line, bool release) {
    wires->masterRelease[line] = release;
    if (slave) slave->step();
  }
  void begin(bool swap) {
    swapped = swap;
    open = true;
    set(0, true);
    set(1, true);
  }
  void end() {
    open = false;
    ends++;
    set(0, true);
    set(1, true);
  }
  void sda(bool release) { set(sdaLine(), release); }
  void scl(bool release) { set(sclLine(), release); }
  bool readSda() {
    if (slave) slave->apply();
    return wires->level(sdaLine());
  }
  bool readScl() {
    if (slave) slave->apply();
    return wires->level(sclLine());
  }
  void halfPeriod() { halfPeriods++; }
};

struct FakeGpio {
  bool begun = false;
  bool on = false;
  int drives = 0;
  void begin() {
    begun = true;
    on = false;
  }
  void drive(bool value) {
    on = value;
    drives++;
  }
};

struct Rig {
  Wires wires;
  FakeDa7280 chip;
  FakePins pins;
  FakeGpio gpio;
  HapticMotor<FakePins, FakeGpio> motor;

  explicit Rig(int sdaLine = 0) : motor(pins, gpio) {
    chip.wires = &wires;
    chip.sdaLine = sdaLine;
    pins.wires = &wires;
    pins.slave = &chip;
    // Reset-like values, with bits set that the driver must keep or clear.
    chip.regs[da7280::REG_CHIP_REV] = 0xBA;
    chip.regs[da7280::REG_TOP_CFG1] = 0x36;     // ERM, bit 4, acceleration, rapid stop
    chip.regs[da7280::REG_ACTUATOR3] = 0xE7;    // bits 7:5 kept
    chip.regs[da7280::REG_FRQ_LRA_PER_L] = 0x80;  // bit 7 kept
    chip.regs[da7280::REG_TOP_CTL1] = 0x09;     // STANDBY_EN + left in DRO by a warm reset
    chip.regs[da7280::REG_TOP_CTL2] = 0x40;     // and still driving
  }
};

static bool sameWrites(const std::vector<Write>& got, const Write* want, size_t n) {
  if (got.size() != n) {
    printf("  got %zu writes, want %zu:", got.size(), n);
    for (size_t i = 0; i < got.size(); i++) printf(" %02X=%02X", got[i].reg, got[i].value);
    printf("\n");
    return false;
  }
  for (size_t i = 0; i < n; i++) {
    if (got[i].reg != want[i].reg || got[i].value != want[i].value) {
      printf("  write %zu: got %02X=%02X, want %02X=%02X\n", i, got[i].reg, got[i].value, want[i].reg,
             want[i].value);
      return false;
    }
  }
  return true;
}

// The bytes computed by hand from the SparkFun defaults (not by the helpers
// under test): 2106 mV / 23.4 = 90, 2260 / 23.4 = 96.6 -> 96,
// (165.4 - 28.6) / 7.2 = 19, 13.8 * (19 + 4) / 1.6104 = 197.1 -> 197,
// 1 / (170 Hz * 1333.32 ns) = 4411.8 -> 4412 = 34 * 128 + 60.
static const Write INIT_WRITES[] = {
    {0x23, 0x00},  // TOP_CTL2: the drive stopped first
    {0x22, 0x08},  // TOP_CTL1: inactive, STANDBY_EN kept
    {0x13, 0x12},  // TOP_CFG1: LRA, acceleration off, the rest kept
    {0x0C, 0x5A},  // ACTUATOR1 nominal 2.106 V
    {0x0D, 0x60},  // ACTUATOR2 absolute 2.26 V
    {0x0E, 0xF3},  // ACTUATOR3 max current step 19, bits 7:5 kept
    {0x0F, 0x00},  // CALIB_V2I_H
    {0x10, 0xC5},  // CALIB_V2I_L: 197
    {0x0A, 0x22},  // FRQ_LRA_PER_H: 4412 >> 7
    {0x0B, 0xBC},  // FRQ_LRA_PER_L: 4412 & 0x7F, bit 7 kept
    {0x03, 0xFF},  // IRQ_EVENT1 cleared
};
static const Write ON_WRITES[] = {{0x23, HAPTIC_AMPLITUDE}, {0x22, 0x09}};
static const Write OFF_WRITES[] = {{0x23, 0x00}, {0x22, 0x08}};

static void testInit() {
  Rig rig;
  expect(rig.motor.begin() == HAPTIC_BACKEND_DA7280, "init: the DA7280 answers with 0xBA -> DA7280 backend");
  expect(!rig.motor.swapped(), "init: found in the documented order (SDA = D6, SCL = D7)");
  expect(rig.motor.chipRev() == 0xBA, "init: chip id 0xBA");
  expect(sameWrites(rig.chip.writes, INIT_WRITES, sizeof(INIT_WRITES) / sizeof(INIT_WRITES[0])),
         "init: the exact register writes (stop, LRA config, clear events, inactive)");
  expect(rig.chip.regs[da7280::REG_TOP_CTL2] == 0 && (rig.chip.regs[da7280::REG_TOP_CTL1] & 7) == 0,
         "init: the chip ends inactive with no drive (power: air-power-risks P3)");
  expect(!rig.gpio.begun && rig.gpio.drives == 0, "init: D0 is never touched when the DA7280 answered");
  expect(rig.pins.open && rig.pins.ends == 0, "init: the bus stays configured");
  expect(rig.chip.phase == FakeDa7280::IDLE, "init: every transfer ends with a STOP");
}

static void testPulse() {
  Rig rig;
  rig.motor.begin();
  rig.chip.writes.clear();
  expect(rig.motor.drive(true), "pulse on: accepted");
  expect(sameWrites(rig.chip.writes, ON_WRITES, 2), "pulse on: amplitude, then DRO");
  rig.chip.writes.clear();
  expect(rig.motor.drive(false), "pulse off: accepted");
  expect(sameWrites(rig.chip.writes, OFF_WRITES, 2), "pulse off: amplitude 0, then inactive (standby between pulses)");
  expect(rig.motor.errors() == 0, "pulse: no errors");
}

static void testAbsent() {
  Rig rig;
  rig.chip.present = false;  // pull-ups there, nobody answers 0x4A
  expect(rig.motor.begin() == HAPTIC_BACKEND_GPIO, "absent: NACK -> GPIO fallback");
  expect(rig.gpio.begun, "absent: D0 set to the off level");
  expect(rig.pins.ends == 2, "absent: both orders tried, D6/D7 handed back each time");
  expect(rig.motor.drive(true) && rig.gpio.on, "absent: on drives D0");
  expect(rig.motor.drive(false) && !rig.gpio.on, "absent: off releases D0");
  expect(rig.chip.writes.empty(), "absent: nothing written");
}

static void testFloating() {
  Rig rig;
  rig.chip.present = false;
  rig.wires.pulledUp = false;  // an old Grove build: nothing on D6/D7
  expect(rig.motor.begin() == HAPTIC_BACKEND_GPIO, "floating D6/D7: GPIO fallback");
  expect(rig.pins.halfPeriods < 1000, "floating D6/D7: gives up within 1000 half periods (5 ms)");
}

static void testWrongChip() {
  Rig rig;
  rig.chip.regs[da7280::REG_CHIP_REV] = 0x00;  // something else ACKs 0x4A
  expect(rig.motor.begin() == HAPTIC_BACKEND_GPIO, "wrong chip id: an ACK alone is not a DA7280 -> GPIO");
  expect(rig.chip.writes.empty(), "wrong chip id: nothing written to it");
  expect(rig.motor.chipRev() == 0x00, "wrong chip id: the id read is kept for the boot line");
}

static void testSwapped() {
  Rig rig(1);  // the board has SDA on D7 and SCL on D6
  expect(rig.motor.begin() == HAPTIC_BACKEND_DA7280, "swapped cable: found on the second try");
  expect(rig.motor.swapped(), "swapped cable: reported");
  expect(rig.pins.ends == 1, "swapped cable: the first order handed back once");
  expect(sameWrites(rig.chip.writes, INIT_WRITES, sizeof(INIT_WRITES) / sizeof(INIT_WRITES[0])),
         "swapped cable: the same init writes");
  rig.chip.writes.clear();
  expect(rig.motor.drive(true) && sameWrites(rig.chip.writes, ON_WRITES, 2), "swapped cable: pulses work");
}

static void testStuckScl() {
  Rig rig;
  rig.chip.holdScl = true;
  expect(rig.motor.begin() == HAPTIC_BACKEND_GPIO, "stuck SCL: GPIO fallback");
  expect(rig.pins.halfPeriods < 2 * (SOFT_I2C_STRETCH_LIMIT + 50), "stuck SCL: bounded wait (no hang)");
}

static void testStuckSda() {
  Rig rig;
  rig.chip.holdSda = true;
  expect(rig.motor.begin() == HAPTIC_BACKEND_GPIO, "stuck SDA: recovery fails -> GPIO fallback");
  // The documented order fails after 9 recovery clocks; the swapped one
  // sees the stuck line as SCL and waits out one stretch limit.
  expect(rig.pins.halfPeriods < SOFT_I2C_STRETCH_LIMIT + 50, "stuck SDA: 9 recovery clocks, then gives up");
}

static void testRecovery() {
  Rig rig;
  rig.motor.begin();
  // A transfer cut off mid-read: the slave holds SDA low for a 0 bit.
  rig.chip.phase = FakeDa7280::TX;
  rig.chip.tx = 0x00;
  rig.chip.bits = 0;
  rig.wires.slaveLow[rig.chip.sdaLine] = true;
  rig.chip.prevSda = false;  // it pulled SDA low itself, while SCL was low
  rig.chip.writes.clear();
  expect(rig.motor.drive(true), "recovery: a slave holding SDA is clocked free, the write goes through");
  expect(sameWrites(rig.chip.writes, ON_WRITES, 2), "recovery: the pulse writes land");
}

static void testRuntimeNack() {
  Rig rig;
  rig.motor.begin();
  rig.chip.present = false;  // unplugged after boot
  expect(!rig.motor.drive(true), "unplugged: on is refused, the caller retries");
  expect(!rig.motor.drive(false), "unplugged: off is refused, the caller retries");
  expect(rig.motor.errors() == 2, "unplugged: errors counted");
  expect(rig.motor.backend() == HAPTIC_BACKEND_DA7280 && !rig.gpio.begun,
         "unplugged: no switch to D0 at run time");
  rig.chip.present = true;  // back
  rig.chip.writes.clear();
  expect(rig.motor.drive(false) && sameWrites(rig.chip.writes, OFF_WRITES, 2), "replugged: the off retry lands");
}

static void testNotBegun() {
  Rig rig;
  expect(!rig.motor.drive(true) && rig.chip.writes.empty() && rig.gpio.drives == 0,
         "before begin(): nothing is driven");
}

static void testRetryDelay() {
  expect(hapticRetryDelayMs(true, 9, 20, 1000, 10) == 20,
         "retry: on failures use the fast interval before the threshold");
  expect(hapticRetryDelayMs(true, 10, 20, 1000, 10) == 1000,
         "retry: repeated on failures use the slow interval");
  expect(hapticRetryDelayMs(false, 255, 20, 1000, 10) == 20,
         "retry: off failures always use the fast interval");
  expect(hapticOffSupersedesRetry(true, true, false),
         "retry: desired off supersedes a pending failed on write");
  expect(!hapticOffSupersedesRetry(true, false, false) &&
             !hapticOffSupersedesRetry(false, true, false),
         "retry: an off retry keeps its deadline and a clean state does not supersede");
}

static void testStopWritesDoNotShortCircuit() {
  StopFailureBus bus;
  Da7280<StopFailureBus> chip(bus);
  expect(!chip.begin(), "init: a failed stop write fails initialization");
  expect(bus.writes.size() == 2 && bus.writes[0].reg == da7280::REG_TOP_CTL2 &&
             bus.writes[1].reg == da7280::REG_TOP_CTL1 && bus.writes[1].value == da7280::MODE_INACTIVE,
         "init: inactive mode is attempted after a failed amplitude stop");
}

static void testFactoryAgrees() {
  expect(HAPTIC_AMPLITUDE == 0x60 && HAPTIC_AMPLITUDE <= 0x7F,
         "factory: safe positive default amplitude");
  // The pin and the Air factory test must program the same LRA.
  expect(da7280::ADDRESS == DA7280_ADDRESS && da7280::CHIP_REV == DA7280_CHIP_REV, "factory: address and id");
  expect(da7280::REG_TOP_CTL1 == DA7280_TOP_CTL1 && da7280::REG_TOP_CTL2 == DA7280_TOP_CTL2 &&
             da7280::REG_TOP_CFG1 == DA7280_TOP_CFG1 && da7280::REG_ACTUATOR1 == DA7280_ACTUATOR1 &&
             da7280::REG_ACTUATOR2 == DA7280_ACTUATOR2 && da7280::REG_ACTUATOR3 == DA7280_ACTUATOR3 &&
             da7280::REG_CALIB_V2I_H == DA7280_CALIB_V2I_H && da7280::REG_CALIB_V2I_L == DA7280_CALIB_V2I_L &&
             da7280::REG_FRQ_LRA_PER_H == DA7280_FRQ_LRA_PER_H && da7280::REG_FRQ_LRA_PER_L == DA7280_FRQ_LRA_PER_L &&
             da7280::REG_IRQ_EVENT1 == DA7280_IRQ_EVENT1,
         "factory: register map");
  expect(da7280::LRA_NOMINAL_MV == LRA_NOMINAL_MV && da7280::LRA_ABSOLUTE_MV == LRA_ABSOLUTE_MV &&
             da7280::LRA_MAX_CURRENT_UA == LRA_MAX_CURRENT_UA && da7280::LRA_IMPEDANCE_MOHM == LRA_IMPEDANCE_MOHM &&
             da7280::LRA_FREQUENCY_HZ == LRA_FREQUENCY_HZ,
         "factory: the same LRA");
  const uint8_t imax = da7280::currentStep(da7280::LRA_MAX_CURRENT_UA);
  const LraPeriod period = da7280LraPeriod(LRA_FREQUENCY_HZ);
  expect(da7280::voltageStep(2106) == da7280VoltageStep(2106) && da7280::voltageStep(2260) == da7280VoltageStep(2260) &&
             imax == da7280CurrentStep(LRA_MAX_CURRENT_UA) &&
             da7280::v2iFactor(13800, imax) == da7280V2iFactor(13800, imax) &&
             ((da7280::lraPeriod(170) >> 7) & 0xFF) == period.high && (da7280::lraPeriod(170) & 0x7F) == period.low,
         "factory: the same register values");
  expect(da7280::lraPeriod(170) == 4412 && da7280::v2iFactor(13800, 19) == 197 && imax == 19,
         "factory: hand-computed period 4412, V2I 197, current step 19");
}

int main() {
  testInit();
  testPulse();
  testAbsent();
  testFloating();
  testWrongChip();
  testSwapped();
  testStuckScl();
  testStuckSda();
  testRecovery();
  testRuntimeNack();
  testNotBegun();
  testRetryDelay();
  testStopWritesDoNotShortCircuit();
  testFactoryAgrees();
  if (failures) {
    printf("da7280: %d failure(s)\n", failures);
    return 1;
  }
  printf("da7280: ok\n");
  return 0;
}

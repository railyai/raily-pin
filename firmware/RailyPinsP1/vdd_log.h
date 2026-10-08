#ifndef RAILY_VDD_LOG_H
#define RAILY_VDD_LOG_H

// The VDD discharge bench (docs/pins/battery-telemetry-design.md «Expansion
// Board kit»), the pure part: the internal VDD reading, the 12-byte log
// record, the persistent bench settings, the two-segment ring, the space
// guard and the serial commands. No Arduino dependency, so
// tests/test_vdd_log.cpp runs the same code on the host. The sketch owns
// the ADC, the files and the LEDs.
//
// Why: on the Expansion Board kit the cell feeds only the board's ETA6003
// charger and ETA3410 buck (SYS_3V3 = the XIAO's 3V3 pin); no battery node
// reaches a XIAO pin, so P0.31 measures nothing. When the cell drops below
// ~3.4 V the buck runs out of headroom and SYS_3V3, the nRF's own VDD,
// sags. The nRF reads its VDD on an internal SAADC channel, no wire.

#include <stddef.h>
#include <stdint.h>

// VDD on the SAADC's internal VDD input, 12-bit, internal 0.6 V reference
// with gain 1/6 (AR_INTERNAL): full scale 3600 mV, enough for a 3.3 V rail.
static const uint32_t VDD_ADC_FULL_SCALE_MV = 3600;
static const uint32_t VDD_ADC_COUNTS = 4096;

// VDD in millivolts from the sum of `samples` raw 12-bit readings; 0 when
// there are no samples.
static inline uint32_t vddMillivolts(uint32_t rawSum, uint32_t samples) {
  if (samples == 0) return 0;
  const uint64_t numerator = (uint64_t)rawSum * VDD_ADC_FULL_SCALE_MV;
  const uint64_t denominator = (uint64_t)samples * VDD_ADC_COUNTS;
  return (uint32_t)((numerator + denominator / 2) / denominator);
}

// One record a minute while the bench flag is set.
static const uint32_t VDD_LOG_PERIOD_MS = 60000;
// No flash write while VDD reads below this. The core's flash layer erases
// a whole 4 KB page and programs it again for every littlefs write; a
// brown-out between the two loses every littlefs block on that page, other
// files included (the counter, the secret, the bonds). Above the floor the
// rail still has margin; the sag from 3.3 V down to it is the signal.
static const uint32_t VDD_LOG_FLOOR_MV = 3000;
// Two append-only segment files. littlefs v1 rewrites everything after a
// changed block, so a ring that overwrites in place would rewrite the whole
// file each minute; appending to one segment and dropping the older one
// when the current one is full keeps every write an append. 384 records of
// 12 B per segment: 384 to 768 records kept, 6.4 to 12.8 h at 60 s.
static const uint32_t VDD_LOG_SEGMENT_RECORDS = 384;
// InternalFS: 224 blocks of 128 B (28 KB) shared with the counter, the
// secret, the mascot and the BLE bonds. An append needs this many blocks
// free; below it the older segment goes, and with no older segment the
// record is skipped.
static const uint32_t VDD_LOG_FS_BLOCKS = 224;
static const uint32_t VDD_LOG_RESERVE_BLOCKS = 48;

// Record flags. The high nibble is a fixed marker: erased flash (0xFF) or
// garbage does not decode as a record.
static const uint8_t VDD_FLAG_USB = 0x01;
static const uint8_t VDD_FLAG_BOOT = 0x02;  // the first record after a boot
static const uint8_t VDD_FLAG_LOAD = 0x04;  // the LED load was on
static const uint8_t VDD_FLAG_START = 0x08;  // the first record after serial V1
static const uint8_t VDD_FLAG_MARK = 0xA0;
static const uint8_t VDD_FLAG_MARK_MASK = 0xF0;

static const size_t VDD_RECORD_LENGTH = 12;
struct VddRecord {
  uint32_t uptimeS;
  uint16_t vddMv;
  uint16_t p031Mv;
  uint16_t boot;   // the boot number this record was taken in
  uint8_t flags;   // VDD_FLAG_*, without the marker nibble
  uint8_t reset;   // vddPackResetReason() of that boot (0 = power-on or brown-out)
};

// RESETREAS in one byte: RESETPIN, DOG, SREQ, LOCKUP in bits 0-3, OFF,
// LPCOMP, DIF, NFC (bits 16-19) in bits 4-7. Power-on and brown-out leave
// every bit 0, so they read the same.
static inline uint8_t vddPackResetReason(uint32_t resetReason) {
  return (uint8_t)((resetReason & 0x0Fu) | ((resetReason >> 12) & 0xF0u));
}

static inline void vddPut16(uint8_t* out, uint16_t value) {
  out[0] = (uint8_t)value;
  out[1] = (uint8_t)(value >> 8);
}
static inline uint16_t vddGet16(const uint8_t* in) { return (uint16_t)(in[0] | (in[1] << 8)); }

// Little-endian: u32 uptime_s, u16 vdd_mV, u16 p031_mV, u16 boot, u8 flags
// (with the marker nibble), u8 reset.
static inline void vddRecordEncode(const VddRecord& rec, uint8_t out[VDD_RECORD_LENGTH]) {
  out[0] = (uint8_t)rec.uptimeS;
  out[1] = (uint8_t)(rec.uptimeS >> 8);
  out[2] = (uint8_t)(rec.uptimeS >> 16);
  out[3] = (uint8_t)(rec.uptimeS >> 24);
  vddPut16(out + 4, rec.vddMv);
  vddPut16(out + 6, rec.p031Mv);
  vddPut16(out + 8, rec.boot);
  out[10] = (uint8_t)((rec.flags & 0x0F) | VDD_FLAG_MARK);
  out[11] = rec.reset;
}

static inline bool vddRecordDecode(const uint8_t in[VDD_RECORD_LENGTH], VddRecord* rec) {
  if ((in[10] & VDD_FLAG_MARK_MASK) != VDD_FLAG_MARK) return false;
  rec->uptimeS = (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
  rec->vddMv = vddGet16(in + 4);
  rec->p031Mv = vddGet16(in + 6);
  rec->boot = vddGet16(in + 8);
  rec->flags = (uint8_t)(in[10] & 0x0F);
  rec->reset = in[11];
  return true;
}

// The persistent bench settings (one small file, rewritten only by a serial
// command or a segment switch, never at boot): logging on, the LED load on,
// which segment is current.
static const size_t VDD_CONFIG_LENGTH = 6;
static const uint8_t VDD_CONFIG_LOG = 0x01;
static const uint8_t VDD_CONFIG_LOAD = 0x02;
struct VddConfig {
  bool log;
  bool load;
  uint8_t active;  // 0 or 1: the segment being appended
};

static inline uint8_t vddConfigCheck(const uint8_t* in) {
  return (uint8_t)(in[0] ^ in[1] ^ in[2] ^ in[3] ^ in[4] ^ 0x5A);
}

static inline void vddConfigEncode(const VddConfig& config, uint8_t out[VDD_CONFIG_LENGTH]) {
  out[0] = 'R';
  out[1] = 'V';
  out[2] = 1;  // version
  out[3] = (uint8_t)((config.log ? VDD_CONFIG_LOG : 0) | (config.load ? VDD_CONFIG_LOAD : 0));
  out[4] = (uint8_t)(config.active & 1);
  out[5] = vddConfigCheck(out);
}

// A missing, short or damaged file decodes as everything off.
static inline VddConfig vddConfigDecode(const uint8_t* in, size_t length) {
  VddConfig config = {false, false, 0};
  if (in == NULL || length != VDD_CONFIG_LENGTH) return config;
  if (in[0] != 'R' || in[1] != 'V' || in[2] != 1 || in[4] > 1 || (in[3] & ~0x03) != 0) return config;
  if (in[5] != vddConfigCheck(in)) return config;
  config.log = (in[3] & VDD_CONFIG_LOG) != 0;
  config.load = (in[3] & VDD_CONFIG_LOAD) != 0;
  config.active = in[4];
  return config;
}

// What an append does about space and the segment length.
enum VddAppendPlan : uint8_t {
  VDD_APPEND = 0,          // append to the current segment
  VDD_SWITCH = 1,          // drop the older segment, make it current, append there
  VDD_SKIP_NO_SPACE = 2,   // too little free space and no older segment to drop
};

// activeRecords: records in the current segment; olderExists: the other
// segment holds data; freeBlocks: InternalFS blocks free now.
static inline VddAppendPlan vddPlanAppend(uint32_t activeRecords, bool olderExists, uint32_t freeBlocks) {
  if (activeRecords >= VDD_LOG_SEGMENT_RECORDS) return VDD_SWITCH;
  if (freeBlocks >= VDD_LOG_RESERVE_BLOCKS) return VDD_APPEND;
  return olderExists ? VDD_SWITCH : VDD_SKIP_NO_SPACE;
}

// Serial commands (115200): `v` dump the log as CSV, `V1` logging on, `V0`
// off, `Vx` erase the log, `L1` LED load on, `L0` off. The sketch feeds this
// parser after bench_serial.h (so the `ov` scene still works). `V` or `L`
// followed by anything else drops the pending letter and that character is
// handled as usual by this parser and then the sketch: `Vx` never reaches
// the sketch's `x` (erase every BLE bond), and `Vp` still presses.
enum VddCommand : uint8_t {
  VDD_CMD_NONE = 0,
  VDD_CMD_DUMP,
  VDD_CMD_LOG_ON,
  VDD_CMD_LOG_OFF,
  VDD_CMD_ERASE,
  VDD_CMD_LOAD_ON,
  VDD_CMD_LOAD_OFF,
};

struct VddSerial {
  char pending;  // 0, 'V' or 'L'

  // Returns whether c was consumed; *command is the action to run.
  bool feed(char c, VddCommand* command) {
    *command = VDD_CMD_NONE;
    const char was = pending;
    pending = 0;
    if (was == 'V') {
      if (c == '1') *command = VDD_CMD_LOG_ON;
      if (c == '0') *command = VDD_CMD_LOG_OFF;
      if (c == 'x' || c == 'X') *command = VDD_CMD_ERASE;
    } else if (was == 'L') {
      if (c == '1') *command = VDD_CMD_LOAD_ON;
      if (c == '0') *command = VDD_CMD_LOAD_OFF;
    }
    if (*command != VDD_CMD_NONE) return true;
    if (c == 'v') {
      *command = VDD_CMD_DUMP;
      return true;
    }
    if (c == 'V' || c == 'L') {
      pending = c;
      return true;
    }
    return false;
  }
};

#endif

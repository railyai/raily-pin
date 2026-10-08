// Host tests for vdd_log.h (the VDD discharge bench, docs/pins/
// battery-telemetry-design.md «Expansion Board kit»): the VDD conversion,
// the record and settings packing, the two-segment plan with its space
// guard, the reset-reason byte and the serial commands (`Vx` must never
// reach the sketch's `x`, which erases every BLE bond).
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../RailyPinsP1/vdd_log.h"

static int failures = 0;
static void expect(bool condition, const char* name) {
  printf("%s %s\n", condition ? "PASS" : "FAIL", name);
  if (!condition) failures++;
}

// Feeds a string; returns the commands and the characters that fell through.
static void feedAll(VddSerial& serial, const char* text, VddCommand* commands, size_t* count, char* passed) {
  *count = 0;
  size_t p = 0;
  for (const char* c = text; *c; c++) {
    VddCommand command;
    if (!serial.feed(*c, &command)) passed[p++] = *c;
    if (command != VDD_CMD_NONE) commands[(*count)++] = command;
  }
  passed[p] = '\0';
}

int main() {
  // VDD: 3.3 V at gain 1/6 is raw 3755.
  const uint32_t raw33 = (uint32_t)(3300.0 / 3600.0 * 4096.0 + 0.5);
  expect(vddMillivolts(raw33 * 8, 8) == 3300, "3300 mV round trip");
  expect(vddMillivolts(4095u * 8, 8) == 3599, "full scale is 3599 mV, no overflow");
  expect(vddMillivolts(0, 8) == 0 && vddMillivolts(5, 0) == 0, "zero and no samples");

  // Record round trip, little-endian layout, marker nibble.
  VddRecord rec = {0x01020304u, 3312, 4130, 7, VDD_FLAG_USB | VDD_FLAG_BOOT, 0x12};
  uint8_t buf[VDD_RECORD_LENGTH];
  vddRecordEncode(rec, buf);
  const uint8_t want[VDD_RECORD_LENGTH] = {0x04, 0x03, 0x02, 0x01, 0xF0, 0x0C, 0x22, 0x10, 0x07, 0x00, 0xA3, 0x12};
  expect(memcmp(buf, want, sizeof(want)) == 0, "record bytes are the documented layout");
  VddRecord back = {};
  expect(vddRecordDecode(buf, &back), "record decodes");
  expect(back.uptimeS == rec.uptimeS && back.vddMv == 3312 && back.p031Mv == 4130 && back.boot == 7 &&
             back.flags == (VDD_FLAG_USB | VDD_FLAG_BOOT) && back.reset == 0x12,
         "every field survives");
  VddRecord high = {1, 1, 1, 1, 0xFF, 0};
  vddRecordEncode(high, buf);
  expect(vddRecordDecode(buf, &back) && back.flags == 0x0F, "flags above bit 3 never reach the marker nibble");
  uint8_t erased[VDD_RECORD_LENGTH];
  memset(erased, 0xFF, sizeof(erased));
  expect(!vddRecordDecode(erased, &back), "erased flash is not a record");
  uint8_t zeros[VDD_RECORD_LENGTH] = {0};
  expect(!vddRecordDecode(zeros, &back), "zeros are not a record");

  // Reset reason byte.
  expect(vddPackResetReason(0) == 0, "power-on / brown-out = 0");
  expect(vddPackResetReason(0x1) == 0x01, "RESETPIN");
  expect(vddPackResetReason(0x4) == 0x04, "SREQ (software reset)");
  expect(vddPackResetReason(0x10000) == 0x10, "OFF (System OFF wake)");
  expect(vddPackResetReason(0x100000) == 0x00, "VBUS (bit 20) does not fit and is dropped");

  // Settings.
  uint8_t cfg[VDD_CONFIG_LENGTH];
  VddConfig config = {true, true, 1};
  vddConfigEncode(config, cfg);
  VddConfig got = vddConfigDecode(cfg, sizeof(cfg));
  expect(got.log && got.load && got.active == 1, "settings round trip");
  config = {true, false, 0};
  vddConfigEncode(config, cfg);
  got = vddConfigDecode(cfg, sizeof(cfg));
  expect(got.log && !got.load && got.active == 0, "log only");
  cfg[3] ^= VDD_CONFIG_LOAD;
  got = vddConfigDecode(cfg, sizeof(cfg));
  expect(!got.log && !got.load, "a flipped bit fails the check: everything off");
  expect(!vddConfigDecode(NULL, 0).log, "no file: off");
  vddConfigEncode(VddConfig{true, false, 0}, cfg);
  expect(!vddConfigDecode(cfg, sizeof(cfg) - 1).log, "short file: off");
  expect(!vddConfigDecode(erased, VDD_CONFIG_LENGTH).log, "erased file: off");

  // Segment plan and space guard.
  expect(vddPlanAppend(0, false, 200) == VDD_APPEND, "empty log appends");
  expect(vddPlanAppend(VDD_LOG_SEGMENT_RECORDS - 1, true, 200) == VDD_APPEND, "one short of full appends");
  expect(vddPlanAppend(VDD_LOG_SEGMENT_RECORDS, true, 200) == VDD_SWITCH, "a full segment switches");
  expect(vddPlanAppend(VDD_LOG_SEGMENT_RECORDS, false, 0) == VDD_SWITCH, "a full segment switches even when tight");
  expect(vddPlanAppend(10, true, VDD_LOG_RESERVE_BLOCKS - 1) == VDD_SWITCH, "low space drops the older segment");
  expect(vddPlanAppend(10, false, VDD_LOG_RESERVE_BLOCKS - 1) == VDD_SKIP_NO_SPACE,
         "low space and no older segment skips");
  expect(vddPlanAppend(10, false, VDD_LOG_RESERVE_BLOCKS) == VDD_APPEND, "exactly the reserve appends");
  // Both segments full stay within the filesystem with the reserve to spare
  // (12 B records, up to ~8 B of skip-list pointers per 128 B block).
  const uint32_t perSegmentBlocks = (VDD_LOG_SEGMENT_RECORDS * VDD_RECORD_LENGTH + 119) / 120 + 1;
  expect(2 * perSegmentBlocks < VDD_LOG_FS_BLOCKS / 2, "two full segments take under half the filesystem");

  // Serial commands.
  VddSerial serial = {};
  VddCommand commands[16];
  size_t count = 0;
  char passed[32];
  feedAll(serial, "v", commands, &count, passed);
  expect(count == 1 && commands[0] == VDD_CMD_DUMP && passed[0] == '\0', "v dumps");
  feedAll(serial, "V1\n", commands, &count, passed);
  expect(count == 1 && commands[0] == VDD_CMD_LOG_ON && strcmp(passed, "\n") == 0, "V1 logs on");
  feedAll(serial, "V0", commands, &count, passed);
  expect(count == 1 && commands[0] == VDD_CMD_LOG_OFF, "V0 logs off");
  feedAll(serial, "Vx", commands, &count, passed);
  expect(count == 1 && commands[0] == VDD_CMD_ERASE && passed[0] == '\0', "Vx erases the log and never passes x on");
  feedAll(serial, "VX", commands, &count, passed);
  expect(count == 1 && commands[0] == VDD_CMD_ERASE && passed[0] == '\0', "VX too");
  feedAll(serial, "L1L0", commands, &count, passed);
  expect(count == 2 && commands[0] == VDD_CMD_LOAD_ON && commands[1] == VDD_CMD_LOAD_OFF, "L1 and L0");
  feedAll(serial, "Vp", commands, &count, passed);
  expect(count == 0 && strcmp(passed, "p") == 0, "V then p: the press still happens");
  feedAll(serial, "L\nx", commands, &count, passed);
  expect(count == 0 && strcmp(passed, "\nx") == 0, "a lone L is dropped at the newline; a later x is the sketch's");
  feedAll(serial, "VV1", commands, &count, passed);
  expect(count == 1 && commands[0] == VDD_CMD_LOG_ON && passed[0] == '\0', "VV1: the second V restarts");
  feedAll(serial, "Vv", commands, &count, passed);
  expect(count == 1 && commands[0] == VDD_CMD_DUMP, "V then v dumps");
  feedAll(serial, "pixw2", commands, &count, passed);
  expect(count == 0 && strcmp(passed, "pixw2") == 0, "other bench letters pass through untouched");
  feedAll(serial, "l1", commands, &count, passed);
  expect(count == 0 && strcmp(passed, "l1") == 0, "lowercase l is not the load command");

  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("all vdd_log tests passed\n");
  return 0;
}

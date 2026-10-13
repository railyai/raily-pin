#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Which bootloader the pin runs, read from its image (0xF4000 on the XIAO
// nRF52840). Pure: host tested in tests/test_bootloader_kind.cpp.
//
// A BLE update parks the pin in its bootloader. Only OTAFIX
// (0.9.2-OTAFIX2.3) finishes one from an iPhone: it advertises `XIAO_DFU`,
// the name the app looks for, and carries the iOS fixes. The stock Seeed
// bootloader («UF2 Bootloader 0.6.1», 2021) advertises `AdaDFU`; the app
// rightly ignores that name (other Adafruit products share it), and a Mac
// upload to it stalled. On the P1 prototype (bench, 2026-10-13) a phone OTA
// therefore left the pin parked in the bootloader: no app, no USB, no
// timeout. So the pin refuses the DFU-entry pass unless its bootloader is
// OTAFIX, and says which one it has (`bl` in device_info and serial `i`).
enum BootloaderKind : uint8_t {
  BOOTLOADER_UNKNOWN = 0,  // not read: no bootloader address, or the region is read-protected
  BOOTLOADER_OTAFIX = 1,   // its image holds the `XIAO_DFU` name
  BOOTLOADER_STOCK = 2,    // read, and no `XIAO_DFU`: stock Seeed/Adafruit
};

static const char BOOTLOADER_OTAFIX_NAME[] = "XIAO_DFU";

// The bootloader's own image, [start, end).
static inline BootloaderKind classifyBootloader(const uint8_t* image, size_t length) {
  if (image == NULL) return BOOTLOADER_UNKNOWN;
  const size_t n = sizeof(BOOTLOADER_OTAFIX_NAME) - 1;
  for (size_t i = 0; i + n <= length; i++) {
    if (memcmp(image + i, BOOTLOADER_OTAFIX_NAME, n) == 0) return BOOTLOADER_OTAFIX;
  }
  return BOOTLOADER_STOCK;
}

// Only OTAFIX takes a BLE update; an unknown one is refused too (a refused
// update is a message, a parked pin needs USB).
static inline bool bootloaderTakesBleUpdate(BootloaderKind kind) { return kind == BOOTLOADER_OTAFIX; }

static inline const char* bootloaderKindName(BootloaderKind kind) {
  switch (kind) {
    case BOOTLOADER_OTAFIX:
      return "otafix";
    case BOOTLOADER_STOCK:
      return "stock";
    default:
      return "unknown";
  }
}

// The bootloader start from UICR.NRFFW[0], when it is a plausible one: inside
// the last 128 KB of the nRF52840's 1 MB flash and 4 KB aligned. 0 otherwise
// (0xFFFFFFFF: none set).
static const uint32_t BOOTLOADER_FLASH_END = 0x100000;
static inline uint32_t bootloaderStart(uint32_t uicrNrffw0) {
  if (uicrNrffw0 < 0xE0000 || uicrNrffw0 >= BOOTLOADER_FLASH_END || (uicrNrffw0 & 0xFFF) != 0) return 0;
  return uicrNrffw0;
}

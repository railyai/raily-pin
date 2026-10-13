// Host tests for bootloader_kind.h: which bootloader takes a BLE update.
#include <cstdio>
#include <cstring>

#include "../RailyPinsP1/bootloader_kind.h"

static int failures = 0;
static void expect(bool condition, const char* name) {
  std::printf("%s %s\n", condition ? "PASS" : "FAIL", name);
  if (!condition) failures++;
}

// A fake bootloader image with `text` at `at`, 0xFF elsewhere (erased flash).
static void image(unsigned char* buf, size_t size, const char* text, size_t at) {
  std::memset(buf, 0xFF, size);
  if (text != NULL) std::memcpy(buf + at, text, std::strlen(text));
}

int main() {
  unsigned char buf[4096];
  // The strings the bench read on the P1 prototype (2026-10-13), and the one
  // in the OTAFIX 0.9.2-OTAFIX2.3 image.
  image(buf, sizeof(buf), "pGAdaDFU\0XIAO nRF52840 Sense\0UF2 Bootloader 0.6.1", 100);
  expect(classifyBootloader(buf, sizeof(buf)) == BOOTLOADER_STOCK, "stock Seeed 0.6.1 (AdaDFU) is stock");
  image(buf, sizeof(buf), "XIAO_DFU", 4000);
  expect(classifyBootloader(buf, sizeof(buf)) == BOOTLOADER_OTAFIX, "an image with XIAO_DFU is OTAFIX");
  image(buf, sizeof(buf), "XIAO_DFU", sizeof(buf) - 8);
  expect(classifyBootloader(buf, sizeof(buf)) == BOOTLOADER_OTAFIX, "the name in the image's last bytes still counts");
  image(buf, sizeof(buf), "XIAO_DF", sizeof(buf) - 7);
  expect(classifyBootloader(buf, sizeof(buf)) == BOOTLOADER_STOCK, "a name cut at the end does not");
  image(buf, sizeof(buf), "XIAO nRF52840 Sense", 10);
  expect(classifyBootloader(buf, sizeof(buf)) == BOOTLOADER_STOCK, "XIAO alone is not the OTAFIX name");
  expect(classifyBootloader(NULL, 0) == BOOTLOADER_UNKNOWN, "no image is unknown");

  expect(bootloaderTakesBleUpdate(BOOTLOADER_OTAFIX), "OTAFIX takes a BLE update");
  expect(!bootloaderTakesBleUpdate(BOOTLOADER_STOCK), "stock does not");
  expect(!bootloaderTakesBleUpdate(BOOTLOADER_UNKNOWN), "unknown does not: a refusal is a message, a park needs USB");
  expect(std::strcmp(bootloaderKindName(BOOTLOADER_OTAFIX), "otafix") == 0 &&
             std::strcmp(bootloaderKindName(BOOTLOADER_STOCK), "stock") == 0 &&
             std::strcmp(bootloaderKindName(BOOTLOADER_UNKNOWN), "unknown") == 0,
         "the names device_info carries");

  expect(bootloaderStart(0x000F4000) == 0xF4000, "the XIAO bootloader address (UICR read on the bench)");
  expect(bootloaderStart(0xFFFFFFFF) == 0, "no bootloader set");
  expect(bootloaderStart(0x00027000) == 0, "an address in the app region is not a bootloader");
  expect(bootloaderStart(0x000F4100) == 0, "an unaligned address is refused");
  expect(bootloaderStart(0x00100000) == 0, "past the end of flash");

  if (failures) {
    std::printf("bootloader_kind: %d failure(s)\n", failures);
    return 1;
  }
  return 0;
}

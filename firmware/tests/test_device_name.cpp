// Host tests for device_name.h: the Bluetooth name by model. run_tests.sh
// builds this file twice, plain (the P1 and DIY images) and with
// -DRAILY_SHIP_MODE=1 (build_firmware.sh --air, the Keyring Air image), and
// each build must carry exactly its own name.
#include <cstdio>
#include <cstring>

#include "../RailyPinsP1/device_info.h"
#include "../RailyPinsP1/device_name.h"

static int failures = 0;

static void expect(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    failures++;
  }
}

// What the sketch builds: the array beside FW_VERSION.
static const char DEVICE_NAME[] = RAILY_DEVICE_NAME;

int main() {
#if RAILY_SHIP_MODE
  const char* build = "Air (RAILY_SHIP_MODE=1)";
  expect(std::strcmp(DEVICE_NAME, "Raily Air") == 0, "the Air image advertises «Raily Air»");
#else
  const char* build = "P1 / DIY (RAILY_SHIP_MODE=0)";
  expect(std::strcmp(DEVICE_NAME, "Raily P1") == 0, "the P1 and DIY images advertise «Raily P1»");
#endif

  // The name follows the very switch device_info.h reports as "air":1, so
  // the Bluetooth entry and the app's isAir agree on every build.
  char info[DEVICE_INFO_MAX];
  const int n = formatDeviceInfo(info, sizeof(info), "rp1-0123456789abcdef", "0.4.3", 0, 1, "otafix",
                                 RAILY_SHIP_MODE != 0);
  expect(n > 0, "device_info formats");
  expect((std::strstr(info, "\"air\":1") != nullptr) == (std::strcmp(DEVICE_NAME, "Raily Air") == 0),
         "the name says Air exactly when device_info says air:1");

  // Only the model, not a retired name, and plain ASCII with no raw ids.
  expect(std::strcmp(DEVICE_NAME, "Raily Device") != 0 && std::strcmp(DEVICE_NAME, "Raily Pin P1") != 0,
         "neither retired name");
  for (const char* c = DEVICE_NAME; *c; c++) {
    expect(*c >= 0x20 && *c < 0x7f && *c != '_', "printable ASCII, no underscore");
  }

  // The scan response holds the name alone: AD header + name of 31 bytes.
  // The advertisement (flags 3 + marker 7 + 128-bit UUID 18 = 28) does not
  // depend on the name; check_device_name.py pins those three against the sketch.
  const unsigned scan = DEVICE_NAME_AD_OVERHEAD + (unsigned)std::strlen(DEVICE_NAME);
  expect(scan <= DEVICE_NAME_PAYLOAD_MAX, "the name fits the 31-byte scan response");
  std::printf("device_name: %s advertises \"%s\", scan response %u of %u bytes\n", build, DEVICE_NAME, scan,
              DEVICE_NAME_PAYLOAD_MAX);

  if (failures) {
    std::printf("device_name: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("device_name: ok\n");
  return 0;
}

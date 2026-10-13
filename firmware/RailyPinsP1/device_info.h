#pragma once

#include <stdint.h>
#include <stdio.h>

// The device_info value (7B1E0002) the phone reads over BLE. Pure: host
// tested in tests/test_device_info.cpp.
//
// `oled` says whether the board found its screen panel: 1 present, 0 none
// (the probe found no panel, or the LED-only build). The panel is probed
// from loop() after advertising starts (the pre-flash gate), so for the
// first ~0.5 s of a boot the answer is not known yet and the key is left
// out: the app then falls back to «takes screen_state»
// (docs/pins/keyring-oled.md §10 «Screen settings»).
static const int8_t DEVICE_INFO_OLED_UNKNOWN = -1;

// `bl` names the bootloader (bootloader_kind.h): "otafix" takes a BLE
// update, "stock" and "unknown" do not (the app then offers no update and
// says the pin updates over USB). NULL leaves the key out, as before 0.4.1.
//
// `air` (Keyring Air builds only, build_firmware.sh --air): the key "air":1
// last, so the app says «hold the button 3 s» instead of «press RESET»
// (docs/pins/air-ship-mode.md). Every other build leaves it out and serves
// the value it served before.
//
// Room for the longest value with a 20-char device id, a 16-char firmware
// version, bind_s up to 3600, oled, bl "unknown" and air, plus the NUL.
static const size_t DEVICE_INFO_MAX = 112;

static inline int formatDeviceInfo(char* buf, size_t size, const char* deviceId, const char* fw,
                                   uint32_t bindSeconds, int8_t oled, const char* bootloader = NULL,
                                   bool air = false) {
  int n = oled < 0 ? snprintf(buf, size, "{\"device_id\":\"%s\",\"fw\":\"%s\",\"bind_s\":%lu", deviceId, fw,
                              (unsigned long)bindSeconds)
                   : snprintf(buf, size, "{\"device_id\":\"%s\",\"fw\":\"%s\",\"bind_s\":%lu,\"oled\":%d", deviceId,
                              fw, (unsigned long)bindSeconds, oled > 0 ? 1 : 0);
  if (n < 0 || (size_t)n >= size) return n;
  if (bootloader != NULL) {
    const int bl = snprintf(buf + n, size - (size_t)n, ",\"bl\":\"%s\"", bootloader);
    if (bl < 0 || (size_t)(n + bl) >= size) return bl < 0 ? bl : n + bl;
    n += bl;
  }
  const int tail = snprintf(buf + n, size - (size_t)n, air ? ",\"air\":1}" : "}");
  return tail < 0 ? tail : n + tail;
}

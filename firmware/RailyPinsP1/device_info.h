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

// Room for the longest value with a 20-char device id, a 16-char firmware
// version, bind_s up to 3600 and oled, plus the NUL.
static const size_t DEVICE_INFO_MAX = 96;

static inline int formatDeviceInfo(char* buf, size_t size, const char* deviceId, const char* fw,
                                   uint32_t bindSeconds, int8_t oled) {
  if (oled < 0) {
    return snprintf(buf, size, "{\"device_id\":\"%s\",\"fw\":\"%s\",\"bind_s\":%lu}", deviceId, fw,
                    (unsigned long)bindSeconds);
  }
  return snprintf(buf, size, "{\"device_id\":\"%s\",\"fw\":\"%s\",\"bind_s\":%lu,\"oled\":%d}", deviceId, fw,
                  (unsigned long)bindSeconds, oled > 0 ? 1 : 0);
}

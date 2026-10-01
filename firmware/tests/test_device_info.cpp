// Host tests for device_info.h: the value the phone reads over BLE.
#include <cstdio>
#include <cstring>

#include "../RailyPinsP1/device_info.h"

static int failures = 0;

static void expect(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    failures++;
  }
}

static bool format(char* buf, int8_t oled, const char* expected) {
  int n = formatDeviceInfo(buf, DEVICE_INFO_MAX, "rp1-0123456789abcdef", "0.2.20-qa", 180, oled);
  return n > 0 && (size_t)n < DEVICE_INFO_MAX && std::strcmp(buf, expected) == 0;
}

int main() {
  char buf[DEVICE_INFO_MAX];
  expect(format(buf, DEVICE_INFO_OLED_UNKNOWN,
                "{\"device_id\":\"rp1-0123456789abcdef\",\"fw\":\"0.2.20-qa\",\"bind_s\":180}"),
         "before the probe: no oled key, the value older firmware served");
  expect(format(buf, 1, "{\"device_id\":\"rp1-0123456789abcdef\",\"fw\":\"0.2.20-qa\",\"bind_s\":180,\"oled\":1}"),
         "a panel answered: oled 1");
  expect(format(buf, 0, "{\"device_id\":\"rp1-0123456789abcdef\",\"fw\":\"0.2.20-qa\",\"bind_s\":180,\"oled\":0}"),
         "no panel: oled 0");
  expect(format(buf, 5, "{\"device_id\":\"rp1-0123456789abcdef\",\"fw\":\"0.2.20-qa\",\"bind_s\":180,\"oled\":1}"),
         "any positive reads as 1: the app takes only 0 or 1");

  // The longest value still fits.
  int n = formatDeviceInfo(buf, DEVICE_INFO_MAX, "rp1-0123456789abcdef", "0123456789abcdef", 3600, 1);
  expect(n > 0 && (size_t)n < DEVICE_INFO_MAX, "a 16-char version, bind_s 3600 and oled fit");

  if (failures) {
    std::printf("device_info: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("device_info: ok\n");
  return 0;
}

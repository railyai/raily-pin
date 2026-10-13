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

  // The bootloader (0.4.1-qa+): the app offers a BLE update only for "otafix".
  int n = formatDeviceInfo(buf, DEVICE_INFO_MAX, "rp1-0123456789abcdef", "0.4.1-qa", 0, 1, "stock");
  expect(n > 0 && std::strcmp(buf, "{\"device_id\":\"rp1-0123456789abcdef\",\"fw\":\"0.4.1-qa\",\"bind_s\":0,"
                                    "\"oled\":1,\"bl\":\"stock\"}") == 0,
         "bl after oled");
  n = formatDeviceInfo(buf, DEVICE_INFO_MAX, "rp1-0123456789abcdef", "0.4.1-qa", 0, DEVICE_INFO_OLED_UNKNOWN, "otafix");
  expect(n > 0 && std::strcmp(buf, "{\"device_id\":\"rp1-0123456789abcdef\",\"fw\":\"0.4.1-qa\",\"bind_s\":0,"
                                    "\"bl\":\"otafix\"}") == 0,
         "bl without oled");

  // Keyring Air (--air): "air":1 last; P1 and DIY leave it out.
  n = formatDeviceInfo(buf, DEVICE_INFO_MAX, "rp1-0123456789abcdef", "0.4.2-qa", 180, 1, "stock", true);
  expect(n > 0 && std::strcmp(buf, "{\"device_id\":\"rp1-0123456789abcdef\",\"fw\":\"0.4.2-qa\",\"bind_s\":180,"
                                    "\"oled\":1,\"bl\":\"stock\",\"air\":1}") == 0,
         "Air: air 1 after bl");
  n = formatDeviceInfo(buf, DEVICE_INFO_MAX, "rp1-0123456789abcdef", "0.4.2-qa", 0, DEVICE_INFO_OLED_UNKNOWN, NULL, true);
  expect(n > 0 && std::strcmp(buf, "{\"device_id\":\"rp1-0123456789abcdef\",\"fw\":\"0.4.2-qa\",\"bind_s\":0,"
                                    "\"air\":1}") == 0,
         "Air before the panel probe, no bl: air 1 only");
  n = formatDeviceInfo(buf, DEVICE_INFO_MAX, "rp1-0123456789abcdef", "0.4.2-qa", 180, 1, "otafix", false);
  expect(n > 0 && std::strstr(buf, "air") == nullptr, "P1 / DIY: no air key");
  n = formatDeviceInfo(buf, DEVICE_INFO_MAX, "rp1-0123456789abcdef", "0.4.2-qa", 180, 1, "otafix");
  expect(n > 0 && std::strstr(buf, "air") == nullptr, "the default is no air key");
  n = formatDeviceInfo(buf, DEVICE_INFO_MAX, "rp1-0123456789abcdef", "0123456789abcdef", 3600, 1, "unknown", true);
  std::printf("device_info: longest Air value %d of %zu characters\n", n, DEVICE_INFO_MAX - 1);
  expect(n > 0 && (size_t)n < DEVICE_INFO_MAX, "a 16-char version, bind_s 3600, oled, bl unknown and air fit");
  char tight[90];
  n = formatDeviceInfo(tight, sizeof(tight), "rp1-0123456789abcdef", "0123456789abcdef", 3600, 1, "unknown", true);
  expect(n >= (int)sizeof(tight), "a buffer that ends inside bl reports truncation, never overflows");

  // The longest value still fits.
  n = formatDeviceInfo(buf, DEVICE_INFO_MAX, "rp1-0123456789abcdef", "0123456789abcdef", 3600, 1, "unknown");
  expect(n > 0 && (size_t)n < DEVICE_INFO_MAX, "a 16-char version, bind_s 3600, oled and bl unknown fit");
  char small[40];
  n = formatDeviceInfo(small, sizeof(small), "rp1-0123456789abcdef", "0.4.1-qa", 0, 1, "stock");
  expect(n >= (int)sizeof(small), "a short buffer reports truncation, never overflows");

  if (failures) {
    std::printf("device_info: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("device_info: ok\n");
  return 0;
}

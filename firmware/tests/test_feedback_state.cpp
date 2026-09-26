#include <stdio.h>
#include "../RailyPinsP1/feedback_state.h"

static int failures = 0;
static void expect(bool condition, const char* name) {
  printf("%s %s\n", condition ? "PASS" : "FAIL", name);
  if (!condition) failures++;
}

int main() {
  FeedbackState feedback = {};
  FeedbackLights lights = feedback.tick(400, false, 0);
  expect(lights.blue && !lights.green, "disconnected pin blinks blue");
  lights = feedback.tick(400, true, 0);
  expect(lights.green && !lights.blue, "connect takes over blue blink immediately");
  lights = feedback.tick(480, true, 2);
  expect(lights.red && lights.green, "ACK starts red feedback without blocking");
  lights = feedback.tick(560, true, 0);
  expect(!lights.red, "red pulse switches off after 80 ms");
  lights = feedback.tick(640, true, 0);
  expect(lights.red, "second pulse starts after 80 ms off");
  lights = feedback.tick(800, true, 0);
  expect(!lights.red && lights.green, "feedback finishes at bounded duration");
  lights = feedback.tick(801, false, 0);
  expect(!lights.green && !lights.blue, "disconnect clears green and blink phase");

  // Flood writes only replace one pending pattern. They cannot extend the
  // current flash or block the BLE callback for the LED duration.
  FeedbackState flooded = {};
  flooded.tick(0, true, 4);
  for (uint32_t now = 1; now < 80; now++) flooded.tick(now, true, 4);
  lights = flooded.tick(80, true, 4);
  expect(!lights.red, "ACK flood cannot restart the active pulse");
  lights = flooded.tick(640, true, 2);
  expect(!lights.red, "queued feedback waits after the active pattern");
  lights = flooded.tick(799, true, 2);
  expect(!lights.red, "ACK flood remains rate-limited through 799 ms");
  lights = flooded.tick(800, true, 2);
  expect(lights.red, "latest queued pattern starts at the 800 ms rate bound");
  lights = flooded.tick(880, true, 0);
  expect(!lights.red, "queued two-flash pattern has an off edge");
  lights = flooded.tick(960, true, 0);
  expect(lights.red, "queued feedback keeps the latest two-flash pattern");
  lights = flooded.tick(1120, true, 0);
  expect(!lights.red, "rate-limited feedback terminates after two flashes");

  FeedbackState wrapped = {};
  wrapped.tick(UINT32_MAX - 398, true, 1);
  lights = wrapped.tick(400, true, 2);
  expect(!lights.red, "feedback stays rate-limited across millis rollover");
  lights = wrapped.tick(401, true, 0);
  expect(lights.red, "queued feedback starts after rollover cooldown");
  return failures ? 1 : 0;
}

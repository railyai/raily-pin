// Host tests for bench_serial.h: the bench's `o<scene>…` and `c<hh>`
// serial commands, and that neither leaves a pending state behind.
#include <stdio.h>
#include <string>
#include "../RailyPinsP1/bench_serial.h"

static int failures = 0;
static void expect(bool condition, const char* name) {
  printf("%s %s\n", condition ? "PASS" : "FAIL", name);
  if (!condition) failures++;
}

struct Run {
  std::string fellThrough;  // characters the sketch handles as usual
  int acks = 0;
  uint8_t lastAck = 0;
  int demos = 0;
  BenchCommand lastDemo = {};
  int unknown = 0;
  int unavailable = 0;
  int badHex = 0;
  int falls = 0;
};

static Run feed(BenchSerial& bench, const char* text, bool demoEnabled = true) {
  Run run;
  for (const char* p = text; *p; p++) {
    BenchCommand command;
    if (!bench.feed(*p, demoEnabled, &command)) run.fellThrough += *p;
    switch (command.action) {
      case BENCH_ACK: run.acks++; run.lastAck = command.ack; break;
      case BENCH_DEMO: run.demos++; run.lastDemo = command; break;
      case BENCH_DEMO_UNKNOWN: run.unknown++; break;
      case BENCH_DEMO_UNAVAILABLE: run.unavailable++; break;
      case BENCH_ACK_BAD: run.badHex++; break;
      case BENCH_FALL: run.falls++; break;
      default: break;
    }
  }
  return run;
}

int main() {
  {
    BenchSerial b = {};
    Run r = feed(b, "c11");
    expect(r.acks == 1 && r.lastAck == 0x11 && r.fellThrough.empty(), "c11 publishes 0x11");
    r = feed(b, "c04C0a");
    expect(r.acks == 2 && r.lastAck == 0x0a, "c04 then C0a (upper case, hex letters)");
  }
  {
    // Qoder #1193: `op10` left the locale digit pending and ate `c11`.
    BenchSerial b = {};
    Run r = feed(b, "op10c11");
    expect(r.demos == 3 && r.lastDemo.shape == 1 && r.lastDemo.material == 0, "op10 shows pressed, cube, satin");
    expect(r.acks == 1 && r.lastAck == 0x11 && r.fellThrough.empty(), "and the c11 after it still plays 0x11");
    r = feed(b, "op1c11");
    expect(r.acks == 1 && r.lastAck == 0x11, "after a shape digit too");
    r = feed(b, "opc11");
    expect(r.acks == 1 && r.lastAck == 0x11, "and right after the scene letter");
    r = feed(b, "ow1234");
    expect(r.demos == 4 && r.lastDemo.locale == 3 && r.lastDemo.material == 2 && r.lastDemo.shape == 1 &&
               r.fellThrough == "4",
           "ow123: watch, cube, glass, es; the 4 after the complete command falls through");
    r = feed(b, "c11");
    expect(r.acks == 1 && r.demos == 0, "a complete demo leaves nothing pending");
    r = feed(b, "ow12\n");
    expect(r.fellThrough == "\n", "Enter ends a demo and falls through");
    r = feed(b, "1");
    expect(r.demos == 0 && r.fellThrough == "1", "so a later digit is no locale");
  }
  {
    // A pending `c` never survives an `o` command.
    BenchSerial b = {};
    Run r = feed(b, "cok\n11");
    expect(r.acks == 0 && r.demos == 1 && r.lastDemo.scene == OLED_SCENE_NOTHING_KNOWN, "c then ok: the demo, no ack");
    expect(r.fellThrough == "\n11", "the later digits fall through instead of finishing the c");
    r = feed(b, "cox11");
    expect(r.acks == 0 && r.unknown == 1 && r.fellThrough == "11", "c, then o + a non-scene: the c is gone, 11 is not an ack");
    r = feed(b, "c1ok");
    expect(r.acks == 0 && r.demos == 1, "a half-typed c1 is cancelled by o too");
  }
  {
    // A non-fitting character ends the command and falls through, like `o`.
    BenchSerial b = {};
    Run r = feed(b, "cp");
    expect(r.acks == 0 && r.badHex == 1 && r.fellThrough == "p", "c + non-hex prints the hint and p still presses");
    r = feed(b, "c1p");
    expect(r.acks == 0 && r.badHex == 1 && r.fellThrough == "p", "after one digit too");
    r = feed(b, "c\n");
    expect(r.fellThrough == "\n" && r.acks == 0, "Enter ends a c");
    r = feed(b, "or");
    expect(r.demos == 1 && r.lastDemo.scene == OLED_SCENE_RESULT && r.fellThrough.empty(), "or shows the result, never resets");
    r = feed(b, "ox");
    expect(r.unknown == 1 && r.fellThrough.empty(), "o + a non-scene prints the scenes");
    r = feed(b, "op9");
    expect(r.demos == 1 && r.fellThrough == "9", "a shape out of range falls through");
    r = feed(b, "op0p");
    expect(r.demos == 2 && r.fellThrough == "p", "and op0 then p still presses");
    r = feed(b, "om");
    expect(r.demos == 1 && r.lastDemo.modest && r.lastDemo.scene == OLED_SCENE_WATCH, "om: on watch, modest");
    r = feed(b, "oo");
    expect(r.demos == 1 && r.lastDemo.scene == OLED_SCENE_OFF, "oo: the off scene");
    r = feed(b, "o\x01");
    expect(r.unknown == 1, "a control byte is no scene");
  }
  {
    // `of`: the real fall path, never a static demo, and it takes no digits.
    BenchSerial b = {};
    Run r = feed(b, "of");
    expect(r.falls == 1 && r.demos == 0 && r.fellThrough.empty(), "of plays a real fall, not a demo");
    r = feed(b, "oF");
    expect(r.falls == 1 && r.demos == 0, "oF too (the plan's spelling)");
    r = feed(b, "of12");
    expect(r.falls == 1 && r.demos == 0 && r.fellThrough == "12", "of takes no look digits: they fall through");
    r = feed(b, "ofc11");
    expect(r.falls == 1 && r.acks == 1 && r.lastAck == 0x11, "of leaves nothing pending: c11 still plays");
    r = feed(b, "cof");
    expect(r.falls == 1 && r.acks == 0, "a pending c is cancelled by of");
    r = feed(b, "of", false);
    expect(r.falls == 0 && r.unavailable == 1 && r.fellThrough == "f", "RAILY_OLED=0: of is unavailable");
  }
  {
    // LED-only build: only the `o` is consumed.
    BenchSerial b = {};
    Run r = feed(b, "op", false);
    expect(r.unavailable == 1 && r.fellThrough == "p", "RAILY_OLED=0: op still presses");
    r = feed(b, "c11", false);
    expect(r.acks == 1 && r.lastAck == 0x11, "and c11 still plays");
  }
  return failures ? 1 : 0;
}

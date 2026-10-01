#pragma once

#include <stdint.h>
#include <string.h>
#include "oled_assets.h"
#include "screen_payload.h"

// The bench's multi-character serial commands (docs/pins/firmware.md
// «Serial protocol»), parsed here so both are host-tested
// (tests/test_bench_serial.cpp); the sketch only runs what they return.
//
//   o<scene>[shape[material[locale]]]  a screen scene for 30 s; `om` = modest
//   of                                 a real fall: screenOnFall(), as the IMU will
//   c<hh>                              one event_ack byte in hex (c11)
//
// `of` (or `oF`) is no static demo: it runs the same path as gestures S1's
// «dropped» gesture (ScreenState::onFall), so the phone's mascot, locale,
// modest and disabled apply, a press in flight wins, and nothing vibrates.
// It takes no digits.
//
// One parser for both, so one command's pending state never survives the
// other: an `o` cancels a pending `c`, and a pending demo digit is dropped
// as soon as anything else arrives. A character that does not fit the
// command in progress ends it and falls through (feed() returns false), so
// the sketch handles it as usual: `o` + a non-scene prints the scenes, `c`
// + a non-hex digit prints a hint, and in both cases `op` / `cp` still
// press. Serial bytes are untrusted: only printable ASCII is looked up.
enum BenchAction : uint8_t {
  BENCH_NONE = 0,
  BENCH_DEMO,             // show demo scene/shape/material/locale/modest
  BENCH_DEMO_UNKNOWN,     // `o` + a letter that is no scene: print the list
  BENCH_DEMO_UNAVAILABLE, // `o` in a RAILY_OLED=0 build
  BENCH_ACK,              // publish `ack` as an event_ack write
  BENCH_ACK_BAD,          // `c` + a non-hex digit: print the hint
  BENCH_FALL,             // `of`: a real fall (screenOnFall), not a demo
};

struct BenchCommand {
  uint8_t action;
  uint8_t scene;
  uint8_t shape;
  uint8_t material;
  uint8_t locale;
  bool modest;
  uint8_t ack;
};

struct BenchSerial {
  uint8_t demoStage;  // 0 none, 1 scene letter, 2 shape, 3 material, 4 locale digit
  uint8_t ackStage;   // 0 none, 1 high nibble, 2 low nibble
  uint8_t ackValue;
  BenchCommand demo;  // the demo being built

  // True when c belongs to a bench command; `out` says what to do (it may
  // be BENCH_NONE for a consumed prefix). False: handle c as usual.
  bool feed(char c, bool demoEnabled, BenchCommand* out) {
    *out = BenchCommand();
    if (c == 'o' || c == 'O') {
      if (demoStage != 1) {  // `oo` looks `o` up as a scene: the off scene
        ackStage = 0;        // an `o` cancels a pending `c`
        if (!demoEnabled) {
          demoStage = 0;
          out->action = BENCH_DEMO_UNAVAILABLE;
          return true;
        }
        demoStage = 1;
        return true;
      }
    }
    if (demoStage != 0) {
      if (feedDemo(c, out)) return true;
      demoStage = 0;  // anything else ends the demo digits: fall through
    }
    return feedAck(c, out);
  }

 private:
  static int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }

  bool feedDemo(char c, BenchCommand* out) {
    if (demoStage == 1) {
      demoStage = 0;
      const unsigned char u = (unsigned char)c;
      char lower = 0;
      const char* hit = NULL;
      if (u > 0x20 && u < 0x7F) {
        lower = (u >= 'A' && u <= 'Z') ? (char)(u + ('a' - 'A')) : (char)u;
        hit = strchr(kOledDemoLetters, lower);
      }
      const bool modest = lower != 0 && lower == OLED_DEMO_MODEST_LETTER;
      if (hit == NULL && !modest) {
        out->action = BENCH_DEMO_UNKNOWN;
        return true;  // the letter after `o` is always consumed: `or` never resets
      }
      if (!modest && (uint8_t)(hit - kOledDemoLetters) == OLED_SCENE_FELL) {
        out->action = BENCH_FALL;  // complete: a digit after it falls through
        return true;
      }
      demo = BenchCommand();
      demo.action = BENCH_DEMO;
      demo.scene = modest ? OLED_SCENE_WATCH : (uint8_t)(hit - kOledDemoLetters);
      demo.shape = OLED_SHAPE_PEBBLE;
      demo.material = OLED_MATERIAL_SATIN;
      demo.modest = modest;
      demoStage = 2;
      *out = demo;
      return true;
    }
    if (demoStage == 2 && c >= '0' && c < (char)('0' + OLED_SHAPE_COUNT)) {
      demo.shape = (uint8_t)(c - '0');
      demoStage = 3;
      *out = demo;
      return true;
    }
    if (demoStage == 3 && c >= '0' && c < (char)('0' + OLED_MATERIAL_COUNT)) {
      demo.material = (uint8_t)(c - '0');
      demoStage = 4;
      *out = demo;
      return true;
    }
    if (demoStage == 4 && c >= '0' && c <= (char)('0' + SCREEN_LOCALE_MAX)) {
      demo.locale = (uint8_t)(c - '0');  // 0 and 5 (ar) draw icons only
      demoStage = 0;                     // the command is complete
      *out = demo;
      return true;
    }
    return false;
  }

  bool feedAck(char c, BenchCommand* out) {
    if (ackStage == 0) {
      if (c != 'c' && c != 'C') return false;
      ackStage = 1;
      ackValue = 0;
      return true;
    }
    const int nibble = hexValue(c);
    if (nibble < 0) {
      // Like `o` + a non-scene: the command ends, and c falls through.
      ackStage = 0;
      out->action = BENCH_ACK_BAD;
      return false;
    }
    ackValue = (uint8_t)((ackValue << 4) | nibble);
    if (ackStage == 1) {
      ackStage = 2;
      return true;
    }
    ackStage = 0;
    out->action = BENCH_ACK;
    out->ack = ackValue;
    return true;
  }
};

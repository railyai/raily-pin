#pragma once

#include <stdint.h>

// Voice bench serial commands (docs/pins/voice-from-pin.md §10 V0a, V2;
// docs/pins/firmware.md «Serial protocol»). Bench builds only
// (-DRAILY_VOICE_BENCH=1): a release never carries them. Pure, host-tested
// in hardware/firmware/tests/test_voice_bench.cpp.
//
//   v       a fake hold begins: the same gesture as the D1 button, with the
//           USB serial port as the armed link; frames come out as `VF <hex>`
//           lines (hardware/tools/voice_capture.py serial)
//   V       the fake hold ends (the 60 s cap ends it too)
//   m       raw PCM on/off: the mic runs, every 20 ms frame (after the
//           high-pass, before ADPCM) comes out as a `VP <hex>` line, for
//           gain calibration; never over BLE. Off prints a `VS {…}` count
//   G<hh>   the PDM gain register for the next mic start, two hex digits,
//           0x00 (-20 dB) to 0x50 (+20 dB), 0.5 dB a step
//   d       the mic power pin's drive: high drive (H0H1, the default) or
//           standard (S0S1), for the next mic start
//   A       the GATT table as the SoftDevice holds it: one `GATT` line per
//           attribute (handle, UUID, read and write security), so the voice
//           characteristics and their encrypted CCCD are visible without a
//           BLE client (the phone usually holds the pin's only link)
//
// Lowercase `g` is gestures-spec.md's (`g1`…`g5`), so the gain is `G`. The
// VDD bench owns `v`/`V` too, so the two benches never share a build
// (build_firmware.sh). A `G` followed by a non-hex digit ends the command
// and the character falls through, like `c` in bench_serial.h.

enum VoiceBenchAction : uint8_t {
  VOICE_BENCH_NONE = 0,
  VOICE_BENCH_HOLD_BEGIN,
  VOICE_BENCH_HOLD_END,
  VOICE_BENCH_RAW_TOGGLE,
  VOICE_BENCH_GAIN,      // `gain` holds the register value
  VOICE_BENCH_GAIN_BAD,  // not two hex digits, or above 0x50
  VOICE_BENCH_DRIVE_TOGGLE,
  VOICE_BENCH_GATT_DUMP,
};

static const uint8_t VOICE_BENCH_GAIN_MAX = 0x50;

struct VoiceBenchCommand {
  uint8_t action;
  uint8_t gain;
};

static inline int8_t voiceBenchHex(char c) {
  if (c >= '0' && c <= '9') return (int8_t)(c - '0');
  if (c >= 'a' && c <= 'f') return (int8_t)(c - 'a' + 10);
  if (c >= 'A' && c <= 'F') return (int8_t)(c - 'A' + 10);
  return -1;
}

struct VoiceBench {
  uint8_t gainStage;  // 0 none, 1 high nibble, 2 low nibble
  uint8_t gainValue;

  // True when c belongs to a voice bench command (out may then be NONE for
  // a consumed prefix). False: handle c as usual.
  bool feed(char c, VoiceBenchCommand* out) {
    out->action = VOICE_BENCH_NONE;
    out->gain = 0;
    if (gainStage != 0) {
      const int8_t nibble = voiceBenchHex(c);
      if (nibble < 0) {
        gainStage = 0;
        out->action = VOICE_BENCH_GAIN_BAD;
        return false;  // the character is handled as usual
      }
      if (gainStage == 1) {
        gainValue = (uint8_t)(nibble << 4);
        gainStage = 2;
        return true;
      }
      gainStage = 0;
      gainValue = (uint8_t)(gainValue | nibble);
      out->action = gainValue <= VOICE_BENCH_GAIN_MAX ? VOICE_BENCH_GAIN : VOICE_BENCH_GAIN_BAD;
      out->gain = gainValue;
      return true;
    }
    switch (c) {
      case 'v':
        out->action = VOICE_BENCH_HOLD_BEGIN;
        return true;
      case 'V':
        out->action = VOICE_BENCH_HOLD_END;
        return true;
      case 'm':
      case 'M':
        out->action = VOICE_BENCH_RAW_TOGGLE;
        return true;
      case 'd':
      case 'D':
        out->action = VOICE_BENCH_DRIVE_TOGGLE;
        return true;
      case 'A':
        out->action = VOICE_BENCH_GATT_DUMP;
        return true;
      case 'G':
        gainStage = 1;
        return true;
      default:
        return false;
    }
  }
};

// "<prefix> <hex>\n" into out (2 * length + prefix + 3 bytes at most);
// returns the length written. The bench's `VF` and `VP` lines.
static inline uint16_t voiceHexLine(char* out, const char* prefix, const uint8_t* data, uint16_t length) {
  static const char HEX_DIGITS[] = "0123456789ABCDEF";
  uint16_t at = 0;
  while (*prefix) out[at++] = *prefix++;
  out[at++] = ' ';
  for (uint16_t i = 0; i < length; i++) {
    out[at++] = HEX_DIGITS[data[i] >> 4];
    out[at++] = HEX_DIGITS[data[i] & 0x0F];
  }
  out[at++] = '\n';
  out[at] = '\0';
  return at;
}

#pragma once

#include <stdint.h>

// IMA-ADPCM 4:1 for voice from the pin (docs/pins/voice-from-pin.md §3, §4).
// The one flavor both sides run: the IMA reference algorithm (IMA Digital
// Audio Focus and Technical Working Groups, 1992; the same as Microsoft
// WAVE_FORMAT_IMA_ADPCM), with the bit-serial difference
//
//   vpdiff = step >> 3
//   if (nibble & 4) vpdiff += step
//   if (nibble & 2) vpdiff += step >> 1
//   if (nibble & 1) vpdiff += step >> 2
//   predictor += (nibble & 8) ? -vpdiff : vpdiff, clamped to [-32768, 32767]
//   index += INDEX_TABLE[nibble], clamped to [0, 88]
//
// not the "exact multiply" ((2 * d + 1) * step) >> 3 variant, which differs
// by one LSB. The encoder computes the same vpdiff, so its predictor is
// always the decoder's. Two samples per byte, low nibble first. The Swift
// decoder in the companion (V3a) and hardware/tools/voice_capture.py decode
// the same golden vectors (hardware/firmware/tests/vectors/adpcm_*.bin) to
// the same PCM, bit for bit.
// Pure, host-tested in hardware/firmware/tests/test_adpcm.cpp.

static const uint8_t ADPCM_STEP_INDEX_MAX = 88;

static const int16_t ADPCM_STEP_TABLE[ADPCM_STEP_INDEX_MAX + 1] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,
    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,
    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,   253,   279,
    307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,   876,   963,
    1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,  3327,
    3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

static const int8_t ADPCM_INDEX_TABLE[16] = {-1, -1, -1, -1, 2, 4, 6, 8,
                                             -1, -1, -1, -1, 2, 4, 6, 8};

// The codec state: what an AUDIO frame header carries (predictor, then
// step_index), the state before the frame's first nibble.
struct AdpcmState {
  int16_t predictor;
  uint8_t stepIndex;  // 0..88
};

static inline int32_t adpcmClampSample(int32_t value) {
  return value > 32767 ? 32767 : value < -32768 ? -32768 : value;
}

static inline uint8_t adpcmClampIndex(int32_t index) {
  return (uint8_t)(index < 0 ? 0 : index > ADPCM_STEP_INDEX_MAX ? ADPCM_STEP_INDEX_MAX : index);
}

// One step of the shared update: the decoder's whole job, the encoder's
// last step. A step index above 88 (a corrupt header) is clamped first.
static inline int16_t adpcmApplyNibble(AdpcmState& state, uint8_t nibble) {
  nibble &= 0x0F;
  const int32_t step = ADPCM_STEP_TABLE[adpcmClampIndex(state.stepIndex)];
  int32_t vpdiff = step >> 3;
  if (nibble & 4) vpdiff += step;
  if (nibble & 2) vpdiff += step >> 1;
  if (nibble & 1) vpdiff += step >> 2;
  const int32_t predicted = (nibble & 8) ? (int32_t)state.predictor - vpdiff : (int32_t)state.predictor + vpdiff;
  state.predictor = (int16_t)adpcmClampSample(predicted);
  state.stepIndex = adpcmClampIndex((int32_t)adpcmClampIndex(state.stepIndex) + ADPCM_INDEX_TABLE[nibble]);
  return state.predictor;
}

static inline uint8_t adpcmEncodeSample(AdpcmState& state, int16_t sample) {
  int32_t step = ADPCM_STEP_TABLE[adpcmClampIndex(state.stepIndex)];
  int32_t diff = (int32_t)sample - state.predictor;
  uint8_t nibble = 0;
  if (diff < 0) {
    nibble = 8;
    diff = -diff;
  }
  if (diff >= step) {
    nibble |= 4;
    diff -= step;
  }
  step >>= 1;
  if (diff >= step) {
    nibble |= 2;
    diff -= step;
  }
  step >>= 1;
  if (diff >= step) nibble |= 1;
  adpcmApplyNibble(state, nibble);
  return nibble;
}

static inline int16_t adpcmDecodeNibble(AdpcmState& state, uint8_t nibble) {
  return adpcmApplyNibble(state, nibble);
}

// `sampleCount` samples into sampleCount / 2 bytes, low nibble first. An odd
// count leaves the last byte's high nibble 0 (voice frames are always 320).
static inline void adpcmEncode(AdpcmState& state, const int16_t* pcm, uint16_t sampleCount, uint8_t* out) {
  for (uint16_t i = 0; i < sampleCount; i += 2) {
    const uint8_t low = adpcmEncodeSample(state, pcm[i]);
    const uint8_t high = (uint16_t)(i + 1) < sampleCount ? adpcmEncodeSample(state, pcm[i + 1]) : 0;
    out[i / 2] = (uint8_t)(low | (high << 4));
  }
}

// `byteCount` bytes into 2 * byteCount samples.
static inline void adpcmDecode(AdpcmState& state, const uint8_t* in, uint16_t byteCount, int16_t* out) {
  for (uint16_t i = 0; i < byteCount; i++) {
    out[2 * i] = adpcmDecodeNibble(state, (uint8_t)(in[i] & 0x0F));
    out[2 * i + 1] = adpcmDecodeNibble(state, (uint8_t)(in[i] >> 4));
  }
}

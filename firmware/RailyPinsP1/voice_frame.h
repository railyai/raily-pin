#pragma once

#include <stdint.h>
#include <string.h>

#include "adpcm.h"

// The voice_stream (7B1E000A) notify payloads (docs/pins/voice-from-pin.md
// §4 «Frame format»). Little-endian. Common 4-byte header:
//
//   kind u8 · utterance u8 · seq u16
//
// kind: 1 START, 2 AUDIO, 3 END, 4 PRESS. utterance: +1 per armed press
// (the PRESS that opens it and the START that may follow share the id),
// wraps 255 -> 0. seq: per utterance, 0 for PRESS and START, 1..n for AUDIO,
// n + 1 for END; the 60 s cap keeps it under 3 002, so it never wraps.
//
//   PRESS  header                                                       4 B
//   START  header · codec u8 · sample_rate u16 · frame_ms u8 · gain i8
//          (0.5 dB units) · flags u8 (bit0 = pre-roll included)        10 B
//   AUDIO  header · codec u8 · predictor i16 · step_index u8 ·
//          160 B = 320 samples, low nibble first                      168 B
//   END    header · reason u8 · frames_sent u16 · frames_dropped u16    9 B
//
// AUDIO's predictor and step_index are the decoder state BEFORE the first
// nibble, never an output sample, so every frame decodes on its own: a lost
// frame costs 20 ms and never corrupts the next one. The Swift parser (V3a)
// and hardware/tools/voice_capture.py read exactly these bytes; the golden
// vectors in hardware/firmware/tests/vectors/ are the contract.
// Pure, host-tested in hardware/firmware/tests/test_voice_frame.cpp.

static const uint8_t VOICE_KIND_START = 1;
static const uint8_t VOICE_KIND_AUDIO = 2;
static const uint8_t VOICE_KIND_END = 3;
static const uint8_t VOICE_KIND_PRESS = 4;

static const uint8_t VOICE_CODEC_IMA_ADPCM_16K = 1;  // 0 is reserved
static const uint16_t VOICE_SAMPLE_RATE = 16000;
static const uint8_t VOICE_FRAME_MS = 20;
static const uint16_t VOICE_FRAME_SAMPLES = 320;  // 16 kHz x 20 ms
static const uint16_t VOICE_FRAME_DATA_BYTES = VOICE_FRAME_SAMPLES / 2;
static const uint8_t VOICE_START_FLAG_PREROLL = 0x01;

static const uint8_t VOICE_HEADER_BYTES = 4;
static const uint8_t VOICE_PRESS_BYTES = 4;
static const uint8_t VOICE_START_BYTES = 10;
static const uint8_t VOICE_AUDIO_BYTES = 168;
static const uint8_t VOICE_END_BYTES = 9;
// An AUDIO frame needs ATT MTU >= 171 (168 + the 3-byte notify header).
static const uint16_t VOICE_MIN_ATT_MTU = VOICE_AUDIO_BYTES + 3;

// END reason: its own code space, unrelated to voice_control STOP's reason.
enum VoiceEndReason : uint8_t {
  VOICE_END_RELEASED = 0,
  VOICE_END_CAP = 1,          // the 60 s utterance cap
  VOICE_END_OVERFLOW = 2,     // the pin's frame ring was full
  VOICE_END_LOW_BATTERY = 3,
  VOICE_END_PHONE_STOP = 4,   // voice_control STOP
};

static inline void voicePutU16(uint8_t* at, uint16_t value) {
  at[0] = (uint8_t)(value & 0xFF);
  at[1] = (uint8_t)(value >> 8);
}

static inline uint16_t voiceGetU16(const uint8_t* at) {
  return (uint16_t)(at[0] | (at[1] << 8));
}

static inline void voicePutHeader(uint8_t* out, uint8_t kind, uint8_t utterance, uint16_t seq) {
  out[0] = kind;
  out[1] = utterance;
  voicePutU16(out + 2, seq);
}

static inline uint8_t voicePackPress(uint8_t* out, uint8_t utterance) {
  voicePutHeader(out, VOICE_KIND_PRESS, utterance, 0);
  return VOICE_PRESS_BYTES;
}

static inline uint8_t voicePackStart(uint8_t* out, uint8_t utterance, int8_t gainHalfDb, uint8_t flags) {
  voicePutHeader(out, VOICE_KIND_START, utterance, 0);
  out[4] = VOICE_CODEC_IMA_ADPCM_16K;
  voicePutU16(out + 5, VOICE_SAMPLE_RATE);
  out[7] = VOICE_FRAME_MS;
  out[8] = (uint8_t)gainHalfDb;
  out[9] = flags;
  return VOICE_START_BYTES;
}

// Encodes one 20 ms frame of PCM: the header takes `state` as it is before
// the first sample, then the encoder carries it on for the next frame.
static inline uint8_t voicePackAudio(uint8_t* out, uint8_t utterance, uint16_t seq, AdpcmState& state,
                                     const int16_t pcm[VOICE_FRAME_SAMPLES]) {
  voicePutHeader(out, VOICE_KIND_AUDIO, utterance, seq);
  out[4] = VOICE_CODEC_IMA_ADPCM_16K;
  voicePutU16(out + 5, (uint16_t)state.predictor);
  out[7] = state.stepIndex;
  adpcmEncode(state, pcm, VOICE_FRAME_SAMPLES, out + 8);
  return VOICE_AUDIO_BYTES;
}

static inline uint8_t voicePackEnd(uint8_t* out, uint8_t utterance, uint16_t seq, uint8_t reason,
                                   uint16_t framesSent, uint16_t framesDropped) {
  voicePutHeader(out, VOICE_KIND_END, utterance, seq);
  out[4] = reason;
  voicePutU16(out + 5, framesSent);
  voicePutU16(out + 7, framesDropped);
  return VOICE_END_BYTES;
}

// A parsed payload. The parser is the reference for the phone's: exact
// lengths, a known kind, codec 1, a START at 16 kHz / 20 ms, a step index
// in 0..88. Anything else is rejected whole.
struct VoiceFrame {
  uint8_t kind;
  uint8_t utterance;
  uint16_t seq;
  // START
  uint8_t codec;
  uint16_t sampleRate;
  uint8_t frameMs;
  int8_t gainHalfDb;
  uint8_t flags;
  // AUDIO (codec too)
  AdpcmState state;
  const uint8_t* data;  // VOICE_FRAME_DATA_BYTES, inside the parsed buffer
  // END
  uint8_t reason;
  uint16_t framesSent;
  uint16_t framesDropped;
};

static inline bool voiceParseFrame(const uint8_t* in, uint16_t length, VoiceFrame* out) {
  if (in == 0 || out == 0 || length < VOICE_HEADER_BYTES) return false;
  memset(out, 0, sizeof(*out));
  out->kind = in[0];
  out->utterance = in[1];
  out->seq = voiceGetU16(in + 2);
  switch (out->kind) {
    case VOICE_KIND_PRESS:
      return length == VOICE_PRESS_BYTES;
    case VOICE_KIND_START:
      if (length != VOICE_START_BYTES) return false;
      out->codec = in[4];
      out->sampleRate = voiceGetU16(in + 5);
      out->frameMs = in[7];
      out->gainHalfDb = (int8_t)in[8];
      out->flags = in[9];
      return out->codec == VOICE_CODEC_IMA_ADPCM_16K && out->sampleRate == VOICE_SAMPLE_RATE &&
             out->frameMs == VOICE_FRAME_MS;
    case VOICE_KIND_AUDIO:
      if (length != VOICE_AUDIO_BYTES) return false;
      out->codec = in[4];
      out->state.predictor = (int16_t)voiceGetU16(in + 5);
      out->state.stepIndex = in[7];
      out->data = in + 8;
      return out->codec == VOICE_CODEC_IMA_ADPCM_16K && out->state.stepIndex <= ADPCM_STEP_INDEX_MAX;
    case VOICE_KIND_END:
      if (length != VOICE_END_BYTES) return false;
      out->reason = in[4];
      out->framesSent = voiceGetU16(in + 5);
      out->framesDropped = voiceGetU16(in + 7);
      return true;
    default:
      return false;
  }
}

// Decodes a parsed AUDIO frame on its own, from its header.
static inline void voiceDecodeAudio(const VoiceFrame& frame, int16_t out[VOICE_FRAME_SAMPLES]) {
  AdpcmState state = frame.state;
  adpcmDecode(state, frame.data, VOICE_FRAME_DATA_BYTES, out);
}

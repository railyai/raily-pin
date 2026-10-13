// Host tests for adpcm.h and the golden vectors (docs/pins/voice-from-pin.md
// §3, §10 V1). Usage:
//   test_adpcm <vectors dir>           check (run_tests.sh)
//   test_adpcm --write <vectors dir>   regenerate the vectors (only when the
//                                      contract changes on purpose; the Swift
//                                      decoder must change with it)
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "../RailyPinsP1/adpcm.h"
#include "../RailyPinsP1/voice_frame.h"
#include "voice_vectors.h"

static int failures = 0;

static void expect(bool ok, const char* what) {
  printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) failures++;
}

// One utterance of a vector stream: a tap (PRESS only) or a full talk.
struct Segment {
  uint8_t utterance;
  bool tap;
  uint16_t frames;  // AUDIO frames, taken in order from the case's PCM
  uint8_t endReason;
};

struct VectorCase {
  const char* name;
  Segment segments[3];
  int segmentCount;
};

static const int8_t VECTOR_GAIN = 40;  // +20 dB, the P1 start gain (voice_board.h)

static const VectorCase CASES[VOICE_VECTOR_CASE_COUNT] = {
    {"sine", {{1, false, 25, VOICE_END_RELEASED}}, 1},
    {"silence", {{1, false, 5, VOICE_END_RELEASED}}, 1},
    {"step_clip", {{1, false, 4, VOICE_END_RELEASED}}, 1},
    // A tone swelling from a whisper to full scale and back: the step index
    // walks through every one of the 89 table entries, so a decoder with a
    // wrong entry anywhere in its table fails this vector.
    {"sweep", {{1, false, 50, VOICE_END_RELEASED}}, 1},
    // A tap that only spends an id, then the u8 utterance id wraps 255 -> 0
    // and seq restarts at 0 on each START.
    {"stream", {{254, true, 0, 0}, {255, false, 6, VOICE_END_RELEASED}, {0, false, 4, VOICE_END_CAP}}, 3},
};

static const VectorCase* findCase(const std::string& name) {
  for (int i = 0; i < VOICE_VECTOR_CASE_COUNT; i++) {
    if (name == CASES[i].name) return &CASES[i];
  }
  return 0;
}

static uint16_t totalFrames(const VectorCase& c) {
  uint16_t total = 0;
  for (int i = 0; i < c.segmentCount; i++) total = (uint16_t)(total + c.segments[i].frames);
  return total;
}

// The pin's side: PCM -> notifications, the encoder reset at every press.
static std::vector<uint8_t> buildFrames(const VectorCase& c, const std::vector<int16_t>& pcm) {
  std::vector<uint8_t> records;
  uint8_t payload[VOICE_AUDIO_BYTES];
  size_t at = 0;
  for (int s = 0; s < c.segmentCount; s++) {
    const Segment& seg = c.segments[s];
    voiceAppendRecord(&records, payload, voicePackPress(payload, seg.utterance));
    if (seg.tap) continue;
    voiceAppendRecord(&records, payload, voicePackStart(payload, seg.utterance, VECTOR_GAIN, VOICE_START_FLAG_PREROLL));
    AdpcmState state = {0, 0};
    for (uint16_t f = 0; f < seg.frames; f++) {
      voiceAppendRecord(&records, payload, voicePackAudio(payload, seg.utterance, (uint16_t)(f + 1), state, &pcm[at]));
      at += VOICE_FRAME_SAMPLES;
    }
    voiceAppendRecord(&records, payload,
                      voicePackEnd(payload, seg.utterance, (uint16_t)(seg.frames + 1), seg.endReason, seg.frames, 0));
  }
  return records;
}

// The phone's side: every AUDIO frame decoded from its own header.
static bool decodeFrames(const std::vector<uint8_t>& records, std::vector<int16_t>* out) {
  std::vector<std::vector<uint8_t> > payloads;
  if (!voiceSplitRecords(records, &payloads)) return false;
  out->clear();
  for (size_t i = 0; i < payloads.size(); i++) {
    VoiceFrame frame;
    if (!voiceParseFrame(payloads[i].data(), (uint16_t)payloads[i].size(), &frame)) return false;
    if (frame.kind != VOICE_KIND_AUDIO) continue;
    int16_t samples[VOICE_FRAME_SAMPLES];
    voiceDecodeAudio(frame, samples);
    out->insert(out->end(), samples, samples + VOICE_FRAME_SAMPLES);
  }
  return true;
}

// ---- Generation (--write only; checks read the PCM back from the files) ----

static int16_t clampSample(double value) {
  const double rounded = floor(value + 0.5);
  return (int16_t)(rounded > 32767 ? 32767 : rounded < -32768 ? -32768 : rounded);
}

static std::vector<int16_t> generatePcm(const std::string& name, uint16_t frames) {
  const size_t count = (size_t)frames * VOICE_FRAME_SAMPLES;
  std::vector<int16_t> pcm(count, 0);
  const double pi = 3.14159265358979323846;
  if (name == "sine") {
    for (size_t i = 0; i < count; i++) pcm[i] = clampSample(12000.0 * sin(2.0 * pi * 1000.0 * (double)i / 16000.0));
  } else if (name == "step_clip") {
    // Rest, full-scale up, full-scale down (both clamps, step index to 88),
    // then rest long enough for the step index to fall back to 0.
    for (size_t i = 0; i < count; i++) pcm[i] = i < 40 ? 0 : i < 340 ? 32767 : i < 640 ? -32768 : 0;
  } else if (name == "sweep") {
    // 1 kHz swelling 4 -> 32767 over 0.5 s; 20 ms of a full-scale 500 Hz
    // square (the tone alone tops out near index 80; the square takes it to
    // 88); the tone fading back to 4 by 0.9 s; then silence. The index only
    // ever falls by 1 per sample, so the way down from 88 to 0 visits all.
    for (size_t i = 0; i < count; i++) {
      const double t = (double)i / 16000.0;
      const double tone = sin(2.0 * pi * 1000.0 * t + 0.3);
      if (t < 0.5) {
        pcm[i] = clampSample(4.0 * pow(32767.0 / 4.0, t / 0.5) * tone);
      } else if (t < 0.52) {
        pcm[i] = (i / 16) % 2 ? -32768 : 32767;
      } else if (t < 0.9) {
        pcm[i] = clampSample(32767.0 * pow(4.0 / 32767.0, (t - 0.52) / 0.38) * tone);
      }
    }
  } else if (name == "stream") {
    // Voice-like: a 140 Hz pitch with formant-weighted harmonics, a 4 Hz
    // syllable envelope and a little deterministic noise.
    const double weights[12] = {0.6, 1.0, 0.8, 0.5, 0.7, 0.9, 0.4, 0.25, 0.3, 0.2, 0.12, 0.08};
    uint32_t lcg = 12345;
    for (size_t i = 0; i < count; i++) {
      const double t = (double)i / 16000.0;
      double voiced = 0;
      for (int h = 0; h < 12; h++) voiced += weights[h] * sin(2.0 * pi * 140.0 * (h + 1) * t + 0.7 * h);
      const double envelope = 0.55 - 0.45 * cos(2.0 * pi * 4.0 * t);
      lcg = lcg * 1664525u + 1013904223u;
      const double noise = ((double)(lcg >> 16) / 65535.0 - 0.5) * 600.0;
      pcm[i] = clampSample(2600.0 * envelope * voiced + noise);
    }
  }
  return pcm;
}

static int writeVectors(const std::string& dir) {
  for (int i = 0; i < VOICE_VECTOR_CASE_COUNT; i++) {
    const VectorCase& c = CASES[i];
    const std::vector<int16_t> pcm = generatePcm(c.name, totalFrames(c));
    const std::vector<uint8_t> frames = buildFrames(c, pcm);
    std::vector<int16_t> decoded;
    if (!decodeFrames(frames, &decoded) ||
        !voiceWriteFile(voiceVectorPath(dir, c.name, "pcm"), voicePcmBytes(pcm)) ||
        !voiceWriteFile(voiceVectorPath(dir, c.name, "frames"), frames) ||
        !voiceWriteFile(voiceVectorPath(dir, c.name, "decoded"), voicePcmBytes(decoded))) {
      fprintf(stderr, "could not write the %s vectors into %s\n", c.name, dir.c_str());
      return 1;
    }
    printf("wrote adpcm_%s_{pcm,frames,decoded}.bin\n", c.name);
  }
  return 0;
}

// ---- Checks ----

static void testTables() {
  bool increasing = true;
  for (int i = 1; i <= ADPCM_STEP_INDEX_MAX; i++) increasing = increasing && ADPCM_STEP_TABLE[i] > ADPCM_STEP_TABLE[i - 1];
  expect(sizeof(ADPCM_STEP_TABLE) / sizeof(ADPCM_STEP_TABLE[0]) == 89, "the step table has 89 entries");
  expect(ADPCM_STEP_TABLE[0] == 7 && ADPCM_STEP_TABLE[88] == 32767 && increasing, "the step table runs 7..32767, increasing");
  expect(ADPCM_INDEX_TABLE[0] == -1 && ADPCM_INDEX_TABLE[4] == 2 && ADPCM_INDEX_TABLE[7] == 8 &&
             ADPCM_INDEX_TABLE[15] == 8 && ADPCM_INDEX_TABLE[11] == -1,
         "the index table is the IMA one");
}

static void testByHand() {
  // From {0, 0}: step 7, diff 100 -> nibble 0b0111, vpdiff 0 + 7 + 3 + 1 = 11.
  AdpcmState enc = {0, 0};
  expect(adpcmEncodeSample(enc, 100) == 7 && enc.predictor == 11 && enc.stepIndex == 8, "encode by hand: 100 from rest");
  AdpcmState dec = {0, 0};
  expect(adpcmDecodeNibble(dec, 7) == 11 && dec.stepIndex == 8, "decode by hand: the same state");
  // Negative: step 16 (index 8), diff -50 -> sign 8; 50 >= 16 -> 4 (34 left);
  // 34 >= 8 -> 2 (26); 26 >= 4 -> 1: nibble 15, vpdiff 2 + 16 + 8 + 4 = 30.
  expect(adpcmEncodeSample(enc, -39) == 15 && enc.predictor == 11 - (2 + 16 + 8 + 4) && enc.stepIndex == 16,
         "encode by hand: a negative step");
  // Low nibble first.
  const int16_t two[2] = {100, 100};
  uint8_t byte = 0;
  AdpcmState pair = {0, 0};
  adpcmEncode(pair, two, 2, &byte);
  expect((byte & 0x0F) == 7, "the first sample is the low nibble");
}

static void testCorruptIndex() {
  AdpcmState state = {0, 200};  // a header with a step index above 88
  const int16_t out = adpcmDecodeNibble(state, 7);
  expect(state.stepIndex == ADPCM_STEP_INDEX_MAX && out > 0, "a step index above 88 is clamped, never read out of bounds");
}

// SNR over each utterance, skipping its first ADAPT_SAMPLES: an encoder
// reset to {0, 0} mid-signal needs ~2.5 ms to reach the signal's step size
// (on the pin a hold starts from the pre-roll, not from loud speech).
static const size_t ADAPT_SAMPLES = 40;

static double snrDb(const VectorCase& c, const std::vector<int16_t>& reference, const std::vector<int16_t>& decoded) {
  double signal = 0, noise = 0;
  size_t at = 0;
  for (int s = 0; s < c.segmentCount; s++) {
    const size_t end = at + (size_t)c.segments[s].frames * VOICE_FRAME_SAMPLES;
    for (size_t i = at + ADAPT_SAMPLES; i < end && i < reference.size() && i < decoded.size(); i++) {
      const double r = reference[i], d = (double)decoded[i] - reference[i];
      signal += r * r;
      noise += d * d;
    }
    at = end;
  }
  return noise == 0 ? 999.0 : 10.0 * log10(signal / noise);
}

static void checkCase(const std::string& dir, const VectorCase& c) {
  std::vector<uint8_t> pcmBytes, frames, decodedBytes;
  std::vector<int16_t> pcm, expectedDecoded, decoded;
  const std::string name = c.name;
  const bool read = voiceReadFile(voiceVectorPath(dir, name, "pcm"), &pcmBytes) &&
                    voiceReadFile(voiceVectorPath(dir, name, "frames"), &frames) &&
                    voiceReadFile(voiceVectorPath(dir, name, "decoded"), &decodedBytes) &&
                    voicePcmFromBytes(pcmBytes, &pcm) && voicePcmFromBytes(decodedBytes, &expectedDecoded);
  expect(read, ("vectors present: " + name).c_str());
  if (!read) return;
  expect(pcm.size() == (size_t)totalFrames(c) * VOICE_FRAME_SAMPLES, ("PCM length: " + name).c_str());
  if (pcm.size() != (size_t)totalFrames(c) * VOICE_FRAME_SAMPLES) return;
  expect(buildFrames(c, pcm) == frames, ("encoder output byte-identical to the golden frames: " + name).c_str());
  expect(decodeFrames(frames, &decoded) && decoded == expectedDecoded,
         ("decoder output identical to the golden PCM: " + name).c_str());

  // Self-contained frames: a continuous decode (state carried from frame to
  // frame within an utterance) equals the per-frame decode from headers.
  std::vector<std::vector<uint8_t> > payloads;
  voiceSplitRecords(frames, &payloads);
  std::vector<int16_t> continuous;
  AdpcmState carried = {0, 0};
  bool headersMatch = true;
  for (size_t i = 0; i < payloads.size(); i++) {
    VoiceFrame frame;
    voiceParseFrame(payloads[i].data(), (uint16_t)payloads[i].size(), &frame);
    if (frame.kind == VOICE_KIND_START) carried.predictor = 0, carried.stepIndex = 0;
    if (frame.kind != VOICE_KIND_AUDIO) continue;
    headersMatch = headersMatch && frame.state.predictor == carried.predictor && frame.state.stepIndex == carried.stepIndex;
    int16_t samples[VOICE_FRAME_SAMPLES];
    adpcmDecode(carried, frame.data, VOICE_FRAME_DATA_BYTES, samples);
    continuous.insert(continuous.end(), samples, samples + VOICE_FRAME_SAMPLES);
  }
  expect(headersMatch && continuous == expectedDecoded,
         ("each AUDIO header is the state before its first nibble: " + name).c_str());

  if (name == "sine") expect(snrDb(c, pcm, decoded) >= 20.0, "1 kHz sine round trip SNR >= 20 dB");
  if (name == "stream") expect(snrDb(c, pcm, decoded) >= 20.0, "voice-like round trip SNR >= 20 dB");
  if (name == "silence") {
    bool zero = true;
    for (size_t i = 0; i < decoded.size(); i++) zero = zero && decoded[i] == 0;
    expect(zero, "silence decodes to exact zeros");
  }
  if (name == "sweep") {
    bool visited[ADPCM_STEP_INDEX_MAX + 1] = {false};
    for (size_t i = 0; i < payloads.size(); i++) {
      VoiceFrame frame;
      voiceParseFrame(payloads[i].data(), (uint16_t)payloads[i].size(), &frame);
      if (frame.kind != VOICE_KIND_AUDIO) continue;
      AdpcmState state = frame.state;
      visited[state.stepIndex] = true;
      for (uint16_t b = 0; b < VOICE_FRAME_DATA_BYTES; b++) {
        adpcmDecodeNibble(state, (uint8_t)(frame.data[b] & 0x0F));
        visited[state.stepIndex] = true;
        adpcmDecodeNibble(state, (uint8_t)(frame.data[b] >> 4));
        visited[state.stepIndex] = true;
      }
    }
    int count = 0;
    for (int i = 0; i <= ADPCM_STEP_INDEX_MAX; i++) count += visited[i];
    printf("  sweep visits %d of 89 step indexes\n", count);
    expect(count == ADPCM_STEP_INDEX_MAX + 1, "the sweep vector uses every step table entry");
  }
  if (name == "step_clip") {
    bool hitTop = false, hitBottom = false, hit88 = false, backTo0 = false;
    AdpcmState state = {0, 0};
    for (size_t i = 0; i < payloads.size(); i++) {
      VoiceFrame frame;
      voiceParseFrame(payloads[i].data(), (uint16_t)payloads[i].size(), &frame);
      if (frame.kind != VOICE_KIND_AUDIO) continue;
      for (uint16_t b = 0; b < VOICE_FRAME_DATA_BYTES; b++) {
        for (int half = 0; half < 2; half++) {
          const int16_t sample = adpcmDecodeNibble(state, (uint8_t)(half ? frame.data[b] >> 4 : frame.data[b] & 0x0F));
          hitTop = hitTop || sample == 32767;
          hitBottom = hitBottom || sample == -32768;
          hit88 = hit88 || state.stepIndex == ADPCM_STEP_INDEX_MAX;
          backTo0 = backTo0 || (hit88 && state.stepIndex == 0);
        }
      }
    }
    expect(hitTop && hitBottom, "the predictor saturates at 32767 and at -32768");
    expect(hit88 && backTo0, "the step index clamps at 88 and falls back to 0");
  }
}

int main(int argc, char** argv) {
  if (argc == 3 && strcmp(argv[1], "--write") == 0) return writeVectors(argv[2]);
  if (argc != 2) {
    fprintf(stderr, "usage: test_adpcm <vectors dir> | --write <vectors dir>\n");
    return 2;
  }
  testTables();
  testByHand();
  testCorruptIndex();
  for (int i = 0; i < VOICE_VECTOR_CASE_COUNT; i++) {
    expect(findCase(VOICE_VECTOR_CASES[i]) == &CASES[i], "the case list matches voice_vectors.h");
    checkCase(argv[1], CASES[i]);
  }
  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("adpcm: all passed\n");
  return 0;
}

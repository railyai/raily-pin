// Host tests for voice_frame.h (docs/pins/voice-from-pin.md §4 «Frame
// format»). Usage: test_voice_frame <vectors dir>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "../RailyPinsP1/voice_frame.h"
#include "voice_vectors.h"

static int failures = 0;

static void expect(bool ok, const char* what) {
  printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) failures++;
}

static void testPress() {
  uint8_t out[VOICE_AUDIO_BYTES];
  const uint8_t expected[4] = {4, 9, 0, 0};
  expect(voicePackPress(out, 9) == 4 && memcmp(out, expected, 4) == 0, "PRESS: kind 4, utterance, seq 0, 4 bytes");
}

static void testStart() {
  uint8_t out[VOICE_AUDIO_BYTES];
  // kind, utterance, seq u16, codec, 16000 LE (0x3E80), 20 ms, gain -3 (i8), flags.
  const uint8_t expected[10] = {1, 200, 0, 0, 1, 0x80, 0x3E, 20, 0xFD, 1};
  expect(voicePackStart(out, 200, -3, VOICE_START_FLAG_PREROLL) == 10 && memcmp(out, expected, 10) == 0,
         "START: 10 bytes, little-endian, gain signed");
  VoiceFrame frame;
  expect(voiceParseFrame(out, 10, &frame) && frame.kind == VOICE_KIND_START && frame.utterance == 200 &&
             frame.seq == 0 && frame.sampleRate == 16000 && frame.frameMs == 20 && frame.gainHalfDb == -3 &&
             frame.flags == 1,
         "START parses back");
  uint8_t other[10];
  memcpy(other, out, 10);
  other[5] = 0x40;  // 16 000 -> 16 192: another rate
  expect(!voiceParseFrame(other, 10, &frame), "START at another sample rate is rejected");
  memcpy(other, out, 10);
  other[7] = 10;
  expect(!voiceParseFrame(other, 10, &frame), "START with another frame length is rejected");
  memcpy(other, out, 10);
  other[4] = 0;
  expect(!voiceParseFrame(other, 10, &frame), "START with codec 0 (reserved) is rejected");
}

static void testAudio() {
  int16_t pcm[VOICE_FRAME_SAMPLES];
  for (int i = 0; i < VOICE_FRAME_SAMPLES; i++) pcm[i] = (int16_t)((i % 32) * 500 - 8000);
  AdpcmState state = {-1234, 37};
  const AdpcmState before = state;
  uint8_t out[VOICE_AUDIO_BYTES + 4];
  memset(out, 0xEE, sizeof(out));
  expect(voicePackAudio(out, 7, 0x0102, state, pcm) == 168 && out[168] == 0xEE, "AUDIO is exactly 168 bytes");
  expect(out[0] == 2 && out[1] == 7 && out[2] == 0x02 && out[3] == 0x01 && out[4] == 1,
         "AUDIO header: kind 2, utterance, seq LE, codec 1");
  expect(out[5] == (uint8_t)(-1234 & 0xFF) && out[6] == (uint8_t)((uint16_t)-1234 >> 8) && out[7] == 37,
         "AUDIO carries the state before the first nibble: predictor i16 LE, then step_index");
  expect(state.predictor != before.predictor || state.stepIndex != before.stepIndex, "the encoder state moves on");

  VoiceFrame frame;
  expect(voiceParseFrame(out, 168, &frame) && frame.kind == VOICE_KIND_AUDIO && frame.seq == 0x0102 &&
             frame.state.predictor == -1234 && frame.state.stepIndex == 37 && frame.data == out + 8,
         "AUDIO parses back");
  int16_t decoded[VOICE_FRAME_SAMPLES];
  voiceDecodeAudio(frame, decoded);
  AdpcmState again = before;
  int16_t direct[VOICE_FRAME_SAMPLES];
  adpcmDecode(again, out + 8, VOICE_FRAME_DATA_BYTES, direct);
  expect(memcmp(decoded, direct, sizeof(decoded)) == 0 && again.predictor == state.predictor &&
             again.stepIndex == state.stepIndex,
         "decoding from the header ends where the encoder ended");

  expect(!voiceParseFrame(out, 167, &frame) && !voiceParseFrame(out, 169, &frame), "short or long AUDIO is rejected");
  uint8_t bad[VOICE_AUDIO_BYTES];
  memcpy(bad, out, sizeof(bad));
  bad[7] = 89;
  expect(!voiceParseFrame(bad, 168, &frame), "AUDIO with a step index above 88 is rejected");
  memcpy(bad, out, sizeof(bad));
  bad[4] = 2;
  expect(!voiceParseFrame(bad, 168, &frame), "AUDIO with an unknown codec is rejected");
}

static void testEnd() {
  uint8_t out[VOICE_AUDIO_BYTES];
  const uint8_t expected[9] = {3, 0, 0xB9, 0x0B, 2, 0xB8, 0x0B, 0x01, 0x00};
  expect(voicePackEnd(out, 0, 3001, VOICE_END_OVERFLOW, 3000, 1) == 9 && memcmp(out, expected, 9) == 0,
         "END: seq n + 1, reason, frames_sent, frames_dropped, LE");
  VoiceFrame frame;
  expect(voiceParseFrame(out, 9, &frame) && frame.kind == VOICE_KIND_END && frame.seq == 3001 && frame.reason == 2 &&
             frame.framesSent == 3000 && frame.framesDropped == 1,
         "END parses back");
}

static void testRejects() {
  VoiceFrame frame;
  const uint8_t unknown[4] = {5, 0, 0, 0};
  const uint8_t zero[4] = {0, 0, 0, 0};
  const uint8_t pressLong[5] = {4, 0, 0, 0, 0};
  expect(!voiceParseFrame(unknown, 4, &frame) && !voiceParseFrame(zero, 4, &frame), "unknown kinds are rejected");
  expect(!voiceParseFrame(pressLong, 5, &frame) && !voiceParseFrame(pressLong, 3, &frame),
         "a PRESS of another length is rejected");
  expect(!voiceParseFrame(0, 4, &frame) && !voiceParseFrame(unknown, 4, 0), "null buffers are rejected");
  expect(VOICE_MIN_ATT_MTU == 171, "an AUDIO notify needs ATT MTU 171");
  expect(VOICE_FRAME_SAMPLES == VOICE_SAMPLE_RATE / 1000 * VOICE_FRAME_MS && VOICE_FRAME_DATA_BYTES == 160,
         "a frame is 20 ms at 16 kHz = 320 samples in 160 bytes");
}

// The stream vector: what every parser (Swift, voice_capture.py) must read.
static void testStreamVector(const std::string& dir) {
  std::vector<uint8_t> records;
  std::vector<std::vector<uint8_t> > payloads;
  const bool read = voiceReadFile(voiceVectorPath(dir, "stream", "frames"), &records) &&
                    voiceSplitRecords(records, &payloads);
  expect(read, "the stream vector reads");
  if (!read) return;
  std::string kinds;
  bool allParse = true;
  std::vector<VoiceFrame> frames;
  for (size_t i = 0; i < payloads.size(); i++) {
    VoiceFrame frame;
    allParse = allParse && voiceParseFrame(payloads[i].data(), (uint16_t)payloads[i].size(), &frame);
    frames.push_back(frame);
    kinds += "?SAEP"[frame.kind <= 4 ? frame.kind : 0];
  }
  expect(allParse, "every stream record parses");
  expect(kinds == "PPSAAAAAAEPSAAAAE", "stream: a tap (PRESS only), then PRESS START AUDIO*6 END, PRESS START AUDIO*4 END");
  if (kinds != "PPSAAAAAAEPSAAAAE") return;
  expect(frames[0].utterance == 254 && frames[1].utterance == 255 && frames[9].utterance == 255 &&
             frames[10].utterance == 0 && frames[16].utterance == 0,
         "the utterance id wraps 254, 255, 0");
  bool seqOk = frames[2].seq == 0 && frames[11].seq == 0;
  for (int i = 0; i < 6; i++) seqOk = seqOk && frames[3 + i].seq == i + 1;
  for (int i = 0; i < 4; i++) seqOk = seqOk && frames[12 + i].seq == i + 1;
  seqOk = seqOk && frames[9].seq == 7 && frames[16].seq == 5;
  expect(seqOk, "seq restarts at 0 on every START, AUDIO 1..n, END n + 1");
  expect(frames[9].framesSent == 6 && frames[16].framesSent == 4 && frames[16].reason == VOICE_END_CAP &&
             frames[9].reason == VOICE_END_RELEASED,
         "END carries frames_sent and the reason");
  expect(frames[3].state.predictor == 0 && frames[3].state.stepIndex == 0 && frames[12].state.predictor == 0 &&
             frames[12].state.stepIndex == 0,
         "the encoder starts every utterance from {0, 0}");
}

int main(int argc, char** argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: test_voice_frame <vectors dir>\n");
    return 2;
  }
  testPress();
  testStart();
  testAudio();
  testEnd();
  testRejects();
  testStreamVector(argv[1]);
  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("voice_frame: all passed\n");
  return 0;
}

// Host tests for voice_ring.h (docs/pins/voice-from-pin.md §4 «Sequence,
// loss, and flow control», §10 V1). Usage: test_voice_ring <vectors dir>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "../RailyPinsP1/voice_ring.h"
#include "voice_vectors.h"

static int failures = 0;

static void expect(bool ok, const char* what) {
  printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) failures++;
}

static int16_t pcm[VOICE_FRAME_SAMPLES];

static void fillPcm(int16_t value) {
  for (int i = 0; i < VOICE_FRAME_SAMPLES; i++) pcm[i] = (int16_t)(value + i);
}

template <uint16_t N>
static std::vector<VoiceFrame> drain(VoiceUplink<N>& up, std::vector<std::vector<uint8_t> >* raw = 0) {
  std::vector<VoiceFrame> frames;
  uint8_t payload[VOICE_AUDIO_BYTES];
  uint8_t length;
  while ((length = up.peek(payload)) != 0) {
    VoiceFrame frame;
    voiceParseFrame(payload, length, &frame);
    frame.data = 0;
    frames.push_back(frame);
    if (raw) raw->push_back(std::vector<uint8_t>(payload, payload + length));
    up.consume();
  }
  return frames;
}

static void testRing() {
  static VoiceRing<3> ring;
  memset(&ring, 0, sizeof(ring));
  uint8_t frame[VOICE_AUDIO_BYTES];
  bool fifo = true;
  for (int round = 0; round < 5; round++) {  // the indexes wrap around
    for (uint8_t i = 0; i < 3; i++) {
      frame[0] = (uint8_t)(round * 3 + i);
      fifo = fifo && ring.push(frame);
    }
    frame[0] = 0xEE;
    fifo = fifo && !ring.push(frame) && ring.full();
    for (uint8_t i = 0; i < 3; i++) {
      fifo = fifo && ring.front()[0] == (uint8_t)(round * 3 + i);
      ring.pop();
    }
    fifo = fifo && ring.empty() && ring.front() == 0;
  }
  expect(fifo, "the ring is FIFO across index wrap and refuses a frame when full");
  ring.pop();
  expect(ring.empty(), "pop on an empty ring is harmless");
}

static void testTalk() {
  static VoiceUplink<VOICE_RING_FRAMES> up;
  memset(&up, 0, sizeof(up));
  up.press();
  std::vector<VoiceFrame> f = drain(up);
  expect(f.size() == 1 && f[0].kind == VOICE_KIND_PRESS && f[0].utterance == 1, "press: PRESS goes at once, utterance 1");
  fillPcm(5000);
  for (int i = 0; i < VOICE_WARMUP_FRAMES; i++) up.capture(pcm);
  expect(up.ring.empty(), "the 2 warm-up frames are discarded");
  for (int i = 0; i < 5; i++) up.capture(pcm);
  expect(drain(up).empty() && up.ring.count == 5, "the pre-roll is held back until START");
  up.start(40);
  for (int i = 0; i < 3; i++) up.capture(pcm);
  up.finish(VOICE_END_RELEASED);
  expect(up.capture(pcm) && up.ring.count == 8, "a frame after the end is ignored, not queued");
  f = drain(up);
  bool order = f.size() == 10 && f[0].kind == VOICE_KIND_START && f[0].flags == VOICE_START_FLAG_PREROLL &&
               f[0].gainHalfDb == 40 && f[9].kind == VOICE_KIND_END;
  for (size_t i = 1; order && i < 9; i++) order = f[i].kind == VOICE_KIND_AUDIO && f[i].seq == i;
  expect(order, "START (pre-roll flag), AUDIO 1..8 with the pre-roll first, then END");
  expect(f.size() == 10 && f[9].seq == 9 && f[9].reason == 0 && f[9].framesSent == 8 && f[9].framesDropped == 0,
         "END: seq n + 1, frames_sent n, nothing dropped, warm-up never counted");
  expect(up.phase == VOICE_UPLINK_IDLE && drain(up).empty(), "after END: idle, nothing more");

  up.press();
  f = drain(up);
  expect(f.size() == 1 && f[0].utterance == 2, "the next hold has the next utterance id");
  up.start(0);
  f = drain(up);
  expect(f.size() == 1 && f[0].flags == 0, "a START with no pre-roll captured has flags 0");
}

static void testTapWipes() {
  static VoiceUplink<VOICE_RING_FRAMES> up;
  memset(&up, 0, sizeof(up));
  up.press();
  drain(up);
  fillPcm(-300);
  for (int i = 0; i < 10; i++) up.capture(pcm);
  up.drop();  // VOICE_ACT_DROP_AUDIO: a tap
  expect(up.ring.empty() && drain(up).empty() && !up.capturing(), "a tap wipes the pre-roll: nothing leaves the pin");
  up.start(40);
  expect(drain(up).empty(), "a START after a wipe is ignored (no utterance is open)");

  // A tap before PRESS went out: the PRESS is dropped too.
  up.press();
  up.drop();
  expect(drain(up).empty(), "an unsent PRESS is dropped with the tap");
}

static void testOverflow() {
  static VoiceUplink<4> up;
  memset(&up, 0, sizeof(up));
  up.press();
  up.start(40);
  drain(up);
  fillPcm(100);
  for (int i = 0; i < VOICE_WARMUP_FRAMES; i++) up.capture(pcm);
  bool fits = true;
  for (int i = 0; i < 4; i++) fits = fits && up.capture(pcm);
  expect(fits && !up.capture(pcm) && up.overflowed && up.current.framesDropped == 1, "a full ring refuses the next frame");
  uint8_t payload[VOICE_AUDIO_BYTES];
  up.peek(payload);  // the link sends AUDIO 1 meanwhile
  up.consume();
  expect(!up.capture(pcm) && up.ring.count == 3, "after an overflow nothing more is taken: never a hole mid-stream");
  up.finish(VOICE_END_OVERFLOW);
  std::vector<VoiceFrame> f = drain(up);
  bool contiguous = f.size() == 4;
  for (size_t i = 0; contiguous && i < 3; i++) contiguous = f[i].kind == VOICE_KIND_AUDIO && f[i].seq == i + 2;
  expect(contiguous, "what was queued still goes, in order");
  expect(f.size() == 4 && f[3].kind == VOICE_KIND_END && f[3].reason == VOICE_END_OVERFLOW && f[3].seq == 5 &&
             f[3].framesSent == 4 && f[3].framesDropped == 2,
         "then END 2 with frames_dropped");
}

static void testStopAndLinkLoss() {
  static VoiceUplink<VOICE_RING_FRAMES> up;
  memset(&up, 0, sizeof(up));
  up.press();
  up.start(40);
  fillPcm(7);
  for (int i = 0; i < VOICE_WARMUP_FRAMES + 6; i++) up.capture(pcm);
  uint8_t payload[VOICE_AUDIO_BYTES];
  for (int i = 0; i < 4; i++) {  // PRESS, START, AUDIO 1, AUDIO 2
    up.peek(payload);
    up.consume();
  }
  up.finish(VOICE_END_PHONE_STOP);
  std::vector<VoiceFrame> f = drain(up);
  expect(f.size() == 1 && f[0].kind == VOICE_KIND_END && f[0].reason == 4 && f[0].seq == 3 && f[0].framesSent == 2,
         "phone STOP: the queue is dropped, END 4 right after the last frame sent");

  up.press();
  up.start(40);
  for (int i = 0; i < VOICE_WARMUP_FRAMES + 6; i++) up.capture(pcm);
  up.peek(payload);
  up.consume();
  up.reset();  // the link went down
  expect(drain(up).empty() && !up.capturing(), "a lost link drops everything");
  up.press();
  f = drain(up);
  expect(f.size() == 1 && f[0].kind == VOICE_KIND_PRESS && f[0].utterance == 3,
         "after a reconnect only the new utterance goes: no stale audio is replayed");
}

// Codex on #1425: a new hold while the last utterance still drains must not
// cut it short.
static void testQuickRetry() {
  static VoiceUplink<VOICE_RING_FRAMES> up;
  memset(&up, 0, sizeof(up));
  fillPcm(900);
  up.press();
  up.start(40);
  for (int i = 0; i < VOICE_WARMUP_FRAMES + 5; i++) up.capture(pcm);
  uint8_t payload[VOICE_AUDIO_BYTES];
  for (int i = 0; i < 3; i++) {  // PRESS, START, AUDIO 1: then the link stalls
    up.peek(payload);
    up.consume();
  }
  up.finish(VOICE_END_RELEASED);
  expect(up.press(), "a new hold while the last utterance drains keeps it");
  for (int i = 0; i < VOICE_WARMUP_FRAMES + 2; i++) up.capture(pcm);  // the new pre-roll
  up.start(40);
  up.capture(pcm);
  up.finish(VOICE_END_RELEASED);
  std::vector<VoiceFrame> f = drain(up);
  std::string kinds;
  for (size_t i = 0; i < f.size(); i++) kinds += "?SAEP"[f[i].kind];
  expect(kinds == "AAAAEPSAAAE", "the old utterance's frames and END go first, then the new PRESS, START, audio, END");
  bool ok = kinds == "AAAAEPSAAAE";
  for (size_t i = 0; ok && i < 4; i++) ok = f[i].utterance == 1 && f[i].seq == i + 2;
  ok = ok && f[4].utterance == 1 && f[4].seq == 6 && f[4].framesSent == 5 && f[4].reason == 0;
  for (size_t i = 7; ok && i < 10; i++) ok = f[i].utterance == 2 && f[i].seq == i - 6;
  ok = ok && f[10].utterance == 2 && f[10].seq == 4 && f[10].framesSent == 3;
  expect(ok, "each utterance keeps its own seq, frames_sent and END");

  // A tap during the drain drops only the tap.
  memset(&up, 0, sizeof(up));
  up.press();
  up.start(40);
  for (int i = 0; i < VOICE_WARMUP_FRAMES + 2; i++) up.capture(pcm);
  up.finish(VOICE_END_RELEASED);
  up.press();
  for (int i = 0; i < VOICE_WARMUP_FRAMES + 3; i++) up.capture(pcm);
  up.drop();
  f = drain(up);
  kinds.clear();
  for (size_t i = 0; i < f.size(); i++) kinds += "?SAEP"[f[i].kind];
  expect(kinds == "PSAAE", "a tap during the drain loses only its own pre-roll (its unsent PRESS too)");

  // A third hold while two utterances still wait: the older audio is given up.
  memset(&up, 0, sizeof(up));
  up.press();
  up.start(40);
  for (int i = 0; i < VOICE_WARMUP_FRAMES + 2; i++) up.capture(pcm);
  up.finish(VOICE_END_RELEASED);
  up.press();
  up.start(40);
  for (int i = 0; i < VOICE_WARMUP_FRAMES + 2; i++) up.capture(pcm);
  up.finish(VOICE_END_RELEASED);
  expect(!up.press(), "a third hold with two queued gives up the older audio (reported)");
  f = drain(up);
  expect(f.size() == 1 && f[0].kind == VOICE_KIND_PRESS && f[0].utterance == 3, "then only the new utterance goes");

  // A phone STOP keeps the previous utterance's frames.
  memset(&up, 0, sizeof(up));
  up.press();
  up.start(40);
  for (int i = 0; i < VOICE_WARMUP_FRAMES + 2; i++) up.capture(pcm);
  up.finish(VOICE_END_RELEASED);
  up.press();
  up.start(40);
  for (int i = 0; i < VOICE_WARMUP_FRAMES + 3; i++) up.capture(pcm);
  up.finish(VOICE_END_PHONE_STOP);
  f = drain(up);
  kinds.clear();
  for (size_t i = 0; i < f.size(); i++) kinds += "?SAEP"[f[i].kind];
  expect(kinds == "PSAAEPSE" && f[7].reason == VOICE_END_PHONE_STOP && f[7].framesSent == 0,
         "a STOP of the new utterance drops only its own queue");
}

// The uplink, driven like the pin, emits the golden stream byte for byte.
static void testGoldenStream(const std::string& dir) {
  std::vector<uint8_t> pcmBytes, golden;
  std::vector<int16_t> source;
  const bool read = voiceReadFile(voiceVectorPath(dir, "stream", "pcm"), &pcmBytes) &&
                    voiceReadFile(voiceVectorPath(dir, "stream", "frames"), &golden) &&
                    voicePcmFromBytes(pcmBytes, &source) && source.size() == 10u * VOICE_FRAME_SAMPLES;
  expect(read, "the stream vector reads");
  if (!read) return;
  static VoiceUplink<VOICE_RING_FRAMES> up;
  memset(&up, 0, sizeof(up));
  up.utterance = 253;
  std::vector<std::vector<uint8_t> > sent;
  up.press();  // 254: a tap
  drain(up, &sent);
  up.drop();
  const int talks[2] = {6, 4};
  const uint8_t reasons[2] = {VOICE_END_RELEASED, VOICE_END_CAP};
  size_t at = 0;
  for (int t = 0; t < 2; t++) {
    up.press();  // 255, then 0
    drain(up, &sent);
    fillPcm(12345);  // warm-up noise: discarded, never sent
    for (int i = 0; i < VOICE_WARMUP_FRAMES; i++) up.capture(pcm);
    up.capture(&source[at]);  // one frame of pre-roll
    at += VOICE_FRAME_SAMPLES;
    up.start(40);
    for (int i = 1; i < talks[t]; i++) {
      up.capture(&source[at]);
      at += VOICE_FRAME_SAMPLES;
      drain(up, &sent);
    }
    up.finish(reasons[t]);
    drain(up, &sent);
  }
  std::vector<uint8_t> records;
  for (size_t i = 0; i < sent.size(); i++) voiceAppendRecord(&records, sent[i].data(), (uint16_t)sent[i].size());
  expect(records == golden, "the uplink emits adpcm_stream_frames.bin byte for byte (ids 254, 255, 0)");
}

static void testSizing() {
  expect(VOICE_RING_FRAMES == VOICE_TALK_HOLD_MS / VOICE_FRAME_MS + 80, "the ring holds the pre-roll plus 1.6 s");
  expect(sizeof(VoiceRing<VOICE_RING_FRAMES>) <= (size_t)VOICE_RING_FRAMES * VOICE_AUDIO_BYTES + 8,
         "the ring is the frames and two counters");
  if (VOICE_TALK_HOLD_MS == 400) expect(VOICE_RING_FRAMES == 100, "100 frames (16.8 KB) at the doc's 0.4 s");
  if (VOICE_TALK_HOLD_MS == 2000) expect(VOICE_RING_FRAMES == 180, "180 frames (30.2 KB) at the owner's 2 s");
}

int main(int argc, char** argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: test_voice_ring <vectors dir>\n");
    return 2;
  }
  testRing();
  testTalk();
  testTapWipes();
  testOverflow();
  testStopAndLinkLoss();
  testQuickRetry();
  testGoldenStream(argv[1]);
  testSizing();
  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("voice_ring: all passed\n");
  return 0;
}

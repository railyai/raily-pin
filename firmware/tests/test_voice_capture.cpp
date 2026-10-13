// Host tests for voice_capture.h: the PDM-to-loop() PCM FIFO and the
// high-pass before the encoder (docs/pins/voice-from-pin.md §2).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "../RailyPinsP1/voice_capture.h"

static int failures = 0;

static void expect(bool ok, const char* what) {
  printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) failures++;
}

static void fill(int16_t* pcm, int16_t value) {
  for (uint16_t i = 0; i < VOICE_FRAME_SAMPLES; i++) pcm[i] = value;
}

static void testFifo() {
  static VoicePcmFifo<VOICE_PCM_FIFO_FRAMES> fifo;
  fifo.reset();
  int16_t pcm[VOICE_FRAME_SAMPLES];
  expect(fifo.empty() && fifo.front() == 0, "a fresh FIFO is empty");
  bool fits = true;
  for (int i = 0; i < VOICE_PCM_FIFO_FRAMES - 1; i++) {
    fill(pcm, (int16_t)(i + 1));
    fits = fits && fifo.push(pcm);
  }
  expect(fits, "N - 1 frames fit");
  fill(pcm, 99);
  expect(!fifo.push(pcm) && fifo.overruns == 1, "a full FIFO drops the new frame and counts it");
  expect(fifo.pushed == VOICE_PCM_FIFO_FRAMES - 1, "only taken frames count as pushed");
  bool inOrder = true;
  for (int i = 0; i < VOICE_PCM_FIFO_FRAMES - 1; i++) {
    const int16_t* front = fifo.front();
    inOrder = inOrder && front && front[0] == i + 1 && front[VOICE_FRAME_SAMPLES - 1] == i + 1;
    fifo.pop();
  }
  expect(inOrder && fifo.empty(), "frames come out in order, whole, and the FIFO drains");
  fifo.pop();
  expect(fifo.empty(), "a pop on an empty FIFO is harmless");
  bool wrapOk = true;
  for (int i = 0; i < 1000; i++) {
    fill(pcm, (int16_t)i);
    wrapOk = wrapOk && fifo.push(pcm) && fifo.front()[0] == (int16_t)i;
    fifo.pop();
  }
  expect(wrapOk, "1000 frames through the wrap");
  fifo.reset();
  expect(fifo.empty() && fifo.pushed == 0 && fifo.overruns == 0, "reset clears the counts");
}

static double rms(const int16_t* pcm, int count) {
  double sum = 0;
  for (int i = 0; i < count; i++) sum += (double)pcm[i] * pcm[i];
  return sqrt(sum / count);
}

static void testHighPass() {
  VoiceHighPass hpf;
  hpf.reset();
  static int16_t pcm[16000];
  for (int i = 0; i < 16000; i++) pcm[i] = 2000;
  hpf.run(pcm, 16000);
  expect(abs(pcm[1599]) < 50 && abs(pcm[15999]) <= 1, "DC (2000) is removed: under 50 after 100 ms, 0 after 1 s");
  hpf.reset();
  for (int i = 0; i < 16000; i++) pcm[i] = (int16_t)lround(8000.0 * sin(2 * M_PI * 1000.0 * i / 16000.0) + 1500);
  hpf.run(pcm, 16000);
  const double r = rms(pcm + 8000, 8000);
  expect(r > 8000 / sqrt(2.0) * 0.98 && r < 8000 / sqrt(2.0) * 1.02, "1 kHz passes within 2 %, the 1500 offset is gone");
  hpf.reset();
  for (int i = 0; i < 16000; i++) pcm[i] = (int16_t)lround(8000.0 * sin(2 * M_PI * 50.0 * i / 16000.0));
  hpf.run(pcm, 16000);
  expect(rms(pcm + 8000, 8000) < 8000 / sqrt(2.0) * 0.5, "50 Hz hum is attenuated by more than 6 dB");
  // Full-scale square waves: every rising step must come out positive (a
  // wrap would flip it negative).
  hpf.reset();
  for (int i = 0; i < 16000; i++) pcm[i] = (i / 8) % 2 ? 32767 : -32768;
  hpf.run(pcm, 16000);
  bool bounded = true;
  for (int i = 8; i < 16000; i += 16) bounded = bounded && pcm[i] > 0;
  expect(bounded, "full-scale steps clamp instead of wrapping");
  VoiceHighPass whole, framed;
  whole.reset();
  framed.reset();
  static int16_t a[3200], b[3200];
  for (int i = 0; i < 3200; i++) a[i] = b[i] = (int16_t)((i * 7919) % 20000 - 10000);
  whole.run(a, 3200);
  for (int f = 0; f < 10; f++) framed.run(b + f * 320, 320);
  expect(memcmp(a, b, sizeof(a)) == 0, "frame by frame equals one long run (the state carries over)");
}

// The 2 s talk hold (owner, 2026-10-09): at START the person is already
// mid-sentence (the 2 s pre-roll holds the start of the question), so the
// cue must not cut audio out of a word. Bench 2026-10-09: the 200 ms zeroed
// window turned «в колоде» into «в коде» in the transcript.
static void testStartCue() {
  VoiceStartCue cue = {};
  const bool motor = cue.start(10000, 2000);
  bool zeroed = false;
  for (uint32_t t = 10000; t < 10400; t += 20) zeroed = zeroed || cue.zeroes(t);
  expect(!zeroed, "talk hold 2 s: no frame after START is zeroed (the speech goes on through START)");
  expect(!motor, "talk hold 2 s: no motor tick at START (it would be in the audio)");
  VoiceStartCue early = {};
  expect(early.start(10000, 400), "talk hold 0.4 s (the doc's original): the START tick plays");
  expect(early.zeroes(10000) && early.zeroes(10000 + VOICE_START_CUE_ZERO_MS - 1) &&
             !early.zeroes(10000 + VOICE_START_CUE_ZERO_MS),
         "talk hold 0.4 s: the tick's frames are zeroed for VOICE_START_CUE_ZERO_MS, then never again");
  VoiceStartCue idle = {};
  expect(!idle.zeroes(0) && !idle.zeroes(0xFFFFFFF0u), "no START, nothing zeroed");
}

int main() {
  testFifo();
  testHighPass();
  testStartCue();
  if (failures) {
    printf("voice_capture: %d failure(s)\n", failures);
    return 1;
  }
  printf("voice_capture: all passed\n");
  return 0;
}

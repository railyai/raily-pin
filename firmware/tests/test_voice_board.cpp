// Host tests for voice_board.h (docs/pins/voice-from-pin.md §2): the
// per-board mic target the portable voice logic does not know about.
#include <stdio.h>

#include "../RailyPinsP1/voice_board.h"
#include "../RailyPinsP1/voice_frame.h"

static int failures = 0;

static void expect(bool ok, const char* what) {
  printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) failures++;
}

int main() {
  const VoicePdmConfig& p1 = VOICE_PDM_XIAO_SENSE;
  expect(p1.present, "P1 (XIAO nRF52840 Sense) has the PDM mic");
  expect(p1.dataPin == 0 * 32 + 16 && p1.clkPin == 1 * 32 + 0 && p1.powerPin == 1 * 32 + 10 && p1.powerActiveHigh,
         "P1 pins: DATA P0.16, CLK P1.00, power P1.10 driven high (nRF pin numbers)");
  expect(p1.pdmClkCtrl == 0x0A000000UL && p1.clockHz == 32000000UL / 25 && p1.ratio == 80,
         "P1 clock: PDMCLKCTRL 1.280 MHz (32 MHz / 25), RATIO 80");
  expect(voicePdmSampleRate(p1) == VOICE_SAMPLE_RATE, "P1 samples at exactly 16 000 Hz, the frame format's rate");
  expect(voicePdmGainHalfDb(p1.gainRegister) == 40 && voicePdmGainHalfDb(0x28) == 0 && voicePdmGainHalfDb(0x00) == -40,
         "the START gain byte is the GAIN register in 0.5 dB units (+20 dB = 40)");
  expect(p1.leftFalling, "P1 is mono, left channel, as the Seeed mic library sets it");

  expect(!p1.hardwareVerified, "P1 is from vendor sources, not yet captured on our bench (V0a)");

  const VoicePdmConfig& touch = VOICE_PDM_XIAO147;
  expect(touch.present && touch.clkPin == 0 * 32 + 2 && touch.dataPin == 0 * 32 + 3,
         "XIAO 1.47\" touch board: CLK D0 = P0.02, DATA D1 = P0.03, assigned explicitly");
  expect(touch.powerPin == VOICE_PDM_NO_PIN, "XIAO 1.47\": mic VDD is hard-wired, no power pin to drive");
  expect(touch.clkPin != 9 && touch.clkPin != 10 && touch.dataPin != 9 && touch.dataPin != 10,
         "XIAO 1.47\": the mic is not on the NFC pins (P0.09/P0.10 = D14/D15)");
  expect(voicePdmSampleRate(touch) == VOICE_SAMPLE_RATE && touch.leftFalling,
         "XIAO 1.47\": 16 kHz mono left, the same frame format");
  expect(voicePdmGainHalfDb(touch.gainRegister) == -10, "XIAO 1.47\": Seeed's demo gain 30 = register 0x1E = -5 dB");
  expect(!touch.hardwareVerified, "XIAO 1.47\": marked unverified on hardware");
#if defined(RAILY_BOARD_XIAO147) && RAILY_BOARD_XIAO147
  expect(&VOICE_PDM_BOARD == &VOICE_PDM_XIAO147, "RAILY_BOARD_XIAO147=1 selects the touch board");
#else
  expect(&VOICE_PDM_BOARD == &VOICE_PDM_XIAO_SENSE, "the default board is P1");
#endif
  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("voice_board: all passed\n");
  return 0;
}
